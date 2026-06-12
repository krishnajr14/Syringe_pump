# ── Zephyr firmware build ─────────────────────────────────────────────────
# Invoke ONLY via:  west build -b nucleo_g474re
# Do NOT run cmake directly on this file — it requires the Zephyr SDK.
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(syringe_pump_fw VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Sources ───────────────────────────────────────────────────────────────
# main.cpp (Zephyr-specific) + the same pure-C++ SUT sources
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AlarmManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/VolumeTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/PumpStateMachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CommandParser.cpp
)

# include/ exposes syringe/*.hpp and syringe/hal/*.hpp
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
