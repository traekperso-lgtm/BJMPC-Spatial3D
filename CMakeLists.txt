cmake_minimum_required(VERSION 3.16)
project(Spatial3D VERSION 0.4.0 LANGUAGES CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(S3D_WITH_MYSOFA "HRTF mesurees (.sofa) via libmysofa" OFF)
option(S3D_BUILD_TESTS "Tests du moteur" ON)
# Dossier d'une build FFmpeg "shared" (include/ lib/ bin/), ex. BtbN ffmpeg-n7.1-win64-lgpl-shared
set(FFMPEG_ROOT "" CACHE PATH "Racine FFmpeg (sinon pkg-config / vcpkg)")

# Runtime C++ statique : l'exe tourne sans redistribuable Visual C++
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options(/utf-8 /W3)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---- Cœur portable (sans FFmpeg ni Windows) ----
add_library(s3dcore STATIC
  src/layout.cpp src/vbap.cpp src/sources.cpp src/renderer.cpp src/dsp.cpp src/wav_writer.cpp src/live_core.cpp)
target_include_directories(s3dcore PUBLIC src)
if(S3D_WITH_MYSOFA)
  find_package(mysofa CONFIG QUIET)
  if(TARGET mysofa::mysofa-shared)
    target_link_libraries(s3dcore PUBLIC mysofa::mysofa-shared)
  elseif(TARGET mysofa::mysofa-static)
    target_link_libraries(s3dcore PUBLIC mysofa::mysofa-static)
  else()
    find_library(MYSOFA_LIB mysofa REQUIRED)
    find_path(MYSOFA_INC mysofa.h REQUIRED)
    target_include_directories(s3dcore PUBLIC ${MYSOFA_INC})
    target_link_libraries(s3dcore PUBLIC ${MYSOFA_LIB})
  endif()
  target_compile_definitions(s3dcore PUBLIC S3D_WITH_MYSOFA)
endif()

# ---- FFmpeg ----
add_library(s3d_ffmpeg INTERFACE)
if(FFMPEG_ROOT)
  target_include_directories(s3d_ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
  foreach(lib avformat avcodec swresample avutil)
    find_library(FF_${lib} NAMES ${lib} lib${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(s3d_ffmpeg INTERFACE ${FF_${lib}})
  endforeach()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
  target_link_libraries(s3d_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

# ---- Exécutable ----
add_executable(spatial3d_cli src/main.cpp src/decoder.cpp)
target_link_libraries(spatial3d_cli PRIVATE s3dcore s3d_ffmpeg)
# Windows ne distingue pas la casse : cible « spatial3d_cli » (projet distinct de « Spatial3D »), exe spatial3d-cli.exe
set_target_properties(spatial3d_cli PROPERTIES OUTPUT_NAME spatial3d-cli)
if(WIN32)
  target_sources(spatial3d_cli PRIVATE src/wasapi_output.cpp src/live.cpp src/engine.cpp res/spatial3d.rc)
  target_link_libraries(spatial3d_cli PRIVATE advapi32)
  target_link_libraries(spatial3d_cli PRIVATE ole32 shell32 avrt)
  if(MINGW)
    target_link_options(spatial3d_cli PRIVATE -static-libgcc -static-libstdc++)
  endif()
  # Copie des DLL FFmpeg à côté de l'exe
  if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/bin")
    file(GLOB FF_DLLS "${FFMPEG_ROOT}/bin/*.dll")
    add_custom_command(TARGET spatial3d_cli POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FF_DLLS} $<TARGET_FILE_DIR:spatial3d_cli>)
  endif()
endif()

# ---- Interface graphique (Windows) ----
if(WIN32)
  set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
  add_executable(Spatial3D WIN32
    src/gui_main.cpp src/ui.cpp src/engine.cpp src/wasapi_output.cpp src/decoder.cpp res/spatial3d.rc
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(Spatial3D PRIVATE ${IMGUI_DIR})
  target_compile_definitions(Spatial3D PRIVATE UNICODE _UNICODE)
  target_link_libraries(Spatial3D PRIVATE s3dcore s3d_ffmpeg d3d11 d3dcompiler dwmapi ole32 shell32 comdlg32 advapi32 avrt)
  if(MINGW)
    target_link_options(Spatial3D PRIVATE -static-libgcc -static-libstdc++ -municode)
  endif()
  if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/bin")
    add_custom_command(TARGET Spatial3D POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FF_DLLS} $<TARGET_FILE_DIR:Spatial3D>)
  endif()
endif()

# ---- Tests ----
if(S3D_BUILD_TESTS)
  enable_testing()
  add_executable(s3d_tests tests/test_core.cpp)
  target_link_libraries(s3d_tests PRIVATE s3dcore)
  add_test(NAME moteur COMMAND s3d_tests)
endif()
