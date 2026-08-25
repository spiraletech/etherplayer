cmake_minimum_required(VERSION 3.21)
project(EtherPlayer VERSION 0.12.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(etherplayer_core
    src/core/PlayerState.cpp
)
target_include_directories(etherplayer_core PUBLIC include)
target_compile_definitions(etherplayer_core PUBLIC ETHERPLAYER_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_compile_definitions(etherplayer_core PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    target_compile_options(etherplayer_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(etherplayer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(ETHERPLAYER_WIN012 ${CMAKE_SOURCE_DIR}/generated/EtherPlayerWin012.cpp)
    add_custom_command(
        OUTPUT ${ETHERPLAYER_WIN012}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/make_v012.py
        DEPENDS ${CMAKE_SOURCE_DIR}/src/win32/EtherPlayerWin.cpp ${CMAKE_SOURCE_DIR}/tools/make_v012.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating EtherPlayer v0.12 Windows shell"
        VERBATIM
    )
    add_executable(EtherPlayer WIN32 ${ETHERPLAYER_WIN012})
    target_include_directories(EtherPlayer PRIVATE include)
    target_compile_definitions(EtherPlayer PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(EtherPlayer PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(EtherPlayer PRIVATE
        etherplayer_core
        ole32 oleaut32 mfplat mfplay mfreadwrite mfuuid
        comdlg32 gdiplus shell32 dwmapi
    )
    set_target_properties(EtherPlayer PROPERTIES OUTPUT_NAME "ETHERPLAYER")
endif()
