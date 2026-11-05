# Original game frontend and hooks; isolated Android development process.
add_library(android_game_frontend OBJECT
    "${BOND_ROOT}/src/gpu/host/host.cpp"
    "${BOND_ROOT}/src/gpu/host/kernel_video.cpp"
    "${BOND_ROOT}/src/gpu/host/kernel_threading.cpp"
    "${BOND_ROOT}/src/gpu/host/kernel_modules.cpp"
    "${BOND_ROOT}/src/gpu/host/journal.cpp"
    "${BOND_ROOT}/src/gpu/host/stop.cpp"
    "${BOND_ROOT}/src/gpu/host/engines.cpp"
    "${BOND_ROOT}/src/gpu/host/graphics.cpp"
    "${BOND_ROOT}/src/gpu/host/frame_completion.cpp"
    "${BOND_ROOT}/src/gpu/host/guest_hooks.cpp"
    "${BOND_ROOT}/src/gpu/host/exports.cpp"
    "${BOND_ROOT}/src/gpu/host/title_loading.cpp"
    "${BOND_ROOT}/src/gpu/host/aspect.cpp"
    "${BOND_ROOT}/src/gpu/d3d/hooks.cpp"
    "${BOND_ROOT}/src/gpu/d3d/device.cpp"
    "${BOND_ROOT}/src/gpu/d3d/resources.cpp"
    "${BOND_ROOT}/src/gpu/d3d/pass.cpp"
    "${BOND_ROOT}/src/gpu/d3d/draw.cpp"
    "${BOND_ROOT}/src/gpu/d3d/state.cpp"
    "${BOND_ROOT}/src/gpu/d3d/screen_layer.cpp"
    "${BOND_ROOT}/src/gpu/d3d/present.cpp"
    "${BOND_ROOT}/src/gpu/d3d/frame.cpp"
    "${BOND_ROOT}/src/gpu/d3d/close.cpp"
    "${BOND_ROOT}/src/gpu/d3d/query.cpp"
    "${BOND_ROOT}/src/gpu/shaders/shaders.cpp"
    "${BOND_ROOT}/src/gpu/shaders/pipelines.cpp"
    "${BOND_ROOT}/src/gpu/shaders/player_pipelines.cpp"
    "${BOND_ROOT}/src/gpu/textures/textures.cpp"
    "${BOND_ROOT}/src/gpu/textures/write_watch.cpp"
    "${BOND_ROOT}/src/gpu/textures/layout.cpp"
    "${BOND_ROOT}/src/gpu/buffers/buffers.cpp"
    "${BOND_ROOT}/src/gpu/buffers/vertex_layout.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/frame_state.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/census.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/frame_trace.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/start_evidence.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/frame_capture.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/measure.cpp"
    "${BOND_ROOT}/src/gpu/ui/layer.cpp"
    "${BOND_ROOT}/src/gpu/ui/immediate_drawer.cpp"
    "${BOND_ROOT}/src/gpu/ui/screen.cpp"
    "${BOND_ROOT}/src/gpu/interpolation/tick.cpp"
    "${BOND_ROOT}/src/gpu/interpolation/transforms.cpp"
    "${BOND_ROOT}/src/gpu/settings.cpp"
    "${BOND_ROOT}/src/studies/rand_trace.cpp"
    "${BOND_ROOT}/src/studies/audio_trace.cpp"
    "${BOND_ROOT}/src/studies/xma_stream_trace.cpp"
    "${BOND_ROOT}/src/studies/export_trace.cpp"
    "${BOND_ROOT}/src/studies/kernel_io_exports.cpp"
    "${BOND_ROOT}/src/studies/kernel_xma_exports.cpp"
    "${BOND_ROOT}/src/gpu/diagnostics/inert_posix.cpp"
    "${BOND_ROOT}/src/intro_skip.cpp"
    "${BOND_ROOT}/src/game_image.cpp"
)
target_include_directories(android_game_frontend PRIVATE "${BOND_ROOT}" "${BOND_ROOT}/sdk/src"
    "${BOND_ROOT}/third_party/nlohmann_json/include")
target_compile_definitions(android_game_frontend PRIVATE BROKENBOND_NATIVE_RENDERER_BOOTSTRAP=1)
target_compile_options(android_game_frontend PRIVATE -g0 -fno-strict-aliasing)
set_source_files_properties("${BOND_ROOT}/src/game_image.cpp" PROPERTIES
    INCLUDE_DIRECTORIES "${BOND_ROOT}/sdk/thirdparty/xxHash")
target_link_libraries(android_game_frontend PUBLIC android_renderer_units)
add_library(broken_bond_game SHARED game_main.cpp touch_input.cpp $<TARGET_OBJECTS:android_game_frontend>)
target_sources(broken_bond_game PRIVATE "${BOND_ROOT}/sdk/src/ui/rex_app.cpp"
    "${BOND_ROOT}/src/sentry_reporting.cpp" "${BOND_ROOT}/src/port_version.cpp"
    "${BOND_ROOT}/src/crash_report.cpp")
target_include_directories(broken_bond_game PRIVATE "${BOND_ROOT}" "${BOND_GENERATED_DIR}"
    "${BOND_ROOT}/sdk/thirdparty/imgui")
target_compile_options(broken_bond_game PRIVATE -g0)
target_compile_definitions(broken_bond_game PRIVATE REXGLUE_BUILD_CONFIG="$<CONFIG>")
target_link_libraries(broken_bond_game PRIVATE android_renderer_units broken_bond_guest)
target_link_options(broken_bond_game PRIVATE -Wl,-z,defs -Wl,--error-limit=0)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/solid_pixel.h"
    COMMAND "${BOND_ANDROID_DXC}" -T ps_6_0 -E main -spirv -fspv-target-env=vulkan1.2
        -Vn kSolidPs -Fh "${CMAKE_CURRENT_BINARY_DIR}/solid_pixel.h"
        "${CMAKE_CURRENT_LIST_DIR}/solid_pixel.hlsl" DEPENDS solid_pixel.hlsl VERBATIM)
add_executable(android_original_shader_probe original_shader_probe.cpp "${CMAKE_CURRENT_BINARY_DIR}/solid_pixel.h")
target_include_directories(android_original_shader_probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(android_original_shader_probe PRIVATE android_renderer_units)

add_executable(android_thread_shutdown_probe thread_shutdown_probe.cpp
    "${BOND_ROOT}/src/gpu/diagnostics/inert_posix.cpp")
target_link_libraries(android_thread_shutdown_probe PRIVATE android_renderer_units)

add_executable(android_touch_input_probe touch_input_probe.cpp touch_input.cpp)
target_include_directories(android_touch_input_probe PRIVATE "${BOND_ROOT}/src")
target_link_libraries(android_touch_input_probe PRIVATE rex::runtime)
target_compile_features(android_touch_input_probe PRIVATE cxx_std_23)
