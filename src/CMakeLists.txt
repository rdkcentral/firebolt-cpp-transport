# Copyright 2025 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

set(TARGET ${PROJECT_NAME})
message("Setup ${TARGET} v${PROJECT_VERSION}")

string(TOLOWER "${TARGET}" TARGET_LOWER)

file(GLOB SOURCES *.cpp)

if(NOT ENABLE_TESTS)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
else()
    set(CMAKE_CXX_VISIBILITY_PRESET default)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN OFF)
endif()

add_library(${TARGET}
    ${SOURCES}
)

add_library(${TARGET}::${TARGET} ALIAS ${TARGET})

if(ENABLE_TESTS)
    target_compile_options(${TARGET} PRIVATE --coverage -g -O0 -fno-inline)
    target_link_options(${TARGET} PRIVATE --coverage)
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json REQUIRED)
endif()

if(NOT TARGET websocketpp::websocketpp)
    find_package(websocketpp REQUIRED)
endif()

if(NOT TARGET Boost::system)
    find_package(Boost REQUIRED COMPONENTS system)
endif()

generate_export_header(
    ${TARGET}
    EXPORT_FILE_NAME "firebolt/transport_export.h"
)

configure_file(
    "${PROJECT_SOURCE_DIR}/include/firebolt/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/firebolt/transport_version.h"
    @ONLY
)

target_link_libraries(${TARGET}
    PRIVATE
        nlohmann_json::nlohmann_json
        websocketpp::websocketpp
)

target_include_directories(${TARGET}
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1" PROJECT_VERSION_MAJOR ${PROJECT_VERSION})
set_target_properties(${TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

install(
    TARGETS ${TARGET}
    EXPORT ${TARGET}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    EXPORT ${TARGET}Targets
    NAMESPACE ${TARGET}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/
    FILES_MATCHING PATTERN "*.h"
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/firebolt/transport_export.h
        ${CMAKE_CURRENT_BINARY_DIR}/firebolt/transport_version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/firebolt
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
