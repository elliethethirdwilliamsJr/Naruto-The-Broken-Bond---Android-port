cmake_minimum_required(VERSION 3.25)
project(BrokenBondAndroidRuntime LANGUAGES C CXX)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This development build requires the Android NDK and arm64-v8a")
endif()

# The application supplies its native renderer. Keep SDK windowing, input,
# audio, virtual filesystem and kernel services; omit unused SDK GPU backends.
set(REXGLUE_EXTERNAL_RENDERER ON CACHE BOOL "Host-provided renderer" FORCE)
set(REXGLUE_BUILD_XENOS_PLUGIN OFF CACHE BOOL "No GPU emulation plugin" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "No D3D12 on Android" FORCE)
set(REXGLUE_USE_VULKAN OFF CACHE BOOL "The game owns its Vulkan backend" FORCE)
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "Runtime development build" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "Limit development build memory" FORCE)
set(REXGLUE_ENABLE_PERF_COUNTERS OFF CACHE BOOL "Runtime development build" FORCE)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
set(REXGLUE_LIBUCONTEXT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../build/deps/libucontext"
    CACHE PATH "Pinned Android fiber dependency")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../sdk" sdk)


add_executable(android_fiber_probe fiber_probe.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../sdk/src/core/fiber_android.cpp")
target_include_directories(android_fiber_probe PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../sdk/include"
    "${REXGLUE_LIBUCONTEXT_SOURCE_DIR}/include"
    "${REXGLUE_LIBUCONTEXT_SOURCE_DIR}/arch/aarch64/include")
target_compile_features(android_fiber_probe PRIVATE cxx_std_23)
target_link_libraries(android_fiber_probe PRIVATE rex_android_ucontext m)

add_executable(android_runtime_probe runtime_probe.cpp)
target_link_libraries(android_runtime_probe PRIVATE rex::runtime)
target_compile_features(android_runtime_probe PRIVATE cxx_std_23)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/texture_probe.spv"
    COMMAND "${ANDROID_NDK}/shader-tools/windows-x86_64/glslc.exe"
        --target-env=vulkan1.1 -fshader-stage=compute
        "${CMAKE_CURRENT_LIST_DIR}/texture_probe.comp" -o "${CMAKE_CURRENT_BINARY_DIR}/texture_probe.spv"
    COMMAND "${ANDROID_NDK}/shader-tools/windows-x86_64/spirv-val.exe"
        --target-env vulkan1.1 "${CMAKE_CURRENT_BINARY_DIR}/texture_probe.spv"
    DEPENDS texture_probe.comp VERBATIM)
