cmake_minimum_required(VERSION 3.16)

# Extract version from git tag matching ^[vV]\d.\d.\d(-...)?
execute_process(
  COMMAND git describe --tags --abbrev=0
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE GIT_TAG_RESULT
)
if(GV2FSM_VERSION)
  message(STATUS "gv2fsm version: ${GV2FSM_VERSION} (overridden by GV2FSM_VERSION environment variable)")
elseif(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^[vV]([0-9]+\\.[0-9]+\\.[0-9]+)")
  set(GV2FSM_VERSION "${CMAKE_MATCH_1}")
  message(STATUS "gv2fsm version (from git tag): ${GV2FSM_VERSION}")
else()
  set(GV2FSM_VERSION "0.0.0")
  message(STATUS "gv2fsm version undefined and set to: ${GV2FSM_VERSION}")
endif()

project(gv2fsm_cpp VERSION ${GV2FSM_VERSION} LANGUAGES CXX)
# Needed at top scope for the C sources of the vendored tree-sitter grammars.
enable_language(C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CMakePackageConfigHelpers)
include(FetchContent)
include(GNUInstallDirs)

# cxxopts for CLI parsing
FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG        v3.2.1
)

# inja for templating
FetchContent_Declare(
  inja
  GIT_REPOSITORY https://github.com/pantor/inja.git
  GIT_TAG        v3.4.0
)

set(INJA_USE_EMBEDDED_JSON ON CACHE BOOL "" FORCE)
set(INJA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INJA_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cxxopts inja)

# tree-sitter: powers context-aware merging of user code during `--update`
# regeneration. The core library's own CMakeLists.txt is clean (no external
# tool needed), so it is fetched normally, forced to build static.
FetchContent_Declare(
  tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG        v0.26.5
)
set(_gv2fsm_saved_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tree_sitter)
set(BUILD_SHARED_LIBS ${_gv2fsm_saved_build_shared_libs} CACHE BOOL "" FORCE)

# tree-sitter-c / tree-sitter-cpp grammars: only the sources are fetched
# (FetchContent_Populate, not MakeAvailable) because each grammar's own
# CMakeLists.txt regenerates src/parser.c by shelling out to the `tree-sitter`
# CLI tool, which we don't want as a required build-time dependency. The
# pre-generated parser.c (and scanner.c for C++) are already committed in
# each repo, so we compile those directly ourselves.
FetchContent_Declare(
  tree_sitter_c
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
  GIT_TAG        v0.24.2
)
FetchContent_Declare(
  tree_sitter_cpp
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
  GIT_TAG        v0.23.4
)
FetchContent_Populate(tree_sitter_c)
FetchContent_Populate(tree_sitter_cpp)

add_library(gv2fsm_ts_c STATIC ${tree_sitter_c_SOURCE_DIR}/src/parser.c)
target_include_directories(gv2fsm_ts_c PRIVATE ${tree_sitter_c_SOURCE_DIR}/src)
set_target_properties(gv2fsm_ts_c PROPERTIES
  C_STANDARD 11
  POSITION_INDEPENDENT_CODE ON
)

add_library(gv2fsm_ts_cpp STATIC
  ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
  ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(gv2fsm_ts_cpp PRIVATE ${tree_sitter_cpp_SOURCE_DIR}/src)
set_target_properties(gv2fsm_ts_cpp PROPERTIES
  C_STANDARD 11
  POSITION_INDEPENDENT_CODE ON
)

option(GV2FSM_SHARED_ONLY "Build only the shared gv2fsm library" OFF)
option(GV2FSM_STATIC_ONLY "Build only the static gv2fsm library" OFF)

if(GV2FSM_SHARED_ONLY AND GV2FSM_STATIC_ONLY)
  message(FATAL_ERROR
    "GV2FSM_SHARED_ONLY and GV2FSM_STATIC_ONLY are mutually exclusive"
  )
endif()

set(GV2FSM_BUILD_SHARED ON)
set(GV2FSM_BUILD_STATIC ON)

if(GV2FSM_SHARED_ONLY)
  set(GV2FSM_BUILD_STATIC OFF)
elseif(GV2FSM_STATIC_ONLY)
  set(GV2FSM_BUILD_SHARED OFF)
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
  @ONLY
)

set(GV2FSM_LIBRARY_SOURCES
  src/gv2fsm.cpp
  src/dot_parser.cpp
  src/fsm.cpp
  src/generator.cpp
  src/merge.cpp
)

