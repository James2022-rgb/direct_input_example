cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(TARGET_NAME "direct_input_example")

project(${TARGET_NAME}
  VERSION 0.1.0.0
  LANGUAGES CXX
)

option(USE_DIRECTINPUT8CREATE "Use DirectInput8Create, as opposed to CoCreateInstance" ON)
option(USE_UNICODE_CHARACTER_SET "CharacterSet. ON: Unicode(IDirectInput8W) OFF: ANSI(IDirectInput8A)" OFF)

# --------------------------------------------------------------------------------
# External Targets
#

set(EXTERNAL_DIR "external")

#
# Dear ImGui
#

set(IMGUI_DIR "${EXTERNAL_DIR}/imgui")
set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imconfig.h
  ${IMGUI_DIR}/imgui.h
  ${IMGUI_DIR}/imgui_internal.h
  ${IMGUI_DIR}/imstb_rectpack.h
  ${IMGUI_DIR}/imstb_textedit.h
  ${IMGUI_DIR}/imstb_truetype.h
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.h
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.h
)

add_library(dear_imgui STATIC ${IMGUI_SOURCES})
set_target_properties(dear_imgui PROPERTIES FOLDER "dear_imgui")
target_include_directories(dear_imgui PUBLIC ${IMGUI_DIR})


# --------------------------------------------------------------------------------
# Executable
#

add_executable(${TARGET_NAME})

set(SOURCE_DIR ".")
set(SOURCES
  ${SOURCE_DIR}/direct_input_context.cpp
  ${SOURCE_DIR}/direct_input_context.h
  ${SOURCE_DIR}/main.cpp
)


if(USE_DIRECTINPUT8CREATE)
  target_compile_definitions(${TARGET_NAME} PRIVATE CONFIG_USE_DIRECTINPUT8CREATE=1)
else()
  target_compile_definitions(${TARGET_NAME} PRIVATE CONFIG_USE_DIRECTINPUT8CREATE=0)
endif()
if(USE_UNICODE_CHARACTER_SET)
  target_compile_definitions(${TARGET_NAME} PRIVATE _UNICODE)
endif()

target_sources(${TARGET_NAME} PRIVATE ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE
  dear_imgui
)
