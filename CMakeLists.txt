cmake_minimum_required(VERSION 3.20)
project(BAKTweaks LANGUAGES CXX RC ASM_MASM)

if(NOT WIN32)
  message(FATAL_ERROR "BAK Tweaks must be built on Windows.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build x64 only.")
endif()

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")

foreach(req
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
  "${MINHOOK_DIR}/include/MinHook.h")
  if(NOT EXISTS "${req}")
    message(FATAL_ERROR "Missing dependency: ${req}. Run Fetch-Dependencies.ps1 first.")
  endif()
endforeach()

add_subdirectory("${MINHOOK_DIR}" minhook_build)

add_library(BAKTweaks SHARED
  src/BAKTweaks.cpp
  src/ExposureWriter.asm
  src/BAKTweaks.rc
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(BAKTweaks PRIVATE
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${MINHOOK_DIR}/include
  src
)

target_compile_features(BAKTweaks PRIVATE cxx_std_20)
target_compile_definitions(BAKTweaks PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BAKTweaks PRIVATE minhook d3d11 dxgi ole32 windowscodecs)
set_target_properties(BAKTweaks PROPERTIES OUTPUT_NAME "BAKTweaks")

add_executable(BAKInjector WIN32
  tools/BAKInjector.cpp
  tools/BAKInjector.rc
)
target_compile_features(BAKInjector PRIVATE cxx_std_17)
target_compile_definitions(BAKInjector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
if(MSVC)
  target_compile_options(BAKInjector PRIVATE /utf-8)
endif()
target_link_libraries(BAKInjector PRIVATE comctl32)
target_link_options(BAKInjector PRIVATE "/MANIFEST:EMBED" "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
set_target_properties(BAKInjector PROPERTIES OUTPUT_NAME "BAKInjector")

if(MSVC)
  target_compile_options(BAKTweaks PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4;/EHsc;/permissive->)
endif()
