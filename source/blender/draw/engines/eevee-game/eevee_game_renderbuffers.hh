/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "eevee_game_defines.hh"
#include "GPU_texture.hh"

namespace blender::eevee_game {

/**
 * Metadata about the G-Buffer layout.
 * Sent to the GPU to identify which layer contains which data.
 */
struct RenderBuffersInfoData {
  int color_len;
  int value_len;

  int normal_id;
  int diffuse_light_id;
  int specular_light_id;
  int ambient_occlusion_id;
  
  // AOV and Custom Pass info
  struct {
    int color_len;
    int value_len;
  } aovs;
};

/**
 * GameRenderBuffers handles the allocation and lifetime of all 
 * intermediate textures used during the frame.
 */
class RenderBuffers {
 public:
  void init();
  
  // Allocate textures based on the current render resolution
  void acquire(int2 extent);
  
  // Release pooled textures to save memory
  void release();

  // Core Render Targets
  gpu::Texture depth_tx;      // SFLOAT_32 for precision
  gpu::Texture combined_tx;   // SFLOAT_16_16_16_16 (HDR Color)
  gpu::Texture vector_tx;     // SFLOAT_16_16 (Motion Vectors)

  // G-Buffer (Closures and Render Passes)
  // These are texture arrays where each slice is a Closure data layer
  gpu::Texture rp_color_tx;   // Array of SFLOAT_16_16_16_16
  gpu::Texture rp_value_tx;   // Array of SFLOAT_16 (Single Channel)

  // FSR 3.0 Specific Buffers
  gpu::Texture reactive_mask_tx; // R8 (Protects smoke/alpha)
  gpu::Texture transp_mask_tx;   // R8 (Protects glass/composition)
  gpu::Texture ui_color_tx;      // RGBA8 (HUD buffer at full resolution)

  RenderBuffersInfoData data;

 private:
  int2 extent_;
  class GameInstance &inst_;

  friend class GameInstance;
  RenderBuffers(class GameInstance &inst) : inst_(inst) {}
};

} // namespace blender::eevee_game