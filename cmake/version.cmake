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
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_SHA
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE
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

if (NOT GIT_SHA)
    set(GIT_SHA "unknown")
endif ()
if (NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "${GIT_SHA}")
endif ()

# Allow build system (e.g. Yocto) to override the git ref label when no .git
# directory is present (tarball builds). Pass -DFIREBOLT_GIT_REF=<value> from
# EXTRA_OECMAKE in a dev bbappend (e.g. -DFIREBOLT_GIT_REF=${SRCREV}).
if (FIREBOLT_GIT_REF)
    string(LENGTH "${FIREBOLT_GIT_REF}" _ref_len)
    if (_ref_len GREATER 8)
        string(SUBSTRING "${FIREBOLT_GIT_REF}" 0 8 GIT_SHA)
    else ()
        set(GIT_SHA "${FIREBOLT_GIT_REF}")
    endif ()
    set(GIT_DESCRIBE "${GIT_SHA}")
elseif (GIT_DESCRIBE STREQUAL "unknown")
    # No git and no override: this is a release tarball build — use the version
    # as the ref since it corresponds to the tagged release.
    set(GIT_DESCRIBE "v${PROJECT_VERSION}")
    set(GIT_SHA "v${PROJECT_VERSION}")
endif ()

# Honor SOURCE_DATE_EPOCH for reproducible/cached builds (common Yocto/Debian convention).
if (DEFINED ENV{SOURCE_DATE_EPOCH})
    execute_process(
        COMMAND date --utc "--date=@$ENV{SOURCE_DATE_EPOCH}" "+%Y-%m-%dT%H:%M:%SZ"
        OUTPUT_VARIABLE BUILD_TIMESTAMP
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _sde_result
        ERROR_QUIET
    )
    if (NOT _sde_result EQUAL 0)
        string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
    endif ()
else ()
    string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
endif ()

set(VERSION "${PROJECT_VERSION}")
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1" PROJECT_VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\2" PROJECT_VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\3" PROJECT_VERSION_PATCH "${VERSION}")
