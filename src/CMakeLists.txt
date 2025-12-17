# Copyright 2023 Comcast Cable Communications Management, LLC
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

cmake_minimum_required(VERSION 3.3)

set(TARGET ${PROJECT_NAME})
message("Setup ${TARGET} v${PROJECT_VERSION}")

file(GLOB SOURCES *.cpp)
add_library(${TARGET} ${FIREBOLT_LIBRARY_TYPE}
    ${SOURCES}
    Logger/Logger.cpp
    Transport/Transport.cpp
    Accessor/Accessor.cpp
    Event/Event.cpp
    Async/Async.cpp
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(${NAMESPACE}WebSocket CONFIG REQUIRED)
find_package(${NAMESPACE}Core CONFIG REQUIRED)

if (ENABLE_UNIT_TESTS)
    find_package(nlohmann_json_schema_validator)
    find_package(GTest)
    target_link_libraries(${TARGET}
        PRIVATE
            nlohmann_json_schema_validator::validator
            GTest::gtest)
endif ()

target_link_libraries(${TARGET}
    PRIVATE
        ${NAMESPACE}WebSocket::${NAMESPACE}WebSocket
        ${NAMESPACE}Core::${NAMESPACE}Core
)

target_include_directories(${TARGET}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
)

set_target_properties(${TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    FRAMEWORK FALSE
    LINK_WHAT_YOU_USE TRUE
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
    TARGETS ${TARGET}
    EXPORT ${TARGET}Targets
    ARCHIVE DESTINATION lib # static lib
    LIBRARY DESTINATION lib # shared lib
)

install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION include/${PROJECT_NAME}
    FILES_MATCHING PATTERN "*.h")

install(
    FILES ${CMAKE_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    DESTINATION lib/cmake/${PROJECT_NAME})

InstallCMakeConfig(TARGETS ${TARGET})
InstallPackageConfig(TARGETS ${TARGET} DESCRIPTION "Firebolt Transport Library")

