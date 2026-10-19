cmake_minimum_required(VERSION 3.16)
project(vsdemake C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# "test": the PC test tool (headless only; the game is played on Xbox 360,
# built with the XDK project in xdk/). "xenon": the libxenon fallback.
set(VS_PLATFORM "test" CACHE STRING "Platform backend: test or xenon")

set(VS_CORE_SOURCES
  src/core/vs_pack.c
  src/core/vs_rng.c
  src/core/vs_text.c
  src/core/vs_audio.c
  src/core/vs_vorbis.c
  src/core/vs_image.c
  src/game/game.c
  src/game/timer.c
  src/game/player.c
  src/game/enemy.c
  src/game/stage.c
  src/game/render.c
  src/game/weapon.c
  src/game/pickup.c
  src/game/levelup.c
  src/game/tilemap.c
  src/game/fx.c
  src/game/save.c
  src/game/options.c
  src/game/treasure.c
  src/game/shop.c
  src/game/collection.c
  src/main.c)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

if(VS_PLATFORM STREQUAL "test")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)   # software renderer only
  add_executable(vsdemake-test ${VS_CORE_SOURCES} src/platform/test/vs_platform_test.c)
  target_include_directories(vsdemake-test PRIVATE src)
  target_link_libraries(vsdemake-test PRIVATE PkgConfig::SDL2 m)
elseif(VS_PLATFORM STREQUAL "xenon")
  # Phase 6: built with the free60 toolchain (xenon-gcc + libxenon). The
  # platform file is a stub until then; see notes/toolchain.md.
  add_executable(vsdemake-xenon ${VS_CORE_SOURCES} src/platform/xenon/vs_platform_xenon.c)
  target_include_directories(vsdemake-xenon PRIVATE src)
  target_compile_definitions(vsdemake-xenon PRIVATE VS_BIG_ENDIAN=1)
else()
  message(FATAL_ERROR "VS_PLATFORM must be test or xenon")
endif()

enable_testing()
add_executable(test_pack test/test_pack.c src/core/vs_pack.c)
target_include_directories(test_pack PRIVATE src)
add_test(NAME pack_reader COMMAND test_pack ${CMAKE_SOURCE_DIR}/../work/vsdata.vspack)
# test_pack exits 77 when no pack has been built yet
set_tests_properties(pack_reader PROPERTIES SKIP_RETURN_CODE 77)

# third-party decoder (stb_vorbis via vs_vorbis.c): built as is, without our warnings
set_source_files_properties(src/core/vs_vorbis.c PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(src/core/vs_image.c PROPERTIES COMPILE_OPTIONS "-w")
