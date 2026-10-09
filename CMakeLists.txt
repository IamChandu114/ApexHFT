cmake_minimum_required(VERSION 3.25)

project(ApexHFT VERSION 0.1.0 LANGUAGES CXX)

option(APEXHFT_ENABLE_TESTS "Build unit tests" ON)
option(APEXHFT_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(APEXHFT_ENABLE_BENCHMARKS "Build the dependency-free benchmark executable" ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "ApexHFT requires GCC or Clang.")
endif()

add_library(apexhft_warnings INTERFACE)
target_compile_options(apexhft_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror
)

if(APEXHFT_ENABLE_SANITIZERS)
    target_compile_options(apexhft_warnings INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(apexhft_warnings INTERFACE
        -fsanitize=address,undefined
    )
endif()

add_library(apexhft_core STATIC src/version.cpp)
target_include_directories(apexhft_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(apexhft_core PUBLIC cxx_std_20)
target_link_libraries(apexhft_core PRIVATE apexhft_warnings)
target_link_libraries(apexhft_core INTERFACE apexhft_warnings)

add_executable(apexhft src/main.cpp)
target_link_libraries(apexhft PRIVATE apexhft_core apexhft_warnings)

add_executable(apexhft_research src/research_main.cpp)
target_link_libraries(apexhft_research PRIVATE apexhft_core apexhft_warnings)

if(APEXHFT_ENABLE_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(APEXHFT_ENABLE_BENCHMARKS)
    add_executable(apexhft_bench bench/bench_main.cpp)
    target_link_libraries(apexhft_bench PRIVATE apexhft_core apexhft_warnings)
    target_compile_definitions(apexhft_bench PRIVATE
        "APEXHFT_BENCH_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\""
    )
endif()
