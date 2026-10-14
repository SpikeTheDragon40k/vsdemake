cmake_minimum_required(VERSION 3.16)
project(vsdemake C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(VS_PLATFORM "sdl2" CACHE STRING "Platform backend: sdl2 or xenon")

set(VS_CORE_SOURCES
  src/core/vs_pack.c
  src/core/vs_rng.c
  src/core/vs_text.c
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
  src/main.c)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

if(VS_PLATFORM STREQUAL "sdl2")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  add_executable(vsdemake-sdl2 ${VS_CORE_SOURCES} src/platform/sdl2/vs_platform_sdl2.c)
  target_include_directories(vsdemake-sdl2 PRIVATE src)
  target_link_libraries(vsdemake-sdl2 PRIVATE PkgConfig::SDL2 m)
elseif(VS_PLATFORM STREQUAL "xenon")
  # Phase 6: built with the free60 toolchain (xenon-gcc + libxenon). The
  # platform file is a stub until then; see notes/toolchain.md.
  add_executable(vsdemake-xenon ${VS_CORE_SOURCES} src/platform/xenon/vs_platform_xenon.c)
  target_include_directories(vsdemake-xenon PRIVATE src)
  target_compile_definitions(vsdemake-xenon PRIVATE VS_BIG_ENDIAN=1)
else()
  message(FATAL_ERROR "VS_PLATFORM must be sdl2 or xenon")
endif()

enable_testing()
add_executable(test_pack test/test_pack.c src/core/vs_pack.c)
target_include_directories(test_pack PRIVATE src)
add_test(NAME pack_reader COMMAND test_pack ${CMAKE_SOURCE_DIR}/../work/vsdata.vspack)
# test_pack exits 77 when no pack has been built yet
set_tests_properties(pack_reader PROPERTIES SKIP_RETURN_CODE 77)