add_custom_target(android_texture_shader DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/texture_probe.spv")
add_executable(android_texture_probe texture_probe.cpp "${CMAKE_CURRENT_LIST_DIR}/../../src/gpu/textures/formats.cpp")
target_include_directories(android_texture_probe PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src")
target_compile_features(android_texture_probe PRIVATE cxx_std_23)
target_link_libraries(android_texture_probe PRIVATE vulkan rex::runtime)
# The contracts use SDK types. Unused SDK code need not enter this isolated
# Vulkan diagnostic's dynamic dependencies.
target_link_options(android_texture_probe PRIVATE -Wl,--as-needed)
add_dependencies(android_texture_probe android_texture_shader)

set(BOND_ANDROID_DXC "${CMAKE_CURRENT_LIST_DIR}/../../../naruto-rise-recomp/tools/xenosrecomp/thirdparty/dxc-bin/bin/x64/dxc.exe"
    CACHE FILEPATH "Host DXC with SPIR-V support for the clipping diagnostic")
if(EXISTS "${BOND_ANDROID_DXC}")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/descriptor_probe.spv"
        COMMAND "${BOND_ANDROID_DXC}" -T cs_6_0 -E main -HV 2021 -spirv -fspv-target-env=vulkan1.2
            -fvk-use-dx-layout -Qstrip_debug -Fo "${CMAKE_CURRENT_BINARY_DIR}/descriptor_probe.spv"
            "${CMAKE_CURRENT_LIST_DIR}/descriptor_probe.hlsl"
        COMMAND "${ANDROID_NDK}/shader-tools/windows-x86_64/spirv-val.exe" --target-env vulkan1.2
            "${CMAKE_CURRENT_BINARY_DIR}/descriptor_probe.spv"
        DEPENDS descriptor_probe.hlsl VERBATIM)
    add_custom_target(android_descriptor_shader DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/descriptor_probe.spv")
    add_executable(android_descriptor_probe descriptor_probe.cpp "${CMAKE_CURRENT_LIST_DIR}/../../src/gpu/textures/formats.cpp")
    target_include_directories(android_descriptor_probe PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src")
    target_compile_features(android_descriptor_probe PRIVATE cxx_std_23)
    target_link_libraries(android_descriptor_probe PRIVATE vulkan rex::runtime)
    target_link_options(android_descriptor_probe PRIVATE -Wl,--as-needed)
    add_dependencies(android_descriptor_probe android_descriptor_shader android_texture_shader)
    foreach(stage vert frag)
        if(stage STREQUAL "vert")
            set(clip_profile vs_6_0)
            set(clip_entry vs_main)
        else()
            set(clip_profile ps_6_0)
            set(clip_entry ps_main)
        endif()
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/clip_probe.${stage}.spv"
            COMMAND "${BOND_ANDROID_DXC}" -T ${clip_profile} -E ${clip_entry} -HV 2021 -spirv
                -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -fvk-stage-io-order=decl -Qstrip_debug
                -Fo "${CMAKE_CURRENT_BINARY_DIR}/clip_probe.${stage}.spv" "${CMAKE_CURRENT_LIST_DIR}/clip_probe.hlsl"
            COMMAND "${ANDROID_NDK}/shader-tools/windows-x86_64/spirv-val.exe" --target-env vulkan1.2
                "${CMAKE_CURRENT_BINARY_DIR}/clip_probe.${stage}.spv"
            DEPENDS clip_probe.hlsl "${CMAKE_CURRENT_LIST_DIR}/../../src/gpu/backend/clip_interface.hlsli" VERBATIM)
    endforeach()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/clip_planes_gs.spv"
        COMMAND "${BOND_ANDROID_DXC}" -T gs_6_0 -E main -HV 2021 -spirv -fspv-target-env=vulkan1.2
            -fvk-use-dx-layout -fvk-stage-io-order=decl -Qstrip_debug
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/clip_planes_gs.spv"
            "${CMAKE_CURRENT_LIST_DIR}/../../src/gpu/backend/clip_planes.hlsl"
        COMMAND "${ANDROID_NDK}/shader-tools/windows-x86_64/spirv-val.exe" --target-env vulkan1.2
            "${CMAKE_CURRENT_BINARY_DIR}/clip_planes_gs.spv"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../src/gpu/backend/clip_planes.hlsl"
            "${CMAKE_CURRENT_LIST_DIR}/../../src/gpu/backend/clip_interface.hlsli" VERBATIM)
    add_custom_target(android_clip_shaders DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/clip_probe.vert.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/clip_probe.frag.spv" "${CMAKE_CURRENT_BINARY_DIR}/clip_planes_gs.spv")
    add_executable(android_clip_probe clip_probe.cpp)
    target_compile_features(android_clip_probe PRIVATE cxx_std_23)
    target_link_libraries(android_clip_probe PRIVATE vulkan)
    add_dependencies(android_clip_probe android_clip_shaders)
endif()

# Reuse the exact original-game codegen output, without modifying generated files
# or pulling the desktop launcher/renderer into this isolated Android build.
set(BOND_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../generated/default")
if(EXISTS "${BOND_GENERATED_DIR}/sources.cmake")
    include("${BOND_GENERATED_DIR}/sources.cmake")
    add_library(broken_bond_guest SHARED ${GENERATED_SOURCES} guest_image.cpp)
    target_include_directories(broken_bond_guest PRIVATE "${BOND_GENERATED_DIR}")
    target_link_libraries(broken_bond_guest PRIVATE rex::runtime)
    target_compile_features(broken_bond_guest PRIVATE cxx_std_23)
    target_compile_options(broken_bond_guest PRIVATE
        -g0 -fno-strict-aliasing -ffp-model=strict -fno-char8_t)
    target_link_options(broken_bond_guest PRIVATE -Wl,-z,defs)
    rexglue_apply_target_settings(broken_bond_guest)

    add_executable(android_guest_probe runtime_probe.cpp guest_checks.cpp)
    target_compile_definitions(android_guest_probe PRIVATE BROKEN_BOND_GUEST_PROBE=1)
    target_link_libraries(android_guest_probe PRIVATE rex::runtime broken_bond_guest)
    target_compile_features(android_guest_probe PRIVATE cxx_std_23)
endif()

option(BROKENBOND_ANDROID_RENDERER "Build the production Vulkan backend diagnostic" OFF)
if(BROKENBOND_ANDROID_RENDERER)
    include("${CMAKE_CURRENT_LIST_DIR}/renderer.cmake")
endif()

option(BROKENBOND_ANDROID_GAME_HOST "Build the original game's Android SDL host" OFF)
if(BROKENBOND_ANDROID_GAME_HOST)
    if(NOT BROKENBOND_ANDROID_RENDERER OR NOT TARGET broken_bond_guest)
        message(FATAL_ERROR "GameHost requires the renderer and generated original-code library")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/game_host.cmake")
endif()
