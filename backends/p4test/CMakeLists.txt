# Copyright 2013-present Barefoot Networks, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Makefile for a fake backend that is used for testing the P4-16 front-end.

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

set (P4TEST_SRCS
  p4test.cpp
  midend.cpp
  )
set (P4TEST_HDRS
  midend.h
  )

add_cpplint_files (${CMAKE_CURRENT_SOURCE_DIR} "${P4TEST_SRCS};${P4TEST_HDRS}")

build_unified(P4TEST_SRCS ALL)
add_executable(p4test ${P4TEST_SRCS} ${EXTENSION_P4_14_CONV_SOURCES})
target_link_libraries (p4test ${P4C_LIBRARIES} ${P4C_LIB_DEPS})
add_dependencies(p4test genIR frontend)

install (TARGETS p4test
  RUNTIME DESTINATION ${P4C_RUNTIME_OUTPUT_DIRECTORY})

file(RELATIVE_PATH
  CURRENT_BINARY_DIR_PATH_REL
  ${P4C_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

file(RELATIVE_PATH
  P4C_BINARY_DIR_PATH_REL
  ${CMAKE_CURRENT_BINARY_DIR}
  ${P4C_BINARY_DIR}
)

# hack to get around the fact that the test scripts expect the backend
# binary to be in the top level directory. This should go away when we
# remove automake and fix the scripts.
add_custom_target(linkp4test
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CURRENT_BINARY_DIR_PATH_REL}/p4test ${P4C_BINARY_DIR}/p4test
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${P4C_BINARY_DIR_PATH_REL}/p4include ${CMAKE_CURRENT_BINARY_DIR}/p4include
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${P4C_BINARY_DIR_PATH_REL}/p4_14include ${CMAKE_CURRENT_BINARY_DIR}/p4_14include
  )
add_dependencies(p4c_driver linkp4test)

# Tests

set(P4TEST_DRIVER ${P4C_SOURCE_DIR}/backends/p4test/run-p4-sample.py)

set (P4TEST_SUITES
  "${P4C_SOURCE_DIR}/testdata/p4_16_samples/*.p4"
  "${P4C_SOURCE_DIR}/testdata/p4_16_samples/fabric_*/fabric.p4"
  )

# Builds a list of tests for which P4Info generation is not supported. It makes
# sense not to fail the test just because P4Info generation fails. It probably
# would not make sense to run all the tests twice (one with and one without
# P4Runtime).
set (P4RUNTIME_EXCLUDE
  # See discussion on this p4c PR: https://github.com/p4lang/p4c/pull/2425
  # It seems that the best long term approach for supporting programs
  # like this one is to enhance the P4Runtime API to support enum
  # types as action parameters, as requested in this issue:
  # https://github.com/p4lang/p4runtime/issues/191
  testdata/p4_16_samples/psa-meter1.p4
  # error not supported in static entries, not strictly required for P4Info
  # generation though
  testdata/p4_16_samples/issue1062-bmv2.p4
  # P4Info generation fails (as expected with the current P4Runtime spec) with:
  # Unsupported type parameter for Value Set; this version of P4Runtime requires
  # that when the type parameter of a Value Set is a struct, all the fields of
  # the struct must be of type bit<W>, but inner is not
  testdata/p4_16_samples/pvs-nested-struct.p4
  # The tests below use type `error` for table key which P4Runtime 1.x
  # does not support.
  # table t_exact key named m.my_err is type error
  testdata/p4_16_samples/issue1062-1-bmv2.p4
  # table t key named meta.err is type error
  testdata/p4_16_samples/issue1304.p4
  # table parser_error_count_and_convert key named istd.parser_error
  # is type error
  testdata/p4_16_samples/psa-example-parser-checksum.p4
)

set (P4_XFAIL_TESTS
  # This program is invalid according to the PSA specification as one direct
  # counter is shared by multiple match tables: "A DirectCounter instance must
  # appear as the value of the psa_direct_counter table attribute for at most
  # one table."
  testdata/p4_16_samples/psa-counter6.p4
  # This program uses a list of expressions on the 'psa_implementation' property
  # which is currently unsupported
  testdata/p4_16_samples/psa-action-profile2.p4
  # This program uses conditional execution inside action body which is currently
  # unsupported
  testdata/p4_16_samples/psa-dpdk-action-jumpOpt.p4
  )
# we invoke p4c with --p4runtime-files even for programs in p4_16_errors. This
# enables us to use p4_16_errors even for programs which pass compilation but
# fail P4Info generation expectedly (see issue 1803). If a program fails
# compilation, the P4Runtime serializer will not be invoked anyway and there
# will be no P4Info file.
p4c_add_tests_w_p4runtime("p4" ${P4TEST_DRIVER} "${P4TEST_SUITES}" "${P4_XFAIL_TESTS}" "${P4RUNTIME_EXCLUDE}" "-a '--maxErrorCount 100'")

set (P4TEST_PARSERUNROLL
  "${P4C_SOURCE_DIR}/testdata/p4_16_samples/parser-unroll-*.p4")
p4c_add_tests("p4unroll" ${P4TEST_DRIVER} "${P4TEST_PARSERUNROLL}" "" "-a '--maxErrorCount 100 --loopsUnroll'")

set (P4TEST_PARSER_INLINE_TESTS "${P4C_SOURCE_DIR}/testdata/p4_16_samples/parser-inline/*.p4")
p4c_add_tests("p4" ${P4TEST_DRIVER} "${P4TEST_PARSER_INLINE_TESTS}" "" "-a '--maxErrorCount 100 --parser-inline-opt'")

set (P4TEST_ERRORS
  "${P4C_SOURCE_DIR}/testdata/p4_16_errors/*.p4"
  "${P4C_SOURCE_DIR}/testdata/p4_14_errors/*.p4")
p4c_add_tests_w_p4runtime("err" ${P4TEST_DRIVER} "${P4TEST_ERRORS}" "${P4_XFAIL_TESTS}" "${P4RUNTIME_EXCLUDE}" "-a '--maxErrorCount 100'")

set (P4_14_SUITES
  "${P4C_SOURCE_DIR}/testdata/p4_14_samples/*.p4"
  "${P4C_SOURCE_DIR}/testdata/p4_14_samples/switch_*/switch.p4")
p4c_add_tests("p14_to_16" ${P4TEST_DRIVER} "${P4_14_SUITES}" "")
