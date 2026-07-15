# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# The SPI/MAX31865 temperature monitor is legacy-only. Keep its source in the
# repository for reference, but never discover or compile it in production.
set(EXCLUDE_COMPONENTS temperature_monitor_component)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(furnace-firmware)
