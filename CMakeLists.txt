cmake_minimum_required(VERSION 3.16)
project(orbital-frontier C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# macOS Metal backend requires Objective-C for Sokol
if(APPLE)
    enable_language(OBJC)
endif()

set(SOKOL_DIR ${CMAKE_SOURCE_DIR}/third_party/sokol)

# Platform detection for Sokol backend
if(EMSCRIPTEN)
    set(SOKOL_BACKEND "SOKOL_GLES3")
elseif(WIN32)
    set(SOKOL_BACKEND "SOKOL_D3D11")
elseif(APPLE)
    set(SOKOL_BACKEND "SOKOL_METAL")
else()
    set(SOKOL_BACKEND "SOKOL_GLCORE")
endif()

# Sokol implementation library (single compilation unit)
add_library(sokol STATIC src/sokol_impl.c)
target_include_directories(sokol PUBLIC ${SOKOL_DIR})
target_compile_definitions(sokol PUBLIC ${SOKOL_BACKEND})
if(APPLE)
    set_source_files_properties(src/sokol_impl.c PROPERTIES LANGUAGE OBJC)
endif()

if(WIN32)
    target_link_libraries(sokol PUBLIC kernel32 user32 gdi32 d3d11 dxgi dxguid ole32)
elseif(APPLE)
    target_link_libraries(sokol PUBLIC
        "-framework Metal" "-framework Cocoa" "-framework QuartzCore"
        "-framework MetalKit")
elseif(UNIX AND NOT EMSCRIPTEN)
    target_link_libraries(sokol PUBLIC X11 Xi Xcursor GL dl pthread m)
endif()

# Main executable
add_executable(orbital-frontier
    src/main.c
    src/camera.c
    src/render.c
    src/lod.c
    src/terrain_noise.c
    src/job_system.c
)
if(APPLE)
    # Add macOS-specific sources here if needed
endif()

target_include_directories(orbital-frontier PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/shaders
    ${SOKOL_DIR}
)

target_link_libraries(orbital-frontier PRIVATE sokol)

if(WIN32)
    set_target_properties(orbital-frontier PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ---- Shader compilation with sokol-shdc ----
set(SOKOL_SHDC "${CMAKE_SOURCE_DIR}/tools/sokol-shdc${CMAKE_EXECUTABLE_SUFFIX}")
set(SHADER_SLANG "hlsl5:glsl430:glsl300es:metal_macos:wgsl")
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")

if(EXISTS ${SOKOL_SHDC})
    file(GLOB SHADER_SOURCES "${SHADER_DIR}/*.glsl")
    set(SHADER_HEADERS)

    foreach(SHADER_SRC ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
        set(SHADER_OUT "${SHADER_DIR}/${SHADER_NAME}.h")
        list(APPEND SHADER_HEADERS ${SHADER_OUT})

        add_custom_command(
            OUTPUT ${SHADER_OUT}
            COMMAND ${SOKOL_SHDC}
                --input ${SHADER_SRC}
                --output ${SHADER_OUT}
                --slang ${SHADER_SLANG}
            DEPENDS ${SHADER_SRC}
            COMMENT "Compiling shader: ${SHADER_NAME}"
            VERBATIM
        )
    endforeach()

    add_custom_target(shaders DEPENDS ${SHADER_HEADERS})
    add_dependencies(orbital-frontier shaders)
else()
    message(STATUS "sokol-shdc not found — using pre-committed shader headers")
endif()

# Emscripten web build settings
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    target_link_options(orbital-frontier PRIVATE
        -sUSE_WEBGL2=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sTOTAL_STACK=8388608
    )
endif()

if(MSVC)
    target_compile_options(orbital-frontier PRIVATE /W3)
    target_compile_options(sokol PRIVATE /W3)
else()
    target_compile_options(orbital-frontier PRIVATE -Wall -Wextra)
    target_compile_options(sokol PRIVATE -Wall -Wextra)
endif()
