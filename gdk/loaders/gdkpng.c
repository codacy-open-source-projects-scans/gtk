/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gdkpngprivate.h"

#include <glib/gi18n-lib.h>
#include "gdkcolorstateprivate.h"
#include "gdkmemoryformatprivate.h"
#include "gdkmemorytexturebuilder.h"
#include "gdkprofilerprivate.h"
#include "gdktexturedownloaderprivate.h"
#include "gsk/gl/fp16private.h"

#include <png.h>
#include <stdio.h>

/* The main difference between the png load/save code here and
 * gdk-pixbuf is that we can support loading 16-bit data in the
 * future, and we can extract gamma and colorspace information
 * to produce linear, color-corrected data.
 */

/* {{{ Callbacks */

/* No sigsetjmp on Windows */
#ifndef HAVE_SIGSETJMP
#define sigjmp_buf jmp_buf
#define sigsetjmp(jb, x) setjmp(jb)
#define siglongjmp longjmp
#endif

typedef struct
{
  guchar *data;
  gsize size;
  gsize position;
} png_io;


static void
png_read_func (png_structp png,
               png_bytep   data,
               png_size_t  size)
{
  png_io *io;

  io = png_get_io_ptr (png);

  if (io->position + size > io->size)
    png_error (png, "Read past EOF");

  memcpy (data, io->data + io->position, size);
  io->position += size;
}

static void
png_write_func (png_structp png,
                png_bytep   data,
                png_size_t  size)
{
  png_io *io;

  io = png_get_io_ptr (png);

  if (io->position > io->size ||
      io->size - io->position  < size)
    {
      io->size = io->position + size;
      io->data = g_realloc (io->data, io->size);
    }

  memcpy (io->data + io->position, data, size);
  io->position += size;
}

static void
png_flush_func (png_structp png)
{
}

static png_voidp
png_malloc_callback (png_structp o,
                     png_size_t  size)
{
  return g_try_malloc (size);
}

static void
png_free_callback (png_structp o,
                   png_voidp   x)
{
  g_free (x);
}

G_GNUC_NORETURN static void
png_simple_error_callback (png_structp     png,
                           png_const_charp error_msg)
{
  GError **error = png_get_error_ptr (png);

  if (error && !*error)
    g_set_error (error,
                 GDK_TEXTURE_ERROR, GDK_TEXTURE_ERROR_CORRUPT_IMAGE,
                 _("Error reading png (%s)"), error_msg);

  longjmp (png_jmpbuf (png), 1);
}

static void
png_simple_warning_callback (png_structp     png,
                             png_const_charp error_msg)
{
}

/* }}} */
/* {{{ Color profile handling */

typedef struct
{
  gboolean cicp_chunk_read;
  int color_primaries;
  int transfer_function;
  int matrix_coefficients;
  int full_range;
} CICPData;

static int
png_read_chunk_func (png_structp        png,
                     png_unknown_chunkp chunk)
{
  if (strcmp ((char *) chunk->name, "cICP") == 0 &&
      chunk->size == 4)
    {
      CICPData *cicp = png_get_user_chunk_ptr (png);

      cicp->cicp_chunk_read = TRUE;

      cicp->color_primaries = chunk->data[0];
      cicp->transfer_function = chunk->data[1];
      cicp->matrix_coefficients = chunk->data[2];
      cicp->full_range = chunk->data[3];

      return 1;
    }

  return 0;
}

/* we use "chunk_read" here to mean "is supported" */
static const CICPData colorstate_cicp[GDK_COLOR_STATE_N_IDS] = {
  [GDK_COLOR_STATE_ID_SRGB] = { TRUE, 1, 13, 0, 1 },
  [GDK_COLOR_STATE_ID_SRGB_LINEAR] = { TRUE, 1, 8, 0, 1 },
  [GDK_COLOR_STATE_ID_REC2100_PQ] = { TRUE, 9, 16, 0, 1 },
  [GDK_COLOR_STATE_ID_REC2100_LINEAR] = { TRUE, 9, 8, 0, 1 }
};

