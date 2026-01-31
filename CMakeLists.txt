cmake_minimum_required(VERSION 3.13)
project(subnets C)

set(SANITIZER
    "off"
    CACHE STRING "off|address|memory|thread|undefined")

set(CMAKE_C_CLANG_TIDY
    clang-tidy "-checks=-*,readability-*,-readability-identifier-length"
    "--warnings-as-errors=-*,readability-*,-readability-identifier-length")

add_compile_options(-Wall -Wextra -Werror -Wpedantic -std=gnu99)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    include_directories(include)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Og -g -fno-omit-frame-pointer)
    add_link_options(-g)

    if(SANITIZER STREQUAL "address")
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    elseif(SANITIZER STREQUAL "memory")
        if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "MemorySanitizer requires Clang")
        endif()
        add_compile_options(-fsanitize=memory -fsanitize-memory-track-origins=2)
        add_link_options(-fsanitize=memory)
    elseif(SANITIZER STREQUAL "undefined")
        add_compile_options(-fsanitize=undefined)
        add_link_options(-fsanitize=undefined)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
endif()

file(GLOB SRC "src/*")
add_executable(${PROJECT_NAME} ${SRC})

install(TARGETS ${PROJECT_NAME} DESTINATION /usr/bin)

find_program(CLANGFORMAT clang-format)
if(CLANGFORMAT)
    set(FORMAT_FILES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
        file(GLOB FORMAT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
        list(APPEND FORMAT_FILES ${FORMAT_SRC})
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        file(GLOB FORMAT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include/*")
        list(APPEND FORMAT_FILES ${FORMAT_INCLUDE})
    endif()
    if(FORMAT_FILES)
        add_custom_target(
            format
            COMMAND ${CLANGFORMAT} -i ${FORMAT_FILES}
            VERBATIM)
        add_dependencies(${PROJECT_NAME} format)
    endif()
endif()
