# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ***** END GPL LICENSE BLOCK *****

# ---------------------------------------------------------------------------
# FSR 3.1 from FidelityFX AMD's SDK
# ---------------------------------------------------------------------------
set(_FFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_FFX_SDK  "${_FFX_ROOT}/sdk")
set(_FFX_API  "${_FFX_ROOT}/ffx-api")

set(_FFX_REQUIRED_FILES
  "${_FFX_SDK}/src/backends/vk/ffx_vk.cpp"
  "${_FFX_SDK}/src/backends/shared/ffx_shader_blobs.cpp"
  "${_FFX_SDK}/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp"
  "${_FFX_SDK}/src/components/fsr3/ffx_fsr3.cpp"
  "${_FFX_SDK}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp"
  "${_FFX_SDK}/src/components/spd/ffx_spd.cpp"
  "${_FFX_SDK}/src/shared/ffx_assert.cpp"
  "${_FFX_SDK}/src/shared/ffx_message.cpp"
  "${_FFX_SDK}/src/shared/ffx_object_management.cpp"
  "${_FFX_API}/src/ffx_api.cpp"
  "${_FFX_API}/src/ffx_provider.cpp"
  "${_FFX_API}/src/ffx_provider_fsr3upscale.cpp"
  "${_FFX_API}/src/backends.cpp"
  "${_FFX_API}/src/validation.cpp"
  "${_FFX_API}/include/ffx_api/ffx_upscale.h"
  "${_FFX_API}/include/ffx_api/vk/ffx_api_vk.h"
  "${_FFX_SDK}/include/FidelityFX/host/backends/vk/ffx_vk.h"
  "${_FFX_ROOT}/ffx_shader_blobs_stub.cpp"
)

foreach(_f ${_FFX_REQUIRED_FILES})
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR
      "[extern/fidelityfx] Missing file:\n  ${_f}\n"
      "See extern/fidelityfx/README.md for the required file list.")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# INC — public include paths (available to all targets that link this lib)
# ---------------------------------------------------------------------------
set(INC
  # eevee_game includes:
  #   #include <ffx_api/ffx_upscale.h>
  #   #include <ffx_api/vk/ffx_api_vk.h>
  ffx-api/include

  # SDK host-side types: FfxInterface, FfxFsr3UpscalerContext, etc.
  sdk/include
)

# ---------------------------------------------------------------------------
# INC_SYS — system include paths (warnings suppressed, AMD's headers)
# ---------------------------------------------------------------------------
set(INC_SYS
  # GPU shader headers included by shaderblobs.cpp
  sdk/include/FidelityFX/gpu

  # Internal SDK src-relative includes
  sdk/src/backends/shared
  sdk/src/backends/shared/blob_accessors
  sdk/src/backends/vk
  sdk/src/components
  sdk/src/components/fsr3
  sdk/src/components/fsr3upscaler
  sdk/src/components/spd
  sdk/src/shared
  ffx-api/src
)

# ---------------------------------------------------------------------------
# SRC — all compiled sources + headers for IDE visibility
# ---------------------------------------------------------------------------
set(SRC
  # Stub for uncompiled effect blob accessors — satisfies linker references
  # in ffx_shader_blobs.cpp without pulling in unused effect code
  ffx_shader_blobs_stub.cpp

  # Vulkan backend — VkDevice, descriptors, pipelines, barriers (233KB)
  sdk/src/backends/vk/ffx_vk.cpp

  # Shader blob router + FSR3 upscaler SPIR-V permutation table.
  # Blobs are pre-compiled — no shader compilation at build or runtime.
  sdk/src/backends/shared/ffx_shader_blobs.cpp
  sdk/src/backends/shared/ffx_shader_blobs.h
  sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
  sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.h

  # FSR3 context manager (wraps upscaler, frame gen excluded at link time)
  sdk/src/components/fsr3/ffx_fsr3.cpp
  sdk/src/components/fsr3/ffx_fsr3_private.h

  # FSR3 upscaler algorithm — 10 temporal compute passes
  sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
  sdk/src/components/fsr3upscaler/ffx_fsr3upscaler_private.h

  # SPD — single-pass luma mip chain, required by luma pyramid pass
  sdk/src/components/spd/ffx_spd.cpp
  sdk/src/components/spd/ffx_spd_private.h

  # Shared utilities
  sdk/src/shared/ffx_assert.cpp
  sdk/src/shared/ffx_message.cpp
  sdk/src/shared/ffx_object_management.cpp
  sdk/src/shared/ffx_object_management.h

  # ffx-api unified layer — ffxCreateContext / ffxDispatch surface
  ffx-api/src/ffx_api.cpp
  ffx-api/src/ffx_provider.cpp
  ffx-api/src/ffx_provider.h
  ffx-api/src/ffx_provider_fsr3upscale.cpp
  ffx-api/src/ffx_provider_fsr3upscale.h
  ffx-api/src/ffx_provider_external.h
  ffx-api/src/backends.cpp
  ffx-api/src/backends.h
  ffx-api/src/validation.cpp
  ffx-api/src/validation.h

  # Public API headers (for IDE and dependency tracking)
  ffx-api/include/ffx_api/ffx_api.h
  ffx-api/include/ffx_api/ffx_api.hpp
  ffx-api/include/ffx_api/ffx_api_types.h
  ffx-api/include/ffx_api/ffx_api_loader.h
  ffx-api/include/ffx_api/ffx_upscale.h
  ffx-api/include/ffx_api/ffx_upscale.hpp
  ffx-api/include/ffx_api/vk/ffx_api_vk.h
  ffx-api/include/ffx_api/vk/ffx_api_vk.hpp
  sdk/include/FidelityFX/host/ffx_fsr3upscaler.h
  sdk/include/FidelityFX/host/ffx_fsr3.h
  sdk/include/FidelityFX/host/ffx_spd.h
  sdk/include/FidelityFX/host/ffx_types.h
  sdk/include/FidelityFX/host/ffx_interface.h
  sdk/include/FidelityFX/host/ffx_error.h
  sdk/include/FidelityFX/host/ffx_assert.h
  sdk/include/FidelityFX/host/ffx_message.h
  sdk/include/FidelityFX/host/ffx_util.h
  sdk/include/FidelityFX/host/ffx_fx.h
  sdk/include/FidelityFX/host/backends/vk/ffx_vk.h
)

# ---------------------------------------------------------------------------
# LIB — dependencies
# bf::dependencies::optional::vulkan carries VULKAN_INCLUDE_DIRS +
# VULKAN_LIBRARIES, already set by platform_win32/unix/apple.cmake.
# ---------------------------------------------------------------------------
set(LIB
  bf::dependencies::optional::vulkan
)

# ---------------------------------------------------------------------------
# Compile definitions required by the SDK
# FFX_GCC     — non-MSVC path in ffx_types.h (__attribute__ vs __declspec)
# FFX_BACKEND_VK — tells backends.cpp only VK provider is compiled in
# FFX_FSR3UPSCALER — enables FSR3 upscaler dispatch in ffx_shader_blobs.cpp
# ---------------------------------------------------------------------------
add_definitions(
  -DFFX_GCC
  -DFFX_BACKEND_VK=1
  -DFFX_FSR3UPSCALER=1
)

blender_add_lib(extern_fidelityfx_fsr "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
