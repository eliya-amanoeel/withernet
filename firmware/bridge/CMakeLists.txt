# withernet bridge firmware for the WT32-ETH01, built with ESP-IDF (idf.py).
# Each dongle builds in its own folder, see main/CMakeLists.txt:
#   idf.py -B build/checkwatt -DBOARD=checkwatt build
# (tools/build-signed.sh does this and signs the result.)
cmake_minimum_required(VERSION 3.16)

# Each build folder keeps its own sdkconfig, generated from sdkconfig.defaults.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The bridge uses undocumented Wi-Fi calls (esp_wifi_internal_*) that have
# changed between IDF versions, so the version is pinned. Don't upgrade it
# without re-running the bench test.
set(withernet_idf_version "5.3.1")
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" STREQUAL withernet_idf_version)
    message(FATAL_ERROR "ESP-IDF ${withernet_idf_version} needed, this is "
        "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH} ($ENV{IDF_PATH})")
endif()

project(withernet-bridge)
