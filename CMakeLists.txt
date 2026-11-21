cmake_minimum_required(VERSION 3.20)
project(keel_font VERSION 0.1.0 LANGUAGES CXX)

option(KEEL_FONT_NO_EXCEPTIONS_RTTI "Compile keel_font without C++ exceptions and RTTI" OFF)
option(KEEL_FONT_BUILD_TESTS "Build keel_font unit tests and header purity checks" ${PROJECT_IS_TOP_LEVEL})
option(KEEL_FONT_UNITY_BUILD "Enable unity build on keel_font" OFF)

if(NOT TARGET keel::keel)
    find_package(keel QUIET CONFIG)
endif()
if(NOT TARGET keel::keel)
    include(FetchContent)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../keel/CMakeLists.txt" AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_KEEL)
        set(FETCHCONTENT_SOURCE_DIR_KEEL "${CMAKE_CURRENT_SOURCE_DIR}/../keel" CACHE PATH "Local keel checkout")
    endif()
    if(KEEL_FONT_NO_EXCEPTIONS_RTTI)
        set(KEEL_NO_EXCEPTIONS_RTTI ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        keel
        GIT_REPOSITORY https://github.com/keelcpp/keel.git
        GIT_TAG        869a5c07a5d77341e5f6acbac5f8e024b274ea08
    )
    FetchContent_MakeAvailable(keel)
endif()

add_library(keel_font STATIC
    src/font.cpp
)
add_library(keel::font ALIAS keel_font)

target_link_libraries(keel_font PUBLIC keel::keel)
target_compile_features(keel_font PUBLIC cxx_std_20)
set_target_properties(keel_font PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(KEEL_FONT_UNITY_BUILD)
    set_target_properties(keel_font PROPERTIES UNITY_BUILD ON)
endif()

target_include_directories(keel_font PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(keel_font PRIVATE /W4 /utf-8)
    if(KEEL_FONT_NO_EXCEPTIONS_RTTI)
        string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        target_compile_options(keel_font PUBLIC /EHs-c- /GR-)
        target_compile_definitions(keel_font PUBLIC _HAS_EXCEPTIONS=0)
    endif()
else()
    target_compile_options(keel_font PRIVATE -Wall -Wextra -Wpedantic)
    if(KEEL_FONT_NO_EXCEPTIONS_RTTI)
        target_compile_options(keel_font PUBLIC -fno-exceptions -fno-rtti)
    endif()
endif()

include(CMakePackageConfigHelpers)
install(TARGETS keel_font EXPORT keel_fontTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/keel DESTINATION include)
install(EXPORT keel_fontTargets
    FILE keel_fontTargets.cmake
    NAMESPACE keel::
    DESTINATION lib/cmake/keel_font
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/keel_fontConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/keel_fontConfig.cmake"
    INSTALL_DESTINATION lib/cmake/keel_font
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/keel_fontConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/keel_fontConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/keel_fontConfigVersion.cmake"
    DESTINATION lib/cmake/keel_font
)

if(KEEL_FONT_BUILD_TESTS)
    enable_testing()

    add_executable(test_font tests/test_font.cpp)
    target_link_libraries(test_font PRIVATE keel::font)
    if(MSVC)
        target_compile_options(test_font PRIVATE /W4 /utf-8)
    else()
        target_compile_options(test_font PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME test_font COMMAND test_font)

    get_target_property(KEEL_CORE_INC_DIRS keel::keel INTERFACE_INCLUDE_DIRECTORIES)
    set(KEEL_CORE_INC_CLEAN "")
    foreach(IDIR IN LISTS KEEL_CORE_INC_DIRS)
        string(REGEX REPLACE "\\$<BUILD_INTERFACE:([^>]+)>" "\\1" IDIR_STRIPPED "${IDIR}")
        if(NOT IDIR_STRIPPED MATCHES "\\$<INSTALL_INTERFACE:" AND EXISTS "${IDIR_STRIPPED}")
            list(APPEND KEEL_CORE_INC_CLEAN "${IDIR_STRIPPED}")
        endif()
    endforeach()
    string(REPLACE ";" "|" KEEL_DEP_INC_JOINED "${KEEL_CORE_INC_CLEAN}")
    string(REPLACE ";" "|" KEEL_STD_INC_JOINED "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")

    add_test(
        NAME test_header_purity
        COMMAND ${CMAKE_COMMAND}
            "-DKEEL_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include"
            "-DKEEL_DEP_INC_DIRS=${KEEL_DEP_INC_JOINED}"
            "-DCOMPILER_PATH=${CMAKE_CXX_COMPILER}"
            "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DSTD_INC_DIRS=${KEEL_STD_INC_JOINED}"
            "-DCMAKE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckHeaderPurity.cmake"
    )
endif()
