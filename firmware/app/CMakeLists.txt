# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
option(WATCH_CALIBRATION "Build the USB wrist calibration recorder" OFF)
if(WATCH_CALIBRATION)
  set(CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/calibration.conf" CACHE STRING "Calibration Kconfig file" FORCE)
  set(EXTRA_DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/calibration.overlay")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_wearable)

if(WATCH_CALIBRATION)
  target_sources(app PRIVATE src/calibration.c)
else()
  target_sources(app PRIVATE src/main.c)
endif()
