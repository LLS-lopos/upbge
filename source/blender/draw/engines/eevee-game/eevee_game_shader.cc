/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_shader.hh"

#include "GPU_material.hh"
#include "DNA_material_types.h"

/* Delegate to EEVEE's ShaderModule singleton.
 * This is the correct approach: eevee::ShaderModule owns the codegen_callback and
 * pass_replacement_cb closures that are required for correct material shader compilation.
 * Reimplementing those here would require duplicating eevee::ShaderModule::material_create_info_amend()
 * (1000+ lines). The singleton is safe to share: GPUMaterials are cached per-nodetree
 * (in blender_mat->gpumaterial) and the cache key includes GPU_MAT_EEVEE so lookups are O(1). */
#include "../eevee/eevee_shader.hh"
#include "../eevee/eevee_material.hh"

namespace blender::eevee_game {

ShaderModule::ShaderModule()
{
  for (int i = 0; i < MAX_SHADER_TYPE; i++) {
    shaders_[i].name   = static_shader_name_get(static_cast<eShaderType>(i));
    shaders_[i].shader = nullptr;
  }
}

ShaderModule::~ShaderModule()
{
  for (int i = 0; i < MAX_SHADER_TYPE; i++) {
    if (shaders_[i].shader) {
      GPU_shader_free(shaders_[i].shader);
      shaders_[i].shader = nullptr;
    }
  }
}

/* static */ void ShaderModule::module_free()
{
  /* Nothing to do here; instance destructor frees per-shader objects.
   * This hook exists for Engine::free_static() at Blender shutdown. */
}

gpu::Shader *ShaderModule::static_shader_get(eShaderType type)
{
  BLI_assert(type >= 0 && type < MAX_SHADER_TYPE);
  if (shaders_[type].shader == nullptr) {
    /* Synchronous fallback compile - only hit if async pre-load was skipped.
     * This stalls the frame; prefer calling static_shaders_load() at init time. */
    shaders_[type].shader = GPU_shader_create_from_info_name(shaders_[type].name);
    BLI_assert_msg(shaders_[type].shader != nullptr, "eevee_game shader compilation failed");
  }
  return shaders_[type].shader;
}

/* static */
const char *ShaderModule::static_shader_name_get(eShaderType type)
{
  switch (type) {
    /* --- Deferred Pipeline --- */
    case SH_DEFERRED_LIGHT:       return "eevee_game_deferred_light";
    case SH_DEFERRED_COMBINE:     return "eevee_game_deferred_combine";
    case SH_DEFERRED_TILE_CLASSIFY: return "eevee_game_tile_classify";
    case SH_GBUFFER:              return "eevee_game_gbuffer";

    /* --- Optimized Shadows --- */
    case SH_SHADOW_DIRECTIONAL_CSM: return "eevee_game_shadow_csm";
    case SH_SHADOW_PUNCTUAL_ATLAS:  return "eevee_game_shadow_atlas";
    case SH_SHADOW_PCSS_FILTER:     return "eevee_game_shadow_pcss";

    /* --- Fast SSR --- */
    case SH_SSR_TRACE:            return "eevee_game_ssr_hiz_trace";

    /* --- GTAO --- */
    case SH_GTAO_MAIN:            return "eevee_game_gtao_main";
    case SH_GTAO_UPSAMPLE:        return "eevee_game_gtao_upsample";

    /* --- SSGI --- */
    case SH_SSGI_MAIN:            return "eevee_game_ssgi_main";
    case SH_SSGI_BLUR:            return "eevee_game_ssgi_blur";

    /* --- Bloom --- */
    case SH_BLOOM_DOWNSAMPLE:     return "eevee_game_bloom_downsample";
    case SH_BLOOM_UPSAMPLE:       return "eevee_game_bloom_upsample";

    /* --- Volumetrics --- */
    case SH_VOLUME_SCATTER:       return "eevee_game_volume_scatter";
    case SH_VOLUME_INTEGRATE:     return "eevee_game_volume_integrate";

    /* --- GPU Culling --- */
    case SH_CULLING_COMPUTE:      return "eevee_game_culling_compute";

    /* --- Forward --- */
    case SH_FORWARD:              return "eevee_game_forward";

    /* --- Anti-Aliasing --- */
    case SH_FXAA:                 return "eevee_game_fxaa";
    case SH_SMAA_EDGE:            return "eevee_game_smaa_edge";
    case SH_SMAA_WEIGHT:          return "eevee_game_smaa_weight";
    case SH_SMAA_BLEND:           return "eevee_game_smaa_blend";

    /* --- DoF --- */
    case SH_DOF_COC_SETUP:        return "eevee_game_dof_coc_setup";
    case SH_DOF_BOKEH_BLUR:       return "eevee_game_dof_bokeh_blur";
    case SH_DOF_RESOLVE:          return "eevee_game_dof_resolve";
    case SH_MOTION_BLUR_FAST:     return "eevee_game_motion_blur_fast";

    /* --- Infrastructure --- */
    case SH_HIZ_UPDATE:           return "eevee_game_hiz_update";
    case SH_FILM_PRESENT:         return "eevee_game_film_present";

    default:
      BLI_assert_unreachable();
      return "";
  }
}

void ShaderModule::static_shaders_load(ShaderGroups groups)
{
  /* Async compilation placeholder — replace when GPU async API is available. */
  (void)groups;
}

/* ================================================================
 * material_shader_get
 *
 * Delegates entirely to eevee::ShaderModule::module_get()->material_shader_get().
 *
 * Why delegate instead of calling GPU_material_from_nodetree() directly:
 *
 *   GPU_material_from_nodetree() requires two engine-specific callbacks:
 *     - codegen_callback: calls ShaderModule::material_create_info_amend()
 *       which sets up 1000+ lines of per-pipeline shader create info.
 *     - pass_replacement_cb: returns optimized default GPUPass for shadow/prepass
 *       when the material has no displacement/transparency — saves significant
 *       shader compilation time at runtime.
 *
 *   Both callbacks are tightly coupled to eevee::ShaderModule internals.
 *   Duplicating them here would be a maintenance burden with no benefit.
 *
 *   eevee::ShaderModule::module_get() returns a static singleton that is
 *   initialised when the EEVEE engine registers itself (before any viewport
 *   render begins). The cache lives on blender_mat->gpumaterial (ListBase on
 *   the DNA struct), so compiled GPUMaterials are shared across all engines
 *   that use GPU_MAT_EEVEE — exactly what we want.
 * ================================================================ */
GPUMaterial *ShaderModule::material_shader_get(blender::Material *material,
                                                bNodeTree *ntree,
                                                eevee::eMaterialPipeline pipeline_type,
                                                eevee::eMaterialGeometry geometry_type,
                                                bool deferred,
                                                blender::Material *default_mat)
{
  return eevee::ShaderModule::module_get()->material_shader_get(
      material, ntree, pipeline_type, geometry_type, deferred, default_mat);
}

} // namespace blender::eevee_game
