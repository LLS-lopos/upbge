/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "eevee_game_defines.hh"
#include "GPU_texture.hh"
#include "draw_handle.hh"  /* draw::ObjectHandle */

namespace blender::eevee_game {

struct VolumeSettings {
  int   tile_size  = 16;    /* Screen-space pixels per froxel XY cell */
  int   samples_z  = 64;    /* Depth slices in the frustum-aligned voxel grid */
  float density    = 0.1f;  /* Homogeneous medium extinction coefficient */
  float anisotropy = 0.7f;  /* Henyey-Greenstein phase function g parameter */
  bool  enabled    = true;
};

/* Frustum-aligned voxel (froxel) volumetric fog.
 *
 * Grid layout:
 *   XY: screen tiles of tile_size pixels each
 *   Z:  exponentially-spaced depth slices (more near the camera)
 *
 * Two-pass approach:
 *   Pass 1 (Scatter): inject light from the Fixed Shadow Atlas into each froxel.
 *   Pass 2 (Integrate): accumulate scattering along each Z column into a transmittance map.
 *
 * The integrated 3D texture is then sampled during the deferred lighting pass. */
class VolumeModule {
 public:
  VolumeModule(class GameInstance &inst);

  void init();
  void sync();

  /**
   * Register an object that carries a volume material so the volume pipeline
   * tracks it for froxel voxelisation. Called from SyncModule::sync_mesh()
   * when mat.has_volume == true. Mirrors eevee::VolumeModule::object_sync().
   */
  void object_sync(const draw::ObjectHandle &ob_handle);

  /* Execute both passes; writes to the integrated froxel texture. */
  void render(View &view);

  /* Composite the integrated volume onto the 2D scene color buffer. */
  void resolve(gpu::Texture *target_color_tx, gpu::Texture *depth_tx);

  gpu::Texture *get_volume_texture() { return volume_integrated_tx_.get(); }

 private:
  GameInstance *inst_;
  VolumeSettings settings_;

  /* 3D RGBA16F: scattering (RGB) + extinction (A) per froxel */
  std::unique_ptr<gpu::Texture> volume_grid_tx_;
  /* 3D RGBA16F: integrated transmittance + in-scattered light after Z accumulation */
  std::unique_ptr<gpu::Texture> volume_integrated_tx_;

  PassSimple volume_scatter_ps_{"Volume.Scatter"};
  PassSimple volume_integration_ps_{"Volume.Integration"};
  PassSimple volume_resolve_ps_{"Volume.Resolve"};
  bool       resolve_synced_ = false; /* True after resolve pass shader is set in sync() */
};

} // namespace blender::eevee_game