static GdkColorState *
gdk_png_get_color_state_from_cicp (const CICPData *cicp)
{
  gsize i;

  for (i = 0; i < GDK_COLOR_STATE_N_IDS; i++)
    {
      if (!colorstate_cicp[i].cicp_chunk_read)
        continue;

      if (colorstate_cicp[i].color_primaries == cicp->color_primaries &&
          colorstate_cicp[i].transfer_function == cicp->transfer_function &&
          colorstate_cicp[i].matrix_coefficients == cicp->matrix_coefficients &&
          colorstate_cicp[i].full_range == cicp->full_range)
        return gdk_color_state_ref ((GdkColorState *) &gdk_default_color_states[i]);
    }

  return NULL;
}

static GdkColorState *
gdk_png_get_color_state (png_struct  *png,
                         png_info    *info,
                         GError     **error)
{
  GdkColorState *color_state;
  CICPData *cicp;
  int intent;

  cicp = png_get_user_chunk_ptr (png);

  if (cicp->cicp_chunk_read)
    {
      color_state = gdk_png_get_color_state_from_cicp (cicp);
      if (color_state)
        {
          g_debug ("Color state from cICP data: %s", gdk_color_state_get_name (color_state));
          return color_state;
        }
      else
        {
          g_set_error (error,
                       GDK_TEXTURE_ERROR, GDK_TEXTURE_ERROR_UNSUPPORTED_CONTENT,
                       /* Translators: the variables at the end are just the numbers as they appear in the PNG */
                       _("Unsupported CICP color properties: %d/%d/%d %d"),
                       cicp->color_primaries,
                       cicp->transfer_function,
                       cicp->matrix_coefficients,
                       cicp->full_range);
        }
    }

#if 0
  if (png_get_iCCP (png, info, &name, NULL, &icc_data, &icc_len))
    {
    }
#endif

  if (png_get_sRGB (png, info, &intent))
    return gdk_color_state_ref (GDK_COLOR_STATE_SRGB);

  /* If neither of those is valid, the result is sRGB */
  if (!png_get_valid (png, info, PNG_INFO_gAMA) &&
      !png_get_valid (png, info, PNG_INFO_cHRM))
    return GDK_COLOR_STATE_SRGB;

  g_debug ("Failed to find color state, assuming SRGB");

  return GDK_COLOR_STATE_SRGB;
}

static void
gdk_png_set_color_state (png_struct    *png,
                         png_info      *info,
                         GdkColorState *color_state,
                         png_byte      *chunk_data)
{
  const CICPData *cicp;

  if (GDK_IS_DEFAULT_COLOR_STATE (color_state))
    cicp = &colorstate_cicp[GDK_DEFAULT_COLOR_STATE_ID (color_state)];
  else
    cicp = NULL;

  if (cicp && cicp->cicp_chunk_read)
    {
      png_unknown_chunk chunk = {
        .name = { 'c', 'I', 'C', 'P', '\0' },
        .data = chunk_data,
        .size = 4,
        .location = PNG_HAVE_IHDR,
      };

      chunk_data[0] = (png_byte) cicp->color_primaries;
      chunk_data[1] = (png_byte) cicp->transfer_function;
      chunk_data[2] = (png_byte) cicp->matrix_coefficients;
      chunk_data[3] = (png_byte) cicp->full_range;

      png_set_unknown_chunks (png, info, &chunk, 1);
    }
  else
    {
      /* unsupported color state. Fall back to sRGB */
      gdk_color_state_unref (color_state);
      color_state = gdk_color_state_ref (gdk_color_state_get_srgb ());
    }

  /* For good measure, we add an sRGB chunk too */
  if (gdk_color_state_equal (color_state, GDK_COLOR_STATE_SRGB))
    png_set_sRGB (png, info, PNG_sRGB_INTENT_PERCEPTUAL);
}

/* }}} */
/* {{{ Public API */

