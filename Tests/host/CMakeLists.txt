cmake_minimum_required(VERSION 3.22)
project(COSMOKE_HostTests C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(cosmoke_measurement_tests
  test_measurement.c
  ../../Core/Src/measurement.c
)

target_include_directories(cosmoke_measurement_tests PRIVATE
  ../../Core/Inc
)

if(MSVC)
  target_compile_options(cosmoke_measurement_tests PRIVATE /W4)
else()
  target_compile_options(cosmoke_measurement_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME measurement_conversion COMMAND cosmoke_measurement_tests)
