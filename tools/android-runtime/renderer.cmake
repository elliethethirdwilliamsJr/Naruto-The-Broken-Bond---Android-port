# Explicit development target. The desktop renderer and its cache stay separate.
get_filename_component(BOND_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(BOND_THIRDPARTY "${BOND_ROOT}/../naruto-rise-recomp/tools/xenosrecomp/thirdparty")
set(BROKENBOND_NATIVE_D3D12 OFF)
set(BROKENBOND_NATIVE_VULKAN ON)
set(NATIVE_RENDERER_PLUME_SOURCE "${BOND_ROOT}/build/deps/plume" CACHE PATH "Pinned clean Plume checkout")
include("${BOND_ROOT}/tools/native-renderer/plume-engine.cmake")
target_link_libraries(native_renderer_plume_vulkan PUBLIC android dl)

set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${BOND_THIRDPARTY}/zstd/build/cmake" zstd-renderer EXCLUDE_FROM_ALL)
add_library(android_renderer_smolv STATIC "${BOND_THIRDPARTY}/smol-v/source/smolv.cpp")
target_include_directories(android_renderer_smolv PUBLIC "${BOND_THIRDPARTY}/smol-v/source")

add_library(android_renderer_incidents STATIC
    "${BOND_ROOT}/src/diagnostics/incident.cpp"
    "${BOND_ROOT}/src/diagnostics/supervisor.cpp"
    "${BOND_ROOT}/src/diagnostics/export.cpp"
    "${BOND_ROOT}/src/data_paths.cpp")
target_include_directories(android_renderer_incidents PUBLIC "${BOND_ROOT}/src"
    PRIVATE "${BOND_ROOT}/third_party/nlohmann_json/include")
target_compile_features(android_renderer_incidents PUBLIC cxx_std_23)

# This Android Clang predates #embed. Include the exact audited container in
# read-only ELF data without expanding fourteen megabytes into C++ tokens.
enable_language(ASM)
set(BOND_MOBILE_CACHE "${BOND_ROOT}/build/android-renderer-cache/shader_cache_spirv.bin")
if(NOT EXISTS "${BOND_MOBILE_CACHE}")
    message(FATAL_ERROR "Run tools/android-runtime/pack_renderer_cache.py before building the renderer")
endif()
file(SIZE "${BOND_MOBILE_CACHE}" BOND_MOBILE_CACHE_SIZE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BOND_MOBILE_CACHE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mobile_shader_cache.S"
    ".section .rodata\n.balign 16\n.global _ZN3gpu17kShaderCacheSpirvE\n.type _ZN3gpu17kShaderCacheSpirvE, %object\n_ZN3gpu17kShaderCacheSpirvE:\n.incbin \"${BOND_MOBILE_CACHE}\"\n.size _ZN3gpu17kShaderCacheSpirvE, .-_ZN3gpu17kShaderCacheSpirvE\n.section .note.GNU-stack,\"\",%progbits\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mobile_shader_cache.cpp"
    "#include <cstddef>\nnamespace gpu {\nextern const char kShaderCache[] = {0};\nextern const size_t kShaderCacheSize = 0;\nextern const size_t kShaderCacheSpirvSize = ${BOND_MOBILE_CACHE_SIZE};\n}\n")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/mobile_shader_cache.S" PROPERTIES OBJECT_DEPENDS "${BOND_MOBILE_CACHE}")

add_library(android_renderer_units STATIC
    "${BOND_ROOT}/src/gpu/backend/engine.cpp"
    "${BOND_ROOT}/src/gpu/backend/vulkan/backend.cpp"
    "${BOND_ROOT}/src/gpu/backend/vulkan/completion.cpp"
    "${BOND_ROOT}/src/gpu/backend/vulkan/shader_contract.cpp"
    "${BOND_ROOT}/src/gpu/backend/failure_channel.cpp"
    "${BOND_ROOT}/src/gpu/backend/pipeline_keys.cpp"
    "${BOND_ROOT}/src/gpu/backend/pipeline_set.cpp"
    "${BOND_ROOT}/src/gpu/backend/dispatcher.cpp"
    "${BOND_ROOT}/src/gpu/shaders/package.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/mobile_shader_cache.S"
    "${CMAKE_CURRENT_BINARY_DIR}/mobile_shader_cache.cpp"
    "${BOND_ROOT}/src/gpu/textures/formats.cpp"
    "${BOND_ROOT}/src/gpu/common/platform_posix.cpp")
target_include_directories(android_renderer_units PUBLIC "${BOND_ROOT}/src")
target_compile_features(android_renderer_units PUBLIC cxx_std_23)
target_compile_definitions(android_renderer_units PUBLIC BROKENBOND_NATIVE_D3D12_DISABLED=1
    BROKENBOND_NATIVE_VULKAN=1 BROKENBOND_SHADER_CACHE_SPIRV=1)
target_link_libraries(android_renderer_units PUBLIC native_renderer_plume_vulkan
    android_renderer_smolv libzstd_static fmt::fmt rex::runtime android_renderer_incidents)

set(BOND_DRAW_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/renderer-probe-shaders")
file(MAKE_DIRECTORY "${BOND_DRAW_HEADERS}")
foreach(stage vs ps)
    if(stage STREQUAL "vs")
        set(invert -fvk-invert-y)
        set(symbol kCacheVs)
    else()
        set(invert)
        set(symbol kCachePs)
    endif()
    add_custom_command(OUTPUT "${BOND_DRAW_HEADERS}/draw_cache_${stage}.h"
        COMMAND "${BOND_ANDROID_DXC}" -T ${stage}_6_0 -E ${stage}_main -HV 2021 -spirv
            -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -fvk-stage-io-order=decl
            -fspv-entrypoint-name=main ${invert} -Vn ${symbol}
            -Fh "${BOND_DRAW_HEADERS}/draw_cache_${stage}.h"
            "${CMAKE_CURRENT_LIST_DIR}/renderer_probe.hlsl"
        DEPENDS renderer_probe.hlsl VERBATIM)
endforeach()
add_executable(android_renderer_probe renderer_probe.cpp
    "${BOND_DRAW_HEADERS}/draw_cache_vs.h" "${BOND_DRAW_HEADERS}/draw_cache_ps.h")
target_include_directories(android_renderer_probe PRIVATE "${BOND_DRAW_HEADERS}")
target_link_libraries(android_renderer_probe PRIVATE android_renderer_units)
target_link_options(android_renderer_probe PRIVATE -Wl,-z,defs)

add_library(broken_bond_renderer_jni SHARED renderer_surface.cpp)
target_link_libraries(broken_bond_renderer_jni PRIVATE android_renderer_units android)
target_link_options(broken_bond_renderer_jni PRIVATE -Wl,-z,defs)
