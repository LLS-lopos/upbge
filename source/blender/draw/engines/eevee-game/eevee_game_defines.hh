/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_math_vector.hh"
#include "BLI_math_matrix.hh"

namespace blender::eevee_game {

/* --- ENGINE CONSTANTS --- */

#define MAX_LIGHTS 512               // Maximum lights for Tiled Deferred evaluation
#define MAX_SHADOW_CASCADES 4        // Standard CSM cascades
#define MAX_GPU_INSTANCES 65536      // Maximum objects for GPU-Driven Culling
#define SHADOW_ATLAS_RES 4096        // Resolution of the fixed shadow atlas
#define VOLUME_FROXEL_Z 64           // Depth slices for volumetric fog
#define RAY_STEPS_MAX 128            // Maximum steps for SSR/SSGI Tracing

/* --- RESOURCE BINDING SLOTS --- */

// Texture Slots
#define SLOT_GBUFER_HEADER   0
#define SLOT_GBUFER_CLOSURE  1
#define SLOT_GBUFER_NORMAL   2
#define SLOT_SHADOW_ATLAS    3
#define SLOT_HIZ_BUFFER      4
#define SLOT_LTC_LUT         5       // Area Light lookup table
#define SLOT_SMAA_AREA_LUT   6
#define SLOT_SMAA_SEARCH_LUT 7
#define SLOT_SSGI_RESULT     8

// Buffer Slots (UBO/SSBO)
#define SLOT_UNIFORM_DATA    0       // Main Scene/View data
#define SLOT_LIGHT_DATA      1       // Light source array
#define SLOT_CULLING_DATA    2       // Object transforms for GPU Culling
#define SLOT_SHADOW_DATA     3       // CSM Matrices and PCSS params

/* --- ENUMERATIONS --- */

enum class LightType : uint32_t {
  SUN = 0,
  PUNCTUAL_POINT = 1,
  PUNCTUAL_SPOT = 2,
  AREA_RECT = 3,
  AREA_ELLIPSE = 4
};

enum class AAMode : uint32_t {
  NONE = 0,
  FXAA = 1,
  SMAA = 2
};

enum class UpscaleMode : uint32_t {
  OFF = 0,
  FSR2_ULTRA_QUALITY = 1,
  FSR2_QUALITY = 2,
  FSR2_BALANCED = 3,
  FSR2_PERFORMANCE = 4
};

/* Stencil Bits for Deferred Hybrid Classification */
enum StencilBits {
  STENCIL_OPAQUE         = (1 << 0),
  STENCIL_TRANSPARENT    = (1 << 1),
  STENCIL_HAIR           = (1 << 2),
  STENCIL_REFRACTIVE     = (1 << 3),
  STENCIL_RECEIVE_SHADOW = (1 << 4),
};

/* --- GPU STRUCTURES (std140 Aligned) --- */

/** 
 * Structure for global scene and view information.
 * Must be 16-byte aligned for GLSL compatibility.
 */
struct UniformData {
  float4x4 viewmat;
  float4x4 projectionmat;
  float4x4 viewprojmat;
  float4x4 viewinv;
  
  float3 camera_pos;
  float time;
  
  float2 screen_res;
  float2 screen_res_inv;
  
  float z_near;
  float z_far;
  float delta_time;
  uint32_t frame_count;
  
  // Anti-Aliasing & Upscaling
  float2 jitter;          // Sub-pixel jitter for FSR 2
  uint32_t aa_mode;       // FXAA, SMAA
  float exposure;
};

/**
 * Optimized Light Data structure.
 */
struct LightData {
  float3 position;
  uint32_t type;
  
  float3 color;
  float energy;
  
  float3 direction;       // For Spot and Sun
  float radius;           // Light size for PCSS/Area
  
  float attenuation;
  float spot_angle;
  int32_t shadow_index;   // Index in Shadow Atlas (-1 if no shadow)
  float padding;
};

/**
 * CSM and Shadow filtering parameters.
 */
struct ShadowUniformData {
  float4x4 cascade_viewproj[MAX_SHADOW_CASCADES];
  float4 cascade_splits;
  
  float pcss_light_radius;
  float pcss_filter_min;
  float shadow_bias;
  uint32_t shadow_map_res;
};

/**
 * GPU Instance data for Indirect Drawing and Culling.
 */
struct GPUInstanceData {
  float4x4 model_matrix;
  float3 bb_min;          // Bounding box for Frustum/Hi-Z culling
  uint32_t resource_id;
  float3 bb_max;
  uint32_t flags;
};

/* --- CLOSURE DEFINITIONS --- */

/**
 * Bitmask for active closures in a material.
 * Used by the Tiled Deferred loop to skip unnecessary calculations.
 */
enum ClosureBits {
  CLOSURE_NONE        = 0,
  CLOSURE_DIFFUSE     = (1 << 0),
  CLOSURE_GLOSSY      = (1 << 1),
  CLOSURE_REFRACTION  = (1 << 2),
  CLOSURE_TRANSLUCENT = (1 << 3),
  CLOSURE_SSS         = (1 << 4),
  CLOSURE_EMISSION    = (1 << 5),
};

} // namespace blender::eevee_game