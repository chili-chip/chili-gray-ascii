cmake_minimum_required(VERSION 3.15...3.31)

project(chili-gray-ascii LANGUAGES C CXX ASM)

# story/*.csv -> texts_gen.hpp/.cpp (all words of the game, PL and EN)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
add_custom_command(
  OUTPUT ${GEN_DIR}/texts_gen.hpp ${GEN_DIR}/texts_gen.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_texts.py ${CMAKE_CURRENT_SOURCE_DIR}/story ${GEN_DIR}
  DEPENDS tools/gen_texts.py story/teksty.csv story/tropy.csv
  COMMENT "Generating texts from story/*.csv")
include_directories(${GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set(GAME_SOURCE story.cpp world.cpp ui.cpp audio.cpp font.cpp ${GEN_DIR}/texts_gen.cpp)
set(PROJECT_SOURCE game.cpp ${GAME_SOURCE} szary.hpp font.hpp wind.hpp ${GEN_DIR}/texts_gen.hpp)
set(PROJECT_DISTRIBS LICENSE README.md)

if(MSVC)
  add_compile_options("/W4" "/wd4244" "/wd4324" "/wd4458" "/wd4100" "/utf-8")
else()
  add_compile_options("-Wall" "-Wextra" "-Wdouble-promotion" "-Wno-unused-parameter")
endif()

find_package (32BLIT CONFIG REQUIRED PATHS ../32blit-sdk $ENV{PATH_32BLIT_SDK})

blit_executable (${PROJECT_NAME} ${PROJECT_SOURCE})

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
else()
  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")
endif()

blit_metadata (${PROJECT_NAME} metadata.yml)
add_custom_target (flash DEPENDS ${PROJECT_NAME}.flash)

# Headless self-test and clip recorder (see README). Desktop only.
option(GRAY_SELFTEST "Build gray-ascii-selftest, the headless test and clip recorder" OFF)
if(GRAY_SELFTEST AND NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  add_executable(gray-ascii-selftest selftest_main.cpp game.cpp ${GAME_SOURCE})
  if(NOT MSVC)
    target_compile_options(gray-ascii-selftest PRIVATE -Wno-double-promotion)
  endif()
  target_link_libraries(gray-ascii-selftest BlitEngine)
endif()

install (FILES ${PROJECT_DISTRIBS} DESTINATION .)
set (CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set (CPACK_GENERATOR "ZIP" "TGZ")
include (CPack)
