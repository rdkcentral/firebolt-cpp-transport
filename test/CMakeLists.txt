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

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

include(GoogleTest)

enable_testing()

set(UNIT_TESTS_APP FireboltTransportUnitTests)

message("Setup ${UNIT_TESTS_APP}")

set(UNIT_TESTS
    "unit/json_typesTest.cpp"
    "unit/transportTest.cpp"
)

add_executable(${UNIT_TESTS_APP}
    UnitTestsMain.cpp
    ${UNIT_TESTS}
)

target_compile_options(${UNIT_TESTS_APP} PRIVATE --coverage -g -O0 -fno-inline)
target_link_options(${UNIT_TESTS_APP} PRIVATE --coverage)

target_link_libraries(${UNIT_TESTS_APP}
    PRIVATE
        FireboltTransport
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
        GTest::gtest
        GTest::gmock
)

target_include_directories(${UNIT_TESTS_APP}
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/test/>
)

set_target_properties(${UNIT_TESTS_APP} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src"
    INSTALL_RPATH "$ORIGIN/../src"
)

if(DISCOVER_UT)
    gtest_discover_tests(${UNIT_TESTS_APP}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src"
    )
endif()
