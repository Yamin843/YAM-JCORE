# ═══════════════════════════════════════════════════════════════
# YAM-JCORE — native build configuration
# ═══════════════════════════════════════════════════════════════
# Layout under jni/:
#   ├── CMakeLists.txt            (this file)
#   ├── main.cpp                  (JNI_OnLoad, JNI_OnUnload, ctor/dtor)
#   ├── core/                      bootstrap, script_host, message_router,
#   │                              thread_sync, error_recovery
#   ├── art/                       art_guard, art_spec, jvmti_bridge,
#   │                              heap_scanner, backtrace
#   ├── bridge/                    class_browser, handle_registry,
#   │                              hook_manager, call_engine, type_converter
#   ├── ui/                        renderer, main_window, theme,
#   │                              notification, tab, tab.cpp
#   ├── ui/widgets/                tree_view, hex_viewer, input_dialog
#   ├── ui/tabs/                   class_browser_tab, hook_editor_tab,
#   │                              call_panel_tab, settings_tab, register_all
#   ├── native/                    runtime, dex_loader, native_methods,
#   │                              wiring
#   ├── util/                      base64, logger, event_bus
#   ├── Generated/                 prelude_js.h, bootstrap_js.h,
#   │                              embedded_dex.h
#   └── all_libs/
#       ├── imgui-master/          ImGui core + OpenGL ES 3 backend
#       ├── LIBYAMJS/              libyamjs.a + include/YAMJS.h
#       └── json/                  nlohmann::json
# ═══════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.22.1)
project(yamgg LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Paths ────────────────────────────────────────────────────
set(JNI_DIR         "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROJECT_DIR     "${JNI_DIR}/../../..")
set(ALL_LIBS_DIR    "${JNI_DIR}/all_libs")
set(IMGUI_DIR       "${ALL_LIBS_DIR}/imgui-master")
set(LIBYAMJS_DIR    "${ALL_LIBS_DIR}/LIBYAMJS")
set(JSON_DIR        "${ALL_LIBS_DIR}/json")
set(GENERATED_DIR   "${JNI_DIR}/Generated")
set(SCRIPTS_DIR     "${PROJECT_DIR}/scripts")

# ─── Build-system check ───────────────────────────────────────
# This project is built directly with CMake + the NDK toolchain
# file. Gradle and the Android Gradle Plugin are NOT used. We require
# the STL to be c++_shared because libyamjs.a bundles a c++_shared
# binary that must match ours at link time.
if(NOT DEFINED ANDROID_STL OR NOT ANDROID_STL STREQUAL "c++_shared")
    message(WARNING
        "ANDROID_STL is not 'c++_shared'. "
        "Reconfigure with: -DANDROID_STL=c++_shared")
endif()

if(NOT DEFINED ANDROID_ABI)
    message(WARNING
        "ANDROID_ABI is not defined. "
        "Reconfigure with: -DANDROID_ABI=arm64-v8a")
endif()

# ─── imgui sources ────────────────────────────────────────────
# Core + OpenGL ES 3 backend. Desktop backends are excluded because
# they target GLFW/SDL/etc. that are not available on Android.
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

# ─── Project sources ──────────────────────────────────────────
# The GLOB is recursive and CONFIGURE_DEPENDS re-runs CMake's file
# scan whenever a source file is added or removed. Adding a .cpp
# under any of these directories is enough to include it.
file(GLOB_RECURSE YAMGG_SOURCES CONFIGURE_DEPENDS
    "${JNI_DIR}/main.cpp"
    "${JNI_DIR}/core/*.cpp"
    "${JNI_DIR}/art/*.cpp"
    "${JNI_DIR}/bridge/*.cpp"
    "${JNI_DIR}/ui/*.cpp"
    "${JNI_DIR}/native/*.cpp"
    "${JNI_DIR}/util/*.cpp"
)

# ─── Build target ─────────────────────────────────────────────
add_library(yamgg SHARED
    ${YAMGG_SOURCES}
    ${IMGUI_SOURCES}
)

# ─── Include directories ──────────────────────────────────────
target_include_directories(yamgg PRIVATE
    ${JNI_DIR}
    ${GENERATED_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${LIBYAMJS_DIR}/include
    ${JSON_DIR}
)

# ─── Preprocessor definitions ─────────────────────────────────
# IMGUI_IMPL_OPENGL_ES3 forces the Android/GLES3 code path inside
# imgui_impl_opengl3.cpp. Without it, that file tries to include
# desktop GL headers that do not exist in the NDK.
target_compile_definitions(yamgg PRIVATE
    IMGUI_IMPL_OPENGL_ES3=1
    GLM_FORCE_CTOR_INIT=1
    NOMINMAX=1
    __STDC_FORMAT_MACROS=1
)

# ─── C++ compile flags ────────────────────────────────────────
# -fexceptions / -frtti are mandatory for V8 (inside libyamjs.a).
# -fvisibility=hidden limits exports to JNIEXPORT-tagged symbols;
# the JNI_OnLoad / JNI_OnUnload entry points are annotated with
# JNIEXPORT and remain visible.
target_compile_options(yamgg PRIVATE
    -fexceptions
    -frtti
    -fvisibility=hidden
    -fvisibility-inlines-hidden

    # Warnings: enable the meaningful set, suppress noise that comes
    # from third-party headers (ImGui, nlohmann, YAMJS.h).
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-invalid-offsetof
    -Wno-deprecated-declarations
    -Wno-unused-variable
    -Wno-unused-function

    # Optimisations: -O2 for release builds. Debug builds inherit the
    # default -O0 from the toolchain.
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g>
)

# ─── System libraries ─────────────────────────────────────────
find_library(LOG_LIB       log)
find_library(ANDROID_LIB   android)
find_library(EGL_LIB       EGL)
find_library(GLES_LIB      GLESv3)
find_library(DL_LIB        dl)
find_library(Z_LIB         z)
find_library(UNWIND_LIB    unwind)
find_library(ATOMIC_LIB    atomic)

target_link_libraries(yamgg PRIVATE
    ${LOG_LIB}
    ${ANDROID_LIB}
    ${EGL_LIB}
    ${GLES_LIB}
    ${DL_LIB}
    ${Z_LIB}
    ${UNWIND_LIB}
    ${ATOMIC_LIB}
    c++_shared
)

# ─── LIBYAMJS static archive ──────────────────────────────────
# --whole-archive is mandatory: without it the linker drops the
# constructor section inside libyamjs.a (frida_libc_shim_init),
# which registers gum_on_thread_realize and installs the thread
# cloak. Skipping it causes JNI recursion crashes on the first
# Java hook.
target_link_libraries(yamgg PRIVATE
    -Wl,--whole-archive
    "${LIBYAMJS_DIR}/libyamjs.a"
    -Wl,--no-whole-archive
)

# ─── Linker options ──────────────────────────────────────────
target_link_options(yamgg PRIVATE
    -Wl,--no-as-needed
    -Wl,-z,max-page-size=16384
    -Wl,--exclude-libs,ALL
)

# ─── DEX build integration ───────────────────────────────────
# embedded_dex.h is generated by scripts/build_dex.sh. The script is
# invoked by the Gradle 'buildDex' task in app/build.gradle, which has
# its own error handling: if the Android SDK is missing, the task logs
# a warning and the build continues using the existing header.
#
# We deliberately do not wire the script into CMake as an ALL custom
# target because:
#
#   1. It would run twice (once via Gradle, once via CMake), which is
#      redundant and slow.
#
#   2. CMake aborts the build when a custom command fails, but we want
#      the build to succeed with a placeholder header so that the rest
#      of the native library compiles even without an SDK.
#
# To regenerate the header manually:
#       ./scripts/build_dex.sh
#
# To force a rebuild after editing Java sources:
#       cmake --build . --target clean_build_dex && \
#           ./scripts/build_dex.sh
#
# The Gradle task (buildDex) handles this automatically in Android
# builds, which is the primary path.

# ─── Debug convenience targets ───────────────────────────────
# `cmake --build . --target clean_build_dex` removes the generated
# header so the next build regenerates it from scratch.
add_custom_target(clean_build_dex
    COMMAND ${CMAKE_COMMAND} -E remove
        "${GENERATED_DIR}/embedded_dex.h"
    COMMENT "Removing Generated/embedded_dex.h"
)
