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
if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^[vV]([0-9]+\\.[0-9]+\\.[0-9]+)")
  set(GV2FSM_VERSION "${CMAKE_MATCH_1}")
else()
  set(GV2FSM_VERSION "0.0.0")
endif()
message(STATUS "gv2fsm version: ${GV2FSM_VERSION}")

project(gv2fsm_cpp VERSION ${GV2FSM_VERSION} LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

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

set(INJA_USE_EMBEDDED_JSON OFF CACHE BOOL "" FORCE)
set(INJA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

# nlohmann_json (inja needs it)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(json)

FetchContent_MakeAvailable(cxxopts inja)

set(GV2FSM_LIBRARY_SOURCES
  src/gv2fsm.cpp
  src/dot_parser.cpp
  src/fsm.cpp
  src/generator.cpp
)

add_library(gv2fsm_static STATIC ${GV2FSM_LIBRARY_SOURCES})
add_library(gv2fsm_shared SHARED ${GV2FSM_LIBRARY_SOURCES})

set_target_properties(gv2fsm_static PROPERTIES OUTPUT_NAME gv2fsm)
set_target_properties(gv2fsm_shared PROPERTIES OUTPUT_NAME gv2fsm)

foreach(target gv2fsm_static gv2fsm_shared)
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
      $<INSTALL_INTERFACE:include/gv2fsm>
  )
  target_link_libraries(${target} PUBLIC cxxopts::cxxopts pantor::inja)
endforeach()

add_executable(gv2fsm src/main.cpp)
target_link_libraries(gv2fsm PRIVATE gv2fsm_static)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
  @ONLY
)

install(TARGETS gv2fsm gv2fsm_static gv2fsm_shared
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(FILES
  src/dot_parser.hpp
  src/fsm.hpp
  src/generator.hpp
  src/gv2fsm.hpp
  "${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
  DESTINATION include/gv2fsm
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
target_link_libraries(gv2fsm_tests PRIVATE Catch2::Catch2WithMain gv2fsm_static)
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
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(CPACK_GENERATOR "STGZ")
endif()

include(CPack)
