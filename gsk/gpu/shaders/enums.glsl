#ifndef _ENUMS_
#define _ENUMS_

#define GSK_GPU_PATTERN_STACK_SIZE 16

#define GSK_GPU_SHADER_CLIP_NONE 0u
#define GSK_GPU_SHADER_CLIP_RECT 1u
#define GSK_GPU_SHADER_CLIP_ROUNDED 2u

#define GSK_GPU_PATTERN_DONE 0u
#define GSK_GPU_PATTERN_COLOR 1u
#define GSK_GPU_PATTERN_OPACITY 2u
#define GSK_GPU_PATTERN_TEXTURE 3u
#define GSK_GPU_PATTERN_STRAIGHT_ALPHA 4u
#define GSK_GPU_PATTERN_COLOR_MATRIX 5u
#define GSK_GPU_PATTERN_GLYPHS 6u
#define GSK_GPU_PATTERN_LINEAR_GRADIENT 7u
#define GSK_GPU_PATTERN_REPEATING_LINEAR_GRADIENT 8u
#define GSK_GPU_PATTERN_RADIAL_GRADIENT 9u
#define GSK_GPU_PATTERN_REPEATING_RADIAL_GRADIENT 10u
#define GSK_GPU_PATTERN_CONIC_GRADIENT 11u
#define GSK_GPU_PATTERN_CLIP 12u
#define GSK_GPU_PATTERN_ROUNDED_CLIP 13u
#define GSK_GPU_PATTERN_REPEAT_PUSH 14u
#define GSK_GPU_PATTERN_POSITION_POP 15u
#define GSK_GPU_PATTERN_PUSH_COLOR 16u
#define GSK_GPU_PATTERN_POP_CROSS_FADE 17u
#define GSK_GPU_PATTERN_POP_MASK_ALPHA 18u
#define GSK_GPU_PATTERN_POP_MASK_INVERTED_ALPHA 19u
#define GSK_GPU_PATTERN_POP_MASK_LUMINANCE 20u
#define GSK_GPU_PATTERN_POP_MASK_INVERTED_LUMINANCE 21u
#define GSK_GPU_PATTERN_AFFINE 22u
#define GSK_GPU_PATTERN_BLEND_DEFAULT 23u
#define GSK_GPU_PATTERN_BLEND_MULTIPLY 24u
#define GSK_GPU_PATTERN_BLEND_SCREEN 25u
#define GSK_GPU_PATTERN_BLEND_OVERLAY 26u
#define GSK_GPU_PATTERN_BLEND_DARKEN 27u
#define GSK_GPU_PATTERN_BLEND_LIGHTEN 28u
#define GSK_GPU_PATTERN_BLEND_COLOR_DODGE 29u
#define GSK_GPU_PATTERN_BLEND_COLOR_BURN 30u
#define GSK_GPU_PATTERN_BLEND_HARD_LIGHT 31u
#define GSK_GPU_PATTERN_BLEND_SOFT_LIGHT 32u
#define GSK_GPU_PATTERN_BLEND_DIFFERENCE 33u
#define GSK_GPU_PATTERN_BLEND_EXCLUSION 34u
#define GSK_GPU_PATTERN_BLEND_COLOR 35u
#define GSK_GPU_PATTERN_BLEND_HUE 36u
#define GSK_GPU_PATTERN_BLEND_SATURATION 37u
#define GSK_GPU_PATTERN_BLEND_LUMINOSITY 38u

#define GSK_MASK_MODE_ALPHA 0u
#define GSK_MASK_MODE_INVERTED_ALPHA 1u
#define GSK_MASK_MODE_LUMINANCE 2u
#define GSK_MASK_MODE_INVERTED_LUMINANCE 3u

#define GDK_COLOR_STATE_ID_SRGB 0u
#define GDK_COLOR_STATE_ID_SRGB_LINEAR 1u
#define GDK_COLOR_STATE_ID_REC2100_PQ 2u
#define GDK_COLOR_STATE_ID_REC2100_LINEAR 3u

#define GDK_BUILTIN_COLOR_STATE_ID_OKLAB 0u
#define GDK_BUILTIN_COLOR_STATE_ID_OKLCH 1u

#define TOP 0u
#define RIGHT 1u
#define BOTTOM 2u
#define LEFT 3u

#define TOP_LEFT 0u
#define TOP_RIGHT 1u
#define BOTTOM_RIGHT 2u
#define BOTTOM_LEFT 3u

#define SLICE_TOP_LEFT 0u
#define SLICE_TOP 1u
#define SLICE_TOP_RIGHT 2u
#define SLICE_RIGHT 3u
#define SLICE_BOTTOM_RIGHT 4u
#define SLICE_BOTTOM 5u
#define SLICE_BOTTOM_LEFT 6u
#define SLICE_LEFT 7u

#endif