GdkTexture *
gdk_load_png (GBytes      *bytes,
              GHashTable  *options,
              GError     **error)
{
  png_io io;
  png_struct *png = NULL;
  png_info *info;
  png_textp text;
  int num_texts;
  guint width, height;
  gsize i, stride;
  int depth, color_type;
  int interlace;
  GdkMemoryTextureBuilder *builder;
  GdkMemoryFormat format;
  guchar *buffer = NULL;
  guchar **row_pointers = NULL;
  GBytes *out_bytes;
  GdkColorState *color_state;
  GdkTexture *texture;
  int bpp;
  CICPData cicp = { FALSE, };

  G_GNUC_UNUSED gint64 before = GDK_PROFILER_CURRENT_TIME;

  io.data = (guchar *)g_bytes_get_data (bytes, &io.size);
  io.position = 0;

  png = png_create_read_struct_2 (PNG_LIBPNG_VER_STRING,
                                  error,
                                  png_simple_error_callback,
                                  png_simple_warning_callback,
                                  NULL,
                                  png_malloc_callback,
                                  png_free_callback);
  if (png == NULL)
    g_error ("Out of memory");

  info = png_create_info_struct (png);
  if (info == NULL)
    g_error ("Out of memory");

  png_set_read_fn (png, &io, png_read_func);
  png_set_read_user_chunk_fn (png, &cicp, png_read_chunk_func);

  if (sigsetjmp (png_jmpbuf (png), 1))
    {
      g_free (buffer);
      g_free (row_pointers);
      png_destroy_read_struct (&png, &info, NULL);
      return NULL;
    }

  png_read_info (png, info);

  png_get_IHDR (png, info,
                &width, &height, &depth,
                &color_type, &interlace, NULL, NULL);

  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb (png);

  if (color_type == PNG_COLOR_TYPE_GRAY)
    png_set_expand_gray_1_2_4_to_8 (png);

  if (png_get_valid (png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha (png);

  if (depth < 8)
    png_set_packing (png);

  if (interlace != PNG_INTERLACE_NONE)
    png_set_interlace_handling (png);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  png_set_swap (png);
#endif

  png_read_update_info (png, info);
  png_get_IHDR (png, info,
                &width, &height, &depth,
                &color_type, &interlace, NULL, NULL);
  if (depth != 8 && depth != 16)
    {
      png_destroy_read_struct (&png, &info, NULL);
      g_set_error (error,
                   GDK_TEXTURE_ERROR, GDK_TEXTURE_ERROR_UNSUPPORTED_CONTENT,
                   _("Unsupported depth %u in png image"), depth);
      return NULL;
    }

  switch (color_type)
    {
    case PNG_COLOR_TYPE_RGB_ALPHA:
      if (depth == 8)
        {
          format = GDK_MEMORY_R8G8B8A8;
        }
      else
        {
          format = GDK_MEMORY_R16G16B16A16;
        }
      break;
    case PNG_COLOR_TYPE_RGB:
      if (depth == 8)
        {
          format = GDK_MEMORY_R8G8B8;
        }
      else if (depth == 16)
        {
          format = GDK_MEMORY_R16G16B16;
        }
      break;
    case PNG_COLOR_TYPE_GRAY:
      if (depth == 8)
        {
          format = GDK_MEMORY_G8;
        }
      else if (depth == 16)
        {
          format = GDK_MEMORY_G16;
        }
      break;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
      if (depth == 8)
        {
          format = GDK_MEMORY_G8A8;
        }
      else if (depth == 16)
        {
          format = GDK_MEMORY_G16A16;
        }
      break;
    default:
      png_destroy_read_struct (&png, &info, NULL);
      g_set_error (error,
                   GDK_TEXTURE_ERROR, GDK_TEXTURE_ERROR_UNSUPPORTED_CONTENT,
                   _("Unsupported color type %u in png image"), color_type);
      return NULL;
    }

  color_state = gdk_png_get_color_state (png, info, error);
  if (color_state == NULL)
    return NULL;

  bpp = gdk_memory_format_bytes_per_pixel (format);
  if (!g_size_checked_mul (&stride, width, bpp) ||
      !g_size_checked_add (&stride, stride, (8 - stride % 8) % 8))
    {
      g_set_error (error,
                   GDK_TEXTURE_ERROR, GDK_TEXTURE_ERROR_TOO_LARGE,
                   _("Image stride too large for image size %ux%u"), width, height);
      return NULL;
    }

  buffer = g_try_malloc_n (height, stride);
  row_pointers = g_try_malloc_n (height, sizeof (char *));

  if (!buffer || !row_pointers)
    {
      gdk_color_state_unref (color_state);
      g_free (buffer);
      g_free (row_pointers);
      png_destroy_read_struct (&png, &info, NULL);
      g_set_error (error,
                   GDK_TEXTURE_ERROR, GDK_TEXTURE_ERROR_TOO_LARGE,
                   _("Not enough memory for image size %ux%u"), width, height);
      return NULL;
    }

  for (i = 0; i < height; i++)
    row_pointers[i] = &buffer[i * stride];

  png_read_image (png, row_pointers);
  png_read_end (png, info);

  out_bytes = g_bytes_new_take (buffer, height * stride);
  builder = gdk_memory_texture_builder_new ();
  gdk_memory_texture_builder_set_format (builder, format);
  gdk_memory_texture_builder_set_color_state (builder, color_state);
  gdk_memory_texture_builder_set_width (builder, width);
  gdk_memory_texture_builder_set_height (builder, height);
  gdk_memory_texture_builder_set_bytes (builder, out_bytes);
  gdk_memory_texture_builder_set_stride (builder, stride);
  texture = gdk_memory_texture_builder_build (builder);
  g_object_unref (builder);
  g_bytes_unref (out_bytes);
  gdk_color_state_unref (color_state);

  if (options && png_get_text (png, info, &text, &num_texts))
    {
      for (i = 0; i < num_texts; i++)
        {
          if (text->compression != -1)
            continue;

          g_hash_table_insert (options, g_strdup (text->key), g_strdup (text->text));
        }
    }

  g_free (row_pointers);
  png_destroy_read_struct (&png, &info, NULL);

  if (GDK_PROFILER_IS_RUNNING)
    {
      gint64 end = GDK_PROFILER_CURRENT_TIME;
      if (end - before > 500000)
        gdk_profiler_add_mark (before, end - before, "Load png", NULL);
    }

  return texture;
}

GBytes *
gdk_save_png (GdkTexture *texture)
{
  png_struct *png = NULL;
  png_info *info;
  png_io io = { NULL, 0, 0 };
  int width, height;
  int y;
  GdkMemoryFormat format;
  GdkTextureDownloader downloader;
  GBytes *bytes;
  gsize stride;
  const guchar *data;
  GdkColorState *color_state;
  int png_format;
  int depth;
  png_byte chunk_data[4];

  width = gdk_texture_get_width (texture);
  height = gdk_texture_get_height (texture);
  color_state = gdk_texture_get_color_state (texture);
  format = gdk_texture_get_format (texture);

  switch (format)
    {
    case GDK_MEMORY_B8G8R8A8_PREMULTIPLIED:
    case GDK_MEMORY_A8R8G8B8_PREMULTIPLIED:
    case GDK_MEMORY_R8G8B8A8_PREMULTIPLIED:
    case GDK_MEMORY_A8B8G8R8_PREMULTIPLIED:
    case GDK_MEMORY_B8G8R8A8:
    case GDK_MEMORY_A8R8G8B8:
    case GDK_MEMORY_R8G8B8A8:
    case GDK_MEMORY_A8B8G8R8:
      format = GDK_MEMORY_R8G8B8A8;
      png_format = PNG_COLOR_TYPE_RGB_ALPHA;
      depth = 8;
      break;

    case GDK_MEMORY_R8G8B8:
    case GDK_MEMORY_B8G8R8:
    case GDK_MEMORY_R8G8B8X8:
    case GDK_MEMORY_X8R8G8B8:
    case GDK_MEMORY_B8G8R8X8:
    case GDK_MEMORY_X8B8G8R8:
      format = GDK_MEMORY_R8G8B8;
      png_format = PNG_COLOR_TYPE_RGB;
      depth = 8;
      break;

    case GDK_MEMORY_R16G16B16A16:
    case GDK_MEMORY_R16G16B16A16_PREMULTIPLIED:
    case GDK_MEMORY_R16G16B16A16_FLOAT:
    case GDK_MEMORY_R16G16B16A16_FLOAT_PREMULTIPLIED:
    case GDK_MEMORY_R32G32B32A32_FLOAT:
    case GDK_MEMORY_R32G32B32A32_FLOAT_PREMULTIPLIED:
      format = GDK_MEMORY_R16G16B16A16;
      png_format = PNG_COLOR_TYPE_RGB_ALPHA;
      depth = 16;
      break;

    case GDK_MEMORY_R16G16B16:
    case GDK_MEMORY_R16G16B16_FLOAT:
    case GDK_MEMORY_R32G32B32_FLOAT:
      format = GDK_MEMORY_R16G16B16;
      png_format = PNG_COLOR_TYPE_RGB;
      depth = 16;
      break;

    case GDK_MEMORY_G8:
      format = GDK_MEMORY_G8;
      png_format = PNG_COLOR_TYPE_GRAY;
      depth = 8;
      break;

    case GDK_MEMORY_G8A8_PREMULTIPLIED:
    case GDK_MEMORY_G8A8:
    case GDK_MEMORY_A8:
      format = GDK_MEMORY_G8A8;
      png_format = PNG_COLOR_TYPE_GRAY_ALPHA;
      depth = 8;
      break;

    case GDK_MEMORY_G16:
      format = GDK_MEMORY_G16;
      png_format = PNG_COLOR_TYPE_GRAY;
      depth = 16;
      break;

    case GDK_MEMORY_G16A16_PREMULTIPLIED:
    case GDK_MEMORY_G16A16:
    case GDK_MEMORY_A16:
    case GDK_MEMORY_A16_FLOAT:
    case GDK_MEMORY_A32_FLOAT:
      format = GDK_MEMORY_G16A16;
      png_format = PNG_COLOR_TYPE_GRAY_ALPHA;
      depth = 16;
      break;

    case GDK_MEMORY_N_FORMATS:
    default:
      g_assert_not_reached ();
    }

  png = png_create_write_struct_2 (PNG_LIBPNG_VER_STRING, NULL,
                                   png_simple_error_callback,
                                   png_simple_warning_callback,
                                   NULL,
                                   png_malloc_callback,
                                   png_free_callback);
  if (!png)
    return NULL;

  /* 2^31-1 is the maximum size for PNG files */
  png_set_user_limits (png, (1u << 31) - 1, (1u << 31) - 1);

  png_set_keep_unknown_chunks (png, PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);

  info = png_create_info_struct (png);
  if (!info)
    {
      png_destroy_read_struct (&png, NULL, NULL);
      return NULL;
    }

  gdk_color_state_ref (color_state);
  bytes = NULL;

  if (sigsetjmp (png_jmpbuf (png), 1))
    {
      gdk_color_state_unref (color_state);
      g_clear_pointer (&bytes, g_bytes_unref);
      g_free (io.data);
      png_destroy_read_struct (&png, &info, NULL);
      return NULL;
    }

  png_set_write_fn (png, &io, png_write_func, png_flush_func);

  png_set_IHDR (png, info, width, height, depth,
                png_format,
                PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_DEFAULT,
                PNG_FILTER_TYPE_DEFAULT);

  gdk_png_set_color_state (png, info, color_state, chunk_data);

  png_write_info (png, info);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  png_set_swap (png);
#endif

  gdk_texture_downloader_init (&downloader, texture);
  gdk_texture_downloader_set_format (&downloader, format);
  bytes = gdk_texture_downloader_download_bytes (&downloader, &stride);
  gdk_texture_downloader_finish (&downloader);
  data = g_bytes_get_data (bytes, NULL);

  for (y = 0; y < height; y++)
    png_write_row (png, data + y * stride);

  png_write_end (png, info);

  png_destroy_write_struct (&png, &info);

  gdk_color_state_unref (color_state);
  g_bytes_unref (bytes);

  return g_bytes_new_take (io.data, io.size);
}

/* }}} */

/* vim:set foldmethod=marker expandtab: */
