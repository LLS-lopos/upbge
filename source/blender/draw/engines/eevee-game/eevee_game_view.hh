/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "eevee_game_defines.hh"
#include "draw_pass.hh"

namespace blender::eevee_game {

/**
 * ShadingView represents a single point of view (Main Camera).
 * It orchestrates the high-level sequence of rendering passes.
 */
class ShadingView {
 public:
  void init(const char *name);
  
  // The main entry point for rendering the frame
  void render();

 private:
  // Updates camera matrices and handles sub-pixel jitter for FSR
  void update_view();
  
  // Handles Bloom, DoF, and Anti-Aliasing/Upscaling
  gpu::Texture *render_postfx(gpu::Texture *input_tx);
  
  // Executes the depth/velocity prepass
  void render_prepass();

  const char *name_;
  int2 extent_; // Internal render resolution (potentially lower than display)
  
  // Render View handles (Culling and Matrices)
  View main_view_;
  View render_view_;

  // Framebuffers
  Framebuffer combined_fb_;
  Framebuffer gbuffer_fb_;
  Framebuffer prepass_fb_;

  // Post-Processing chain textures
  gpu::Texture postfx_tx_;      // Intermediary for Bloom/DoF
  gpu::Texture display_res_tx_; // High-resolution output for FSR 3.0

  class GameInstance &inst_;

  friend class GameInstance;
  ShadingView(class GameInstance &inst) : inst_(inst) {}
};

} // namespace blender::eevee_game