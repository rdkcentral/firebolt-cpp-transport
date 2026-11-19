
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

cmake_minimum_required(VERSION 3.12)

# Determine whether Transport is being built standalone or
# included as a subproject (e.g. by FireboltEntos).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(FIREBOLT_TRANSPORT_STANDALONE ON)
else()
    set(FIREBOLT_TRANSPORT_STANDALONE OFF)
endif()

# Always use Transport's own CMake modules.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT DEFINED FIREBOLT_TRANSPORT_VERSION)
    # TEMPORARY local override for Yocto/debug work.
    set(FIREBOLT_TRANSPORT_VERSION "1.1.13"
        CACHE STRING "Firebolt transport version")
endif()

project(FireboltTransport)

# Explicit path avoids accidentally loading a parent
# project's version module.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake")

message(STATUS "Project version: ${PROJECT_VERSION}")

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_TESTS "Build tests" OFF)
option(DISCOVER_UT "Discover all Unit Tests" ON)
option(ENABLE_LEGACY_RPC_V1 "Use legacy communication" OFF)
option(DISABLE_SO_VERSION "Disable SONAME/SOVERSION of shared library" OFF)
option(FIREBOLT_TRANSPORT_ACK_OBSERVER "Test-only nonblocking ACK wait observation (not for release builds)" OFF)

# Only adjust global installation settings when building
# Transport as the top-level project.
if(FIREBOLT_TRANSPORT_STANDALONE AND SYSROOT_PATH)
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${SYSROOT_PATH}/usr"
            CACHE PATH "Install prefix" FORCE)
        set(CMAKE_PREFIX_PATH "${SYSROOT_PATH}/usr/lib/cmake"
            CACHE PATH "Package prefix" FORCE)
    endif()
endif()

# Add toolchain/sysroot search locations when available.
if(SYSROOT_PATH)
    list(APPEND CMAKE_PREFIX_PATH
        "${SYSROOT_PATH}/usr/share/cmake")

    list(APPEND CMAKE_MODULE_PATH
        "${SYSROOT_PATH}/usr/share/cmake"
        "${SYSROOT_PATH}/usr/lib/cmake"
        "${SYSROOT_PATH}/tools/cmake")
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(src)

if(ENABLE_TESTS AND FIREBOLT_TRANSPORT_STANDALONE)
    add_subdirectory(test)
endif()