cmake_minimum_required(VERSION 3.24)

project(KCD2_AutoWalk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIBKCD2_ROOT "${CMAKE_SOURCE_DIR}/.deps/libKCD2" CACHE PATH "Pinned libKCD2 checkout")
set(AUTOWALK_LIBKCD2_COMMIT "10d20f28faba462c4bf98a01abb48225cc51bb91" CACHE STRING "Expected libKCD2 commit")

if(NOT EXISTS "${LIBKCD2_ROOT}/include/KCSE/KCSEAPI.h")
    message(FATAL_ERROR "libKCD2 not found. Run scripts/bootstrap.ps1 or set LIBKCD2_ROOT.")
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(boost_container CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

file(GLOB_RECURSE LIBKCD2_HEADERS CONFIGURE_DEPENDS "${LIBKCD2_ROOT}/include/*.h")
file(GLOB_RECURSE LIBKCD2_SOURCES CONFIGURE_DEPENDS "${LIBKCD2_ROOT}/src/*.cpp")

add_library(kcd_re STATIC
    "${LIBKCD2_ROOT}/.buildenv/check_sizes.cpp"
    "${LIBKCD2_ROOT}/.buildenv/CrySDKStubs.cpp"
    ${LIBKCD2_SOURCES}
    ${LIBKCD2_HEADERS})

target_precompile_headers(kcd_re PRIVATE "${LIBKCD2_ROOT}/.buildenv/PCH.h")
target_compile_definitions(kcd_re PRIVATE _ITERATOR_DEBUG_LEVEL=0 WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kcd_re PRIVATE /utf-8 /W4 /wd4819 /wd4100)
target_include_directories(kcd_re PRIVATE
    "${LIBKCD2_ROOT}/include"
    "${LIBKCD2_ROOT}/include/CryEngine/CryCommon")
target_link_libraries(kcd_re PUBLIC Boost::container spdlog::spdlog)

file(GLOB_RECURSE AUTOWALK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h")

add_library(KCD2_AutoWalk SHARED ${AUTOWALK_SOURCES})
target_precompile_headers(KCD2_AutoWalk PRIVATE "${LIBKCD2_ROOT}/.buildenv/PCH.h")
target_compile_definitions(KCD2_AutoWalk PRIVATE
    _ITERATOR_DEBUG_LEVEL=0
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    AUTOWALK_LIBKCD2_COMMIT="\"${AUTOWALK_LIBKCD2_COMMIT}\"")
target_compile_options(KCD2_AutoWalk PRIVATE /utf-8 /W4 /wd4819 /wd4100)
target_include_directories(KCD2_AutoWalk PRIVATE
    "${LIBKCD2_ROOT}/include"
    "${LIBKCD2_ROOT}/include/CryEngine/CryCommon"
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(KCD2_AutoWalk PRIVATE kcd_re minhook::minhook spdlog::spdlog)

set_target_properties(KCD2_AutoWalk PROPERTIES
    OUTPUT_NAME "KCD2_AutoWalk"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo")
