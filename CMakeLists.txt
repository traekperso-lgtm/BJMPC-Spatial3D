cmake_minimum_required(VERSION 3.16)
project(Spatial3D VERSION 0.6.0 LANGUAGES CXX)
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

# ---- HRTF mesurée intégrée (MIT KEMAR égalisée, voir tools/make_hrtf.py) ----
set(S3D_HRTF_BIN ${CMAKE_CURRENT_SOURCE_DIR}/res/hrtf_kemar_48k.bin)
set(S3D_HRTF_CPP ${CMAKE_CURRENT_BINARY_DIR}/hrtf_kemar_data.cpp)
add_custom_command(OUTPUT ${S3D_HRTF_CPP}
  COMMAND ${CMAKE_COMMAND} -DIN=${S3D_HRTF_BIN} -DOUT=${S3D_HRTF_CPP} -DNAME=kHrtfKemar
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${S3D_HRTF_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  COMMENT "Integration de l'HRTF KEMAR")

# ---- Cœur portable (aucune dépendance) ----
add_library(s3dcore STATIC
  src/layout.cpp src/vbap.cpp src/sources.cpp src/renderer.cpp src/dsp.cpp src/wav_writer.cpp src/wav_reader.cpp src/live_core.cpp src/convolver.cpp
  src/hrtf_builtin.cpp ${S3D_HRTF_CPP})
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

# ---- Exécutable ----
add_executable(spatial3d_cli src/main.cpp)
target_link_libraries(spatial3d_cli PRIVATE s3dcore)
# Windows ne distingue pas la casse : cible « spatial3d_cli » (projet distinct de « Spatial3D »), exe spatial3d-cli.exe
set_target_properties(spatial3d_cli PROPERTIES OUTPUT_NAME spatial3d-cli)
if(WIN32)
  target_sources(spatial3d_cli PRIVATE src/wasapi_output.cpp src/live.cpp src/engine.cpp res/spatial3d.rc)
  target_link_libraries(spatial3d_cli PRIVATE advapi32)
  target_link_libraries(spatial3d_cli PRIVATE ole32 shell32 avrt)
  if(MINGW)
    target_link_options(spatial3d_cli PRIVATE -static-libgcc -static-libstdc++)
  endif()
endif()

# ---- Interface graphique (Windows) ----
if(WIN32)
  set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
  add_executable(Spatial3D WIN32
    src/gui_main.cpp src/ui.cpp src/engine.cpp src/wasapi_output.cpp res/spatial3d.rc
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(Spatial3D PRIVATE ${IMGUI_DIR})
  target_compile_definitions(Spatial3D PRIVATE UNICODE _UNICODE)
  target_link_libraries(Spatial3D PRIVATE s3dcore d3d11 d3dcompiler dwmapi ole32 shell32 comdlg32 advapi32 avrt)
  if(MINGW)
    target_link_options(Spatial3D PRIVATE -static-libgcc -static-libstdc++ -municode)
  endif()
endif()

# ---- Tests ----
if(S3D_BUILD_TESTS)
  enable_testing()
  add_executable(s3d_tests tests/test_core.cpp)
  target_link_libraries(s3d_tests PRIVATE s3dcore)
  add_test(NAME moteur COMMAND s3d_tests)
endif()
