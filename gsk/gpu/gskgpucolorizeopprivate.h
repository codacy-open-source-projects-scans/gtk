#pragma once

#include "gskgpushaderopprivate.h"

#include <graphene.h>

G_BEGIN_DECLS

void                    gsk_gpu_colorize_op                             (GskGpuFrame                    *frame,
                                                                         GskGpuShaderClip                clip,
                                                                         GskGpuColorStates               color_states,
                                                                         GskGpuDescriptors              *desc,
                                                                         guint32                         descriptor,
                                                                         const graphene_rect_t          *rect,
                                                                         const graphene_point_t         *offset,
                                                                         const graphene_rect_t          *tex_rect,
                                                                         const float                     color[4]);


G_END_DECLS

