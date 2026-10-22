cmake_minimum_required(VERSION 3.20)
project(deko3d_stencil_z24s8_host LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_program(DEKODEF dekodef HINTS /opt/devkitpro/tools/bin REQUIRED)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/engine_copy.h"
  COMMAND "${DEKODEF}" -h "${CMAKE_CURRENT_BINARY_DIR}/engine_copy.h"
          "${ROOT}/source/maxwell/engine_copy.def"
  DEPENDS "${ROOT}/source/maxwell/engine_copy.def" VERBATIM)
enable_testing()
foreach(target stencil_z24s8_host stencil_z24s8_sanitized)
  add_executable(${target} StencilZ24S8Tests.cpp
    "${ROOT}/examples/z24s8_stencil_probe/ProbeCore.cpp"
    "${ROOT}/examples/z24s8_stencil_probe/TerminalResult.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/engine_copy.h")
  target_include_directories(${target} PRIVATE "${ROOT}/include" "${ROOT}/source"
    "${CMAKE_CURRENT_BINARY_DIR}")
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES TIMEOUT 30)
endforeach()
target_compile_options(stencil_z24s8_sanitized PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(stencil_z24s8_sanitized PRIVATE -fsanitize=address,undefined)
set_tests_properties(stencil_z24s8_sanitized PROPERTIES
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# Only new probe tests run for hardware preparation; prior encoder tests are retained.
add_test(NAME probe_host COMMAND stencil_z24s8_host --probe-only)
add_test(NAME probe_sanitized COMMAND stencil_z24s8_sanitized --probe-only)
set_tests_properties(probe_host probe_sanitized PROPERTIES TIMEOUT 30)
set_tests_properties(probe_sanitized PROPERTIES
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
