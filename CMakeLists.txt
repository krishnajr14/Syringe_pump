cmake_minimum_required(VERSION 3.20)
project(syringe_pump_host_tests CXX)

# ── Language standard ────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Coverage flags (GCC / Clang) ─────────────────────────────────────────
option(ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0 -g -fno-inline)
        add_link_options(--coverage)
    else()
        message(WARNING "Coverage only supported with GCC or Clang")
    endif()
endif()

# ── GTest ────────────────────────────────────────────────────────────────
find_package(GTest REQUIRED)
include(GoogleTest)
enable_testing()

# ── Source under test (pure C++ — NO Zephyr headers) ─────────────────────
set(SUT_SOURCES
    src/AlarmManager.cpp
    src/VolumeTracker.cpp
    src/PumpStateMachine.cpp
    src/CommandParser.cpp
)

# ── Test executable ───────────────────────────────────────────────────────
add_executable(syringe_pump_tests
    ${SUT_SOURCES}
    tests/test_PumpStateMachine.cpp
    tests/test_VolumeTracker.cpp
    tests/test_AlarmManager.cpp
    tests/test_CommandParser.cpp
)

target_include_directories(syringe_pump_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include      # syringe/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs  # *Stub.hpp
)

target_link_libraries(syringe_pump_tests
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(syringe_pump_tests)

# ── Coverage target ───────────────────────────────────────────────────────
# Usage: cmake -B build -DENABLE_COVERAGE=ON && cmake --build build --target coverage
add_custom_target(coverage
    DEPENDS syringe_pump_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND lcov
            --capture
            --directory ${CMAKE_BINARY_DIR}
            --output-file ${CMAKE_BINARY_DIR}/coverage_raw.info
            --rc lcov_branch_coverage=1
            --ignore-errors mismatch
    COMMAND lcov
            --remove ${CMAKE_BINARY_DIR}/coverage_raw.info
            "/usr/*"
            "*/tests/*"
            "*/gtest/*"
            "*/zephyr/*"
            --output-file ${CMAKE_BINARY_DIR}/coverage.info
            --rc lcov_branch_coverage=1
            --ignore-errors unused
    COMMAND genhtml
            ${CMAKE_BINARY_DIR}/coverage.info
            --output-directory ${CMAKE_BINARY_DIR}/coverage_html
            --branch-coverage
    COMMAND lcov --list ${CMAKE_BINARY_DIR}/coverage.info
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating coverage report → build/coverage_html/index.html"
)
