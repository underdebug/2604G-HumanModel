cmake_minimum_required(VERSION 3.18)

set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc CACHE FILEPATH "CUDA compiler")
project(test LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86 87 89 90 100 110 120)

find_package(CUDAToolkit REQUIRED)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Quick QuickControls2)
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
add_executable(human_viewer Clothing.cpp Clothing.h ClothSimulation.cpp ClothSimulation.h AppMenu.cpp AppMenu.h Test.cpp Test.h main.cpp MainWindow.cpp MainWindow.h makehuman.cpp makehuman.h
    VulkanWindow.cpp VulkanWindow.h MouseTool.cpp MouseTool.h)
set_target_properties(human_viewer PROPERTIES OUTPUT_NAME test)
qt_add_resources(human_viewer qml_resources PREFIX "/" FILES Main.qml)
target_link_libraries(human_viewer PRIVATE Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Vulkan::Vulkan X11::X11)
set(SHADER_BINARIES)
foreach(NAME mesh.vert mesh.frag overlay.vert overlay.frag)
    set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shader/${NAME}")
    set(BINARY "${CMAKE_CURRENT_BINARY_DIR}/shader/${NAME}.spv")
    add_custom_command(OUTPUT "${BINARY}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shader"
        COMMAND "${GLSLANG_VALIDATOR}" -V --glsl-version 450 -g -Od "${SOURCE}" -o "${BINARY}"
        DEPENDS "${SOURCE}" VERBATIM)
    list(APPEND SHADER_BINARIES "${BINARY}")
endforeach()
add_custom_target(mesh_shaders DEPENDS ${SHADER_BINARIES})
add_dependencies(human_viewer mesh_shaders)

add_custom_command(TARGET human_viewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/models" "$<TARGET_FILE_DIR:human_viewer>/models")

add_library(cloth_backend STATIC CudaCloth.cu CudaCloth.h)
target_include_directories(cloth_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cloth_backend PUBLIC CUDA::cudart)
target_link_libraries(human_viewer PRIVATE cloth_backend)

# Preserve CUDA device symbols and host locals for cuda-gdb.
# Target-scoped options avoid appending flags on repeated configuration.
set_property(TARGET human_viewer cloth_backend PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
foreach(TARGET_NAME human_viewer cloth_backend)
    target_compile_options(${TARGET_NAME} PRIVATE
        "$<$<COMPILE_LANGUAGE:CUDA>:-G;-g;-O0;-Xcompiler=-O0,-g3,-fno-omit-frame-pointer>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-O0;-g3;-fno-omit-frame-pointer>"
    )
endforeach()