set(GV2FSM_EXPORT_TARGETS gv2fsm_interface)

function(gv2fsm_configure_library target_name)
  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/gv2fsm>
  )
  target_link_libraries(${target_name}
    PRIVATE
      $<BUILD_INTERFACE:cxxopts::cxxopts>
      $<BUILD_INTERFACE:pantor::inja>
      $<BUILD_INTERFACE:tree-sitter>
      $<BUILD_INTERFACE:gv2fsm_ts_c>
      $<BUILD_INTERFACE:gv2fsm_ts_cpp>
  )
  set_target_properties(${target_name} PROPERTIES OUTPUT_NAME gv2fsm)
endfunction()

if(GV2FSM_BUILD_STATIC)
  add_library(gv2fsm_static STATIC ${GV2FSM_LIBRARY_SOURCES})
  gv2fsm_configure_library(gv2fsm_static)
  set_target_properties(gv2fsm_static PROPERTIES EXPORT_NAME Gv2FsmStatic)
  list(APPEND GV2FSM_EXPORT_TARGETS gv2fsm_static)
endif()

if(GV2FSM_BUILD_SHARED)
  add_library(gv2fsm_shared SHARED ${GV2FSM_LIBRARY_SOURCES})
  gv2fsm_configure_library(gv2fsm_shared)
  set_target_properties(gv2fsm_shared PROPERTIES EXPORT_NAME Gv2FsmShared)
  list(APPEND GV2FSM_EXPORT_TARGETS gv2fsm_shared)
endif()

if(BUILD_SHARED_LIBS AND GV2FSM_BUILD_SHARED)
  set(GV2FSM_LINK_TARGET gv2fsm_shared)
elseif(GV2FSM_BUILD_STATIC)
  set(GV2FSM_LINK_TARGET gv2fsm_static)
else()
  set(GV2FSM_LINK_TARGET gv2fsm_shared)
endif()

add_library(gv2fsm_interface INTERFACE)
target_link_libraries(gv2fsm_interface INTERFACE ${GV2FSM_LINK_TARGET})
set_target_properties(gv2fsm_interface PROPERTIES EXPORT_NAME Gv2Fsm)
add_library(Gv2Fsm::Gv2Fsm ALIAS gv2fsm_interface)

add_executable(gv2fsm src/main.cpp)
target_link_libraries(gv2fsm PRIVATE ${GV2FSM_LINK_TARGET})

install(TARGETS ${GV2FSM_EXPORT_TARGETS}
  EXPORT Gv2FsmTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gv2fsm
)
install(TARGETS gv2fsm
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES
  src/dot_parser.hpp
  src/fsm.hpp
  src/generator.hpp
  src/gv2fsm.hpp
  src/merge.hpp
  "${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gv2fsm
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/Gv2FsmConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Gv2FsmConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Gv2FsmConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Gv2Fsm"
)

install(EXPORT Gv2FsmTargets
  FILE Gv2FsmTargets.cmake
  NAMESPACE Gv2Fsm::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Gv2Fsm"
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/Gv2FsmConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/Gv2FsmConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Gv2Fsm"
)

# ---- Testing ----
option(GV2FSM_ENABLE_TESTS "Build the test suite" OFF)

if(GV2FSM_ENABLE_TESTS)
set(BUILD_TESTING ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(gv2fsm_tests
  test/test_main.cpp
)
target_link_libraries(gv2fsm_tests PRIVATE Catch2::Catch2WithMain ${GV2FSM_LINK_TARGET})
target_compile_definitions(gv2fsm_tests PRIVATE
  SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

include(CTest)
include(Catch)
catch_discover_tests(gv2fsm_tests)
endif()

# ---- Packaging ----
set(CPACK_PACKAGE_NAME "gv2fsm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Graphviz DOT to Finite State Machine C/C++ code generator")
set(CPACK_PACKAGE_CONTACT "paolo.bosetti@unitn.it")
set(CPACK_PACKAGE_VENDOR "gv2fsm")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_GENERATOR "DEB")
  # Install under /usr/local (the DEB generator defaults to /usr) so the binary
  # lands in /usr/local/bin, which precedes /usr/bin on the default PATH.
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(CPACK_GENERATOR "STGZ")
endif()

include(CPack)
