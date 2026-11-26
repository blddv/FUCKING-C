# SPDX-License-Identifier: WTFPL
cmake_minimum_required(VERSION 3.20)

# The C++ header is the single source of truth for the version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/fucking_c.hpp" FC_VERSION_LINE
    REGEX "^inline constexpr std::string_view version = ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" FC_VERSION "${FC_VERSION_LINE}")
if(NOT FC_VERSION)
    message(FATAL_ERROR "Cannot read the version from include/fucking_c.hpp")
endif()
project(FuckingC VERSION ${FC_VERSION} DESCRIPTION "A C-only language with a native C++ interpreter" LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
option(FC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
find_package(Boost 1.74 REQUIRED)
include(CTest)
include(GNUInstallDirs)

add_library(fc_build_options INTERFACE)
target_compile_features(fc_build_options INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(fc_build_options INTERFACE /W4 /utf-8)
else()
    target_compile_options(fc_build_options INTERFACE -Wall -Wextra -Wpedantic)
endif()
if(FC_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR "FC_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    target_compile_options(fc_build_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(fc_build_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(fc_core STATIC src/syntax.cpp src/runtime.cpp src/cli.cpp)
target_include_directories(fc_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(fc_core PUBLIC fc_build_options)
if(TARGET Boost::headers)
    target_link_libraries(fc_core PRIVATE Boost::headers)
elseif(TARGET Boost::boost)
    target_link_libraries(fc_core PRIVATE Boost::boost)
else()
    target_include_directories(fc_core SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
endif()

add_executable(fc src/main.cpp)
target_link_libraries(fc PRIVATE fc_core)

if(BUILD_TESTING)
    add_executable(fc_tests tests/test_language.cpp)
    target_link_libraries(fc_tests PRIVATE fc_core)
    add_test(NAME language_and_cli COMMAND fc_tests "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    set_tests_properties(language_and_cli PROPERTIES TIMEOUT 120)
    add_test(NAME native_factorial COMMAND fc "${CMAKE_CURRENT_SOURCE_DIR}/examples/factorial.fc")
    set_tests_properties(native_factorial PROPERTIES PASS_REGULAR_EXPRESSION "720" TIMEOUT 10)
    add_test(NAME native_version COMMAND fc --version)
    set_tests_properties(native_version PROPERTIES PASS_REGULAR_EXPRESSION "Fucking C ${PROJECT_VERSION}" TIMEOUT 10)
endif()

install(TARGETS fc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md README.ru.md LICENSE THIRD_PARTY.md CHANGELOG.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/fucking-c")
install(DIRECTORY LICENSES/ DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/fucking-c/LICENSES")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/fucking-c/docs"
    FILES_MATCHING PATTERN "language*.md")
install(DIRECTORY examples/ DESTINATION "${CMAKE_INSTALL_DATADIR}/fucking-c/examples"
    FILES_MATCHING PATTERN "*.fc")

string(TOLOWER "${CMAKE_SYSTEM_NAME}" FC_SYSTEM)
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_NAME "fucking-c")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native C++20 interpreter for Fucking C")
set(CPACK_PACKAGE_FILE_NAME "fucking-c-${PROJECT_VERSION}-${FC_SYSTEM}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES TRUE)
include(CPack)
