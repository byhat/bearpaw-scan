cmake_minimum_required(VERSION 3.21)
project(bearpaw-scan VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# ---- engine (vendored libbearpawcore from the bearpaw-sane driver) ----
add_library(bearpawcore STATIC
    src/core/bearpaw.h
    src/core/bearpaw.cpp
)
target_link_libraries(bearpawcore PUBLIC PkgConfig::LIBUSB)
target_include_directories(bearpawcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

# the recorded vendor session is embedded at link time (ld -b binary):
# the app ships as a single self-contained binary
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/session_ops.o
    COMMAND ld -r -b binary -o ${CMAKE_CURRENT_BINARY_DIR}/session_ops.o session.ops
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/session.ops
    VERBATIM)
target_sources(bearpawcore PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/session_ops.o)

# ---- GUI ----
qt_add_executable(bearpaw-scan
    src/main.cpp
    src/logmodel.h src/logmodel.cpp
    src/scanworker.h src/scanworker.cpp
)
qt_add_qml_module(bearpaw-scan
    URI BearPawScan
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES src/main.qml
)
target_link_libraries(bearpaw-scan PRIVATE Qt6::Quick bearpawcore)
