/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_view.hh"

namespace blender::eevee_game {

void ShadingView::render() {
  // Step 1: GPU-Driven Culling (Eliminate non-visible instances)
  inst_.culling.execute(render_view_);

  // Step 2: Depth/Velocity Prepass (Needed for Hi-Z and FSR)
  render_prepass();
  inst_.hiz.update(); // Update depth hierarchy for SSR/Culling

  // Step 3: Main Rendering Pipeline (G-Buffer -> GTAO -> SSGI -> Tiled Light)
  inst_.pipelines.render(render_view_);

  // Step 4: Post-Processing at Render Resolution
  inst_.bloom.render(rbufs.combined_tx);
  inst_.dof.render_fast(rbufs.combined_tx);

  // Step 5: AAA Presentation / Upscaling
  gpu::Texture *final_output;
  if (inst_.upscale_settings.mode != UpscaleMode::OFF) {
    // TEMPORAL UPSCALING (FSR 3.0)
    // Converts Render Resolution to Display Resolution
    inst_.upscale.apply_fsr3(rbufs.combined_tx, display_res_tx, rbufs.ui_color_tx);
    final_output = display_res_tx;
  } else {
    // SPATIAL AA (FXAA / SMAA)
    inst_.pipelines.aa.apply_aa(rbufs.combined_tx, postfx_tx);
    final_output = postfx_tx;
  }

  // Step 6: Direct Presentation to Screen
  inst_.film.present(final_output);
}

} // namespace blender::eevee_game