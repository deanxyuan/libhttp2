cmake_minimum_required(VERSION 3.10)
project(libhttp2 VERSION 1.0.0 LANGUAGES CXX)

# This project requires C++11.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PACKAGE_NAME          "libhttp2")
set(PACKAGE_VERSION       "1.0.0")

if(UNIX)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(LIBHTTP2_PLATFORM_LINUX ON)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(LIBHTTP2_PLATFORM_MAC ON)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "iOS")
        set(LIBHTTP2_PLATFORM_IOS ON)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
        set(LIBHTTP2_PLATFORM_ANDROID ON)
    else()
        set(LIBHTTP2_PLATFORM_POSIX ON)
    endif()
endif()
if(WIN32)
    set(LIBHTTP2_PLATFORM_WINDOWS ON)
endif()


if(LIBHTTP2_PLATFORM_MAC OR LIBHTTP2_PLATFORM_IOS)
    set(LIBHTTP2_BASE_LIBRARIES ${CMAKE_DL_LIBS} m pthread)
elseif(LIBHTTP2_PLATFORM_ANDROID)
    set(LIBHTTP2_BASE_LIBRARIES ${CMAKE_DL_LIBS} m)
elseif(UNIX)
    set(LIBHTTP2_BASE_LIBRARIES ${CMAKE_DL_LIBS} rt m pthread)
endif()

option(LIBHTTP2_BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" OFF)

option(BUILD_SHARED_LIBS "Build as shared library" OFF)

# Public API uses STL types (std::string, std::vector, etc.), so shared library
# is not supported on Windows due to MSVC STL ABI instability across versions.
if(WIN32 AND BUILD_SHARED_LIBS)
    message(WARNING "LibHttp2 public API uses STL types; forcing static library on Windows")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

option(LIBHTTP2_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LIBHTTP2_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LIBHTTP2_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

add_library(http2 "")

set_target_properties(http2 PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PACKAGE_VERSION}
    SOVERSION 1
)

target_compile_options(http2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
target_compile_definitions(http2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS>
)

aux_source_directory("${PROJECT_SOURCE_DIR}/src/hpack"  HPACK_SOURCES)
aux_source_directory("${PROJECT_SOURCE_DIR}/src/http2"  HTTP2_SOURCES)
aux_source_directory("${PROJECT_SOURCE_DIR}/src/utils"  UTILS_SOURCES)
# testutil is linked into test targets only, not the library itself
list(FILTER UTILS_SOURCES EXCLUDE REGEX "testutil\\.cc$")

target_sources(http2
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/http2.cc"
    ${HPACK_SOURCES}
    ${HTTP2_SOURCES}
    ${UTILS_SOURCES}
)

target_include_directories(http2
    PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    PUBLIC "$<INSTALL_INTERFACE:include>"
    PRIVATE "${PROJECT_SOURCE_DIR}"
)

target_link_libraries(http2
    PRIVATE
    ${LIBHTTP2_BASE_LIBRARIES}
)

# Sanitizer support
if(LIBHTTP2_ENABLE_ASAN)
    target_compile_options(http2 PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address -fno-omit-frame-pointer>)
    target_compile_options(http2 PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>)
    target_link_options(http2 PUBLIC $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address>)
endif()

if(LIBHTTP2_ENABLE_UBSAN)
    target_compile_options(http2 PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=undefined>)
    target_link_options(http2 PUBLIC $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=undefined>)
endif()

if(LIBHTTP2_ENABLE_TSAN)
    target_compile_options(http2 PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=thread>)
    target_link_options(http2 PUBLIC $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=thread>)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS http2 EXPORT http2Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/http2 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT http2Targets
    FILE libhttp2Targets.cmake
    NAMESPACE libhttp2::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libhttp2
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libhttp2Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libhttp2Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libhttp2
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libhttp2ConfigVersion.cmake"
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libhttp2Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libhttp2ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libhttp2
)

if (LIBHTTP2_BUILD_TESTS)
    enable_testing()
    function(libhttp2_test test_file)
        get_filename_component(test_target_name "${test_file}" NAME_WE)
        add_executable("${test_target_name}" "")
        target_sources("${test_target_name}"
            PRIVATE
            "${PROJECT_SOURCE_DIR}/src/utils/testutil.cc"
            "${PROJECT_SOURCE_DIR}/src/utils/testutil.h"
            "${test_file}"
        )
        target_link_libraries("${test_target_name}" http2)
        target_include_directories("${test_target_name}"
            PRIVATE
            "${PROJECT_SOURCE_DIR}"
        )
        # Apply sanitizer compile options to test targets
        if(LIBHTTP2_ENABLE_ASAN)
            target_compile_options("${test_target_name}" PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address -fno-omit-frame-pointer>)
            target_compile_options("${test_target_name}" PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>)
        endif()
        if(LIBHTTP2_ENABLE_UBSAN)
            target_compile_options("${test_target_name}" PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=undefined>)
        endif()
        if(LIBHTTP2_ENABLE_TSAN)
            target_compile_options("${test_target_name}" PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=thread>)
        endif()
        add_test(NAME "${test_target_name}" COMMAND "${test_target_name}")
    endfunction()

    file(GLOB LIBHTTP2_TEST_SOURCES "${PROJECT_SOURCE_DIR}/test/test_*.cc")
    foreach(test_file ${LIBHTTP2_TEST_SOURCES})
        libhttp2_test("${test_file}")
    endforeach()

endif()

if(BUILD_EXAMPLES)
    function(libhttp2_example example_file)
        get_filename_component(example_target "${example_file}" NAME_WE)
        add_executable("${example_target}" "${example_file}")
        target_link_libraries("${example_target}" http2)
        target_include_directories("${example_target}" PRIVATE "${PROJECT_SOURCE_DIR}")
    endfunction()

    file(GLOB LIBHTTP2_EXAMPLE_SOURCES "${PROJECT_SOURCE_DIR}/examples/*.cc")
    foreach(example_file ${LIBHTTP2_EXAMPLE_SOURCES})
        libhttp2_example("${example_file}")
    endforeach()
endif()
