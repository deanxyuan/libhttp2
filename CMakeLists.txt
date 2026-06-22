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

option(BUILD_SHARED_LIBS "Build as shared library" OFF)
option(LIBHTTP2_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LIBHTTP2_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LIBHTTP2_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

add_library(http2 "")

set_target_properties(http2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_options(http2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
target_compile_definitions(http2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS>
)

set(HPACK_SOURCES
    "${PROJECT_SOURCE_DIR}/src/hpack/decode.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/dynamic_metadata.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/encode.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/hpack.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/huffman_data.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/huffman.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/send_record.cc"
    "${PROJECT_SOURCE_DIR}/src/hpack/static_metadata.cc"
)

set(HTTP2_SOURCES
    "${PROJECT_SOURCE_DIR}/src/http2/connection.cc"
    "${PROJECT_SOURCE_DIR}/src/http2/frame.cc"
    "${PROJECT_SOURCE_DIR}/src/http2/pack.cc"
    "${PROJECT_SOURCE_DIR}/src/http2/parser.cc"
    "${PROJECT_SOURCE_DIR}/src/http2/settings.cc"
    "${PROJECT_SOURCE_DIR}/src/http2/stream.cc"
)

set(UTILS_SOURCES
    "${PROJECT_SOURCE_DIR}/src/utils/byte_order.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/mpscq.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/murmur_hash.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/slice_buffer.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/slice.cc"
)

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

install(TARGETS http2
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/http2 DESTINATION include)

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

    libhttp2_test("${PROJECT_SOURCE_DIR}/test/byte_order_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/dynamic_table_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/hpack_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/huffman_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/integration_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/pack_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/parse_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/slice_test.cc")
    libhttp2_test("${PROJECT_SOURCE_DIR}/test/static_table_test.cc")

endif()
