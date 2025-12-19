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

if (FIREBOLT_TRANSPORT_VERSION)
    set(PROJECT_VERSION "${FIREBOLT_TRANSPORT_VERSION}")
endif ()

if (NOT PROJECT_VERSION)
    set(VERSION_STRING "0.1.0-unknown")

    find_package(Git QUIET)

    if (GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v[0-9]*.[0-9]*.[0-9]*"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif ()

    if (GIT_VERSION)
        string(REGEX REPLACE "^v" "" VERSION_STRING "${GIT_VERSION}")
    endif ()

    if(VERSION_STRING STREQUAL "0.1.0-unknown" AND EXISTS "${CMAKE_SOURCE_DIR}/.version")
        file(READ "${CMAKE_SOURCE_DIR}/.version" VERSION_STRING)
        string(STRIP "${VERSION_STRING}" VERSION_STRING)
    endif()

    set(PROJECT_VERSION "${VERSION_STRING}" CACHE STRING "Project version string")
    set(PROJECT_VERSION "${VERSION_STRING}")
endif ()

set(VERSION "${PROJECT_VERSION}")
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1" PROJECT_VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\2" PROJECT_VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\3" PROJECT_VERSION_PATCH "${VERSION}")
