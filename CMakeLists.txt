cmake_minimum_required(VERSION 3.16)
project(vgmmm VERSION 1.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(VGMMM_BUILD_GUI "Build the Qt editor" ON)

find_package(ZLIB REQUIRED)

# ---- chip emulators and VGM optimiser shipped with the original program (misc/*_src)
add_library(vgmmm_chips STATIC
  third_party/ym2612/fm2612.cpp
  third_party/segapsg/sn76496.cpp
  third_party/vgmcmp/vgm_cmp.cpp
  third_party/vgmcmp/chip_cmp.cpp)
target_include_directories(vgmmm_chips PRIVATE third_party/shim)
# the sources expect MSVC; the shim header is force-included so its macros are seen first
target_compile_options(vgmmm_chips PRIVATE -w -fpermissive -std=gnu++98
  -include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shim/windows.h)

# ---- editor core (song data, player, import/export); no GUI dependencies
add_library(vgmmm_core STATIC
  src/core/pack.cpp
  src/core/song.cpp
  src/core/instrument.cpp
  src/core/sample.cpp
  src/core/player.cpp
  src/core/chips.cpp
  src/core/vgmexport.cpp
  src/core/import.cpp
  src/core/edit.cpp
  src/core/vgmplay.cpp)
target_include_directories(vgmmm_core PUBLIC src/core)
target_compile_options(vgmmm_core PRIVATE -Wall -Wextra)
target_link_libraries(vgmmm_core PUBLIC vgmmm_chips ZLIB::ZLIB)

add_executable(vgmmm-cli tools/vgmmm-cli.cpp)
target_link_libraries(vgmmm-cli PRIVATE vgmmm_core)

# ---- Qt editor
if(VGMMM_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(ALSA IMPORTED_TARGET alsa)
  set(CMAKE_AUTORCC ON)

  # Widget construction code is generated from the original Borland forms (tools/dfm2qt.py);
  # the generated files are checked in, run the script again after changing it.
  add_executable(vgmmm
    src/ui/main.cpp
    src/ui/mainwindow.cpp
    src/ui/ptn_view.cpp
    src/ui/ptn_input.cpp
    src/ui/ins_view.cpp
    src/ui/edit_actions.cpp
    src/ui/app_input.cpp
    src/ui/dialogs.cpp
    src/ui/widgets.cpp
    src/ui/keys.cpp
    src/ui/audio.cpp
    src/ui/midi.cpp
    src/ui/gen/ui_forms.cpp
    res/resources.qrc)
  target_include_directories(vgmmm PRIVATE src/ui)
  target_compile_options(vgmmm PRIVATE -Wall -Wextra)
  target_link_libraries(vgmmm PRIVATE vgmmm_core Qt6::Widgets PkgConfig::SDL2)
  if(ALSA_FOUND)
    target_compile_definitions(vgmmm PRIVATE VGMMM_HAVE_ALSA)
    target_link_libraries(vgmmm PRIVATE PkgConfig::ALSA)
  endif()
endif()
