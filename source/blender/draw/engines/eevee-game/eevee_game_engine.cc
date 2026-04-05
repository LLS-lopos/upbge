/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_engine.h"
#include "eevee_game_instance.hh"

#include "RE_pipeline.h"
#include "DRW_render.hh"

/* eevee::ShaderModule is a static singleton with reference counting.
 * We acquire it in init_static() and release it in free_static() so
 * eevee_game owns a reference independently of whether EEVEE is open. */
#include "../eevee/eevee_shader.hh"

namespace blender::eevee_game {

DrawEngine *Engine::create_instance()
{
  return new GameInstance();
}

void Engine::init_static()
{
  /* Acquire a reference to EEVEE's ShaderModule singleton.
   *
   * StaticShaderCache<T>::get() is reference-counted and thread-safe.
   * If EEVEE is already open (or registered), the refcount increments.
   * If not (e.g. standalone game player), this call constructs the singleton.
   *
   * We need this because eevee_game::ShaderModule::material_shader_get()
   * delegates to eevee::ShaderModule for the codegen_callback and
   * pass_replacement_cb — both are deeply coupled to EEVEE's shader infos.
   * Without this init, those callbacks would run on a null or invalid module. */
  eevee::ShaderModule::module_get();
}

void Engine::free_static()
{
  /* Release eevee_game's own compiled static shaders */
  ShaderModule::module_free();

  /* Release our reference to the EEVEE ShaderModule singleton.
   * If EEVEE still has its own reference, the object stays alive.
   * If we were the last holder (standalone player), it is destroyed here. */
  eevee::ShaderModule::module_free();
}

/* ---- Static render callback (F12 / Render Image) ----
 *
 * DRW_render_to_image expects a plain C function pointer, so we cannot use a lambda
 * with captures. Instead we store the GameInstance* in the engine's 'type_data'
 * pointer (which persists for the duration of the render) and recover it inside
 * the static trampoline. */

struct RenderJobData {
  GameInstance *instance;
};

static void eevee_game_render_to_image(RenderEngine *engine,
                                       RenderLayer  *layer,
                                       const rcti    *rect)
{
  auto *job = static_cast<RenderJobData *>(engine->type->type_data);
  if (!job || !job->instance) {
    return;
  }

  const int2 size = int2(engine->resolution_x, engine->resolution_y);

  /* Initialize GameInstance for a static high-res render.
   * output_rect may be non-null for border renders. */
  job->instance->init(size, rect);

  /* Execute the full AAA game render loop for one still frame */
  job->instance->render_frame(engine, layer);
}

static void eevee_game_render(RenderEngine *engine, Depsgraph *depsgraph)
{
  RenderJobData job;
  job.instance = new GameInstance();

  /* Attach job data to the engine type so the trampoline can find it */
  engine->type->type_data = &job;

  DRW_render_to_image(engine, depsgraph, eevee_game_render_to_image, nullptr);

  engine->type->type_data = nullptr;
  delete job.instance;
}

/* ---- Render pass registration ----
 * Declares which AOV outputs this engine can produce.
 * Called by Blender when building the compositor node graph. */
static void eevee_game_render_update_passes(RenderEngine *engine,
                                            Scene        *scene,
                                            ViewLayer    *view_layer)
{
  RE_engine_register_pass(
      engine, scene, view_layer, RE_PASSNAME_COMBINED, 4, "RGBA", SOCK_RGBA);
  RE_engine_register_pass(
      engine, scene, view_layer, RE_PASSNAME_VELOCITY, 4, "XYZW", SOCK_VECTOR);

  /* FSR Reactive Mask debug output - only registered if the scene flag is set.
   * This flag must be added to the EEVEE scene DNA (see DNA notes at end of file). */
  if (scene->eevee.flag & SCE_EEVEE_GAME_DEBUG_FSR_MASK) {
    RE_engine_register_pass(
        engine, scene, view_layer, "FSR2_Reactive", 1, "R", SOCK_FLOAT);
  }
}

/* ---- Engine Type Registration ----
 * Blender's render engine registry reads this struct to populate the Engine selector.
 * The idname "BLENDER_EEVEE_GAME" must also be registered in rna_scene.cc.
 *
 * IMPORTANT: Call Engine::init_static() from draw_manager.cc immediately after
 * DRW_engines_register() for this engine type. This acquires the eevee::ShaderModule
 * singleton reference required for material shader compilation.
 * Example in draw_context.cc:
 *
 *   RE_engines_register(&DRW_engine_viewport_eevee_type);
 *   RE_engines_register(&DRW_engine_viewport_eevee_game_type);
 *   blender::eevee_game::Engine::init_static();  // ← TODO: add this line
 */
RenderEngineType DRW_engine_viewport_eevee_game_type = {
    /* next */                nullptr,
    /* prev */                nullptr,
    /* idname */              "BLENDER_EEVEE_GAME",
    /* name */                N_("EEVEE Game"),
    /* flag */                RE_INTERNAL | RE_USE_GPU_CONTEXT | RE_USE_STEREO_VIEWPORT,
    /* update */              nullptr,
    /* render */              &eevee_game_render,
    /* render_frame_finish */ nullptr,
    /* draw */                nullptr,
    /* bake */                nullptr,
    /* view_update */         nullptr,
    /* view_draw */           nullptr,
    /* update_script_node */  nullptr,
    /* update_render_passes */ &eevee_game_render_update_passes,
    /* update_custom_camera */ nullptr,
    /* draw_engine */         nullptr,
    /* rna_ext */             {nullptr, nullptr, nullptr},
};

} // namespace blender::eevee_game
