cmake_minimum_required(VERSION 3.20)

add_library(vterm STATIC
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)

target_include_directories(vterm
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(vterm PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_definitions(vterm PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
    target_compile_options(vterm PRIVATE /W3)
else()
    target_compile_options(vterm PRIVATE -Wall -Wno-unused-parameter)
endif()
