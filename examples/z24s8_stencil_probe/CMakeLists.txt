cmake_minimum_required(VERSION 3.20)
project(z24s8_stencil_probe VERSION 0.1.0 LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PROTOTYPE_LIBRARY "" CACHE FILEPATH "Exact previously verified experimental archive")
set(PROBE_SOURCE_ID "uncommitted-candidate" CACHE STRING "Exact commit or explicitly uncommitted candidate")
if(NOT EXISTS "${PROTOTYPE_LIBRARY}")
  message(FATAL_ERROR "An existing verified prototype library is required")
endif()
file(SHA256 "${PROTOTYPE_LIBRARY}" PROTOTYPE_LIBRARY_SHA256)
if(NOT PROTOTYPE_LIBRARY_SHA256 STREQUAL "e32bf92815b72869a4648dcd0d1262e88f7bb3804971040c67d43ea3119bce92")
  message(FATAL_ERROR "Prototype archive identity mismatch: ${PROTOTYPE_LIBRARY_SHA256}")
endif()
get_filename_component(DEKO_ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(z24s8_stencil_probe main.cpp ProbeCore.cpp TerminalResult.cpp)
set_target_properties(z24s8_stencil_probe PROPERTIES SUFFIX ".elf")
target_include_directories(z24s8_stencil_probe PRIVATE "${DEKO_ROOT}/include")
target_compile_options(z24s8_stencil_probe PRIVATE -Wall -Wextra -Werror
  "-ffile-prefix-map=${DEKO_ROOT}=/src/deko3d"
  "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=/build/z24s8-probe"
  "-fmacro-prefix-map=${DEKO_ROOT}=/src/deko3d")
target_compile_definitions(z24s8_stencil_probe PRIVATE
  PROBE_SOURCE_ID="${PROBE_SOURCE_ID}" PROTOTYPE_LIBRARY_SHA256="${PROTOTYPE_LIBRARY_SHA256}")
target_link_libraries(z24s8_stencil_probe PRIVATE "${PROTOTYPE_LIBRARY}" nx)
target_link_options(z24s8_stencil_probe PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/z24s8_stencil_probe.map,--cref")
nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/z24s8_stencil_probe.nacp"
  NAME "Z24S8 stencil correctness probe" AUTHOR "Local graphics research" VERSION "0.1.0")
nx_create_nro(z24s8_stencil_probe NOICON NACP "${CMAKE_CURRENT_BINARY_DIR}/z24s8_stencil_probe.nacp")
