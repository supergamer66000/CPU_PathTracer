cmake_minimum_required(VERSION 3.30)
project(CPU_PathTracer)
set(CMAKE_CXX_STANDARD 23)

add_subdirectory(external)
find_package(embree REQUIRED)
find_package(OpenImageDenoise REQUIRED CONFIG)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(CPU_PathTracer src/main.cpp
        src/engine.cpp
        src/engine.h
        src/scene/camera.cpp
        src/scene/camera.h
        src/input.cpp
        src/input.h
        src/types/ray.h
        src/components.h
        src/rendering/intersections.h
        src/scene/scene.cpp
        src/scene/scene.h
        src/rendering/renderer.cpp
        src/rendering/renderer.h
        src/util.h
        src/util.cpp
        src/types/context.h
        src/scene/model.cpp
        src/scene/model.h
        src/logger.cpp
        src/logger.h
        src/loader/glTFLoader.h
        src/loader/glTFLoader.cpp
        src/rendering/ui.h
        src/rendering/ui.cpp
        src/rendering/mipmaps.h
        src/random.h
        src/rendering/brdf.h
        src/types/types.h
        src/rendering/sample.h
        src/rendering/phase.h
)

target_precompile_headers(CPU_PathTracer PRIVATE src/pch.h)

target_link_libraries(CPU_PathTracer
        PRIVATE glfw glad imgui_impl glm ${EMBREE_LIBRARIES} fastgltf::fastgltf OpenImageDenoise
)

target_compile_definitions(CPU_PathTracer PRIVATE NOMINMAX)

target_compile_definitions(CPU_PathTracer PUBLIC GLFW_INCLUDE_NONE)
target_compile_definitions(CPU_PathTracer PUBLIC GLM_ENABLE_EXPERIMENTAL)

target_include_directories(CPU_PathTracer PUBLIC
        external
        external/entt
        external/stb
        ${EMBREE_INCLUDE_DIRS}
)

# Development builds read assets in place; copying the entire library on every
# relink can dwarf compilation time. Enable this for a portable build folder.
option(CPT_COPY_ASSETS "Copy assets beside the executable after linking" OFF)
if(CPT_COPY_ASSETS)
    add_custom_command(TARGET CPU_PathTracer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:CPU_PathTracer>/assets"
            VERBATIM
    )
else()
    target_compile_definitions(CPU_PathTracer PRIVATE
            CPT_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets")
endif()
