/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "DRW_render.hh"
#include "RE_engine.h"

namespace blender::eevee_game {

/* Engine bridges the Blender/UPBGE Draw Manager to our eevee_game implementation.
 * It acts as a factory: Blender calls create_instance() when it needs a new
 * GameInstance, and free_static() at shutdown to release shared GPU resources. */
struct Engine : public DrawEngine::Pointer {
  /* Factory: allocates and returns a new GameInstance (owned by the Draw Manager) */
  DrawEngine *create_instance() final;

  /* Initialize static/shared resources. Called once at engine registration time.
   * Acquires a reference to eevee::ShaderModule so material compilation works
   * correctly even when EEVEE is not open (e.g. standalone game player). */
  static void init_static();

  /* Free static/shared resources (compiled shaders) when Blender closes */
  static void free_static();
};

/* The global engine type descriptor; registered with Blender's render engine system */
extern RenderEngineType DRW_engine_viewport_eevee_game_type;

} // namespace blender::eevee_game
