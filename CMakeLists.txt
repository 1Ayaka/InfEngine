cmake_minimum_required(VERSION 3.22)
project(InfEngine LANGUAGES CXX)

# ------------------------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Paths and Directories
# ------------------------------------------------------------------------------
set(PYTHON_TARGET_DIR "${CMAKE_SOURCE_DIR}/python/InfEngine/lib")

# ------------------------------------------------------------------------------
# Source Files
# ------------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS cpp/*.cpp cpp/*.h)
file(GLOB_RECURSE BINDING_SOURCES CONFIGURE_DEPENDS cpp/infengine/tools/pybinding/*.cpp)

# ------------------------------------------------------------------------------
# Header Include Directories
# ------------------------------------------------------------------------------
file(GLOB_RECURSE INFENGINE_HEADERS "${CMAKE_SOURCE_DIR}/cpp/*.h")
foreach(_hdr ${INFENGINE_HEADERS})
    get_filename_component(_hdr_dir "${_hdr}" DIRECTORY)
    list(APPEND INFENGINE_INCLUDE_DIRS "${_hdr_dir}")
endforeach()
list(REMOVE_DUPLICATES INFENGINE_INCLUDE_DIRS)

# ------------------------------------------------------------------------------
# pybind11 Module Definition
# ------------------------------------------------------------------------------
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

pybind11_add_module(_InfEngine MODULE
    ${ENGINE_SOURCES}
    ${BINDING_SOURCES}
)

target_include_directories(_InfEngine PRIVATE
    ${INFENGINE_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/external
)

# Set UTF-8 encoding for MSVC to avoid C4819 warnings
if(MSVC)
    target_compile_options(_InfEngine PRIVATE /utf-8)
endif()

# ------------------------------------------------------------------------------
# SPIRV-Cross (from Vulkan SDK)
# ------------------------------------------------------------------------------
# SPIRV-Cross is included with the Vulkan SDK for shader reflection
if(DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
    target_include_directories(_InfEngine PRIVATE ${VULKAN_SDK_PATH}/Include)
    
    # Link SPIRV-Cross libraries for shader reflection
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_libraries(_InfEngine PRIVATE
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-cored.lib
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-glsld.lib
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-cppd.lib
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-reflectd.lib
        )
    else()
        target_link_libraries(_InfEngine PRIVATE
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-core.lib
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-glsl.lib
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-cpp.lib
            ${VULKAN_SDK_PATH}/Lib/spirv-cross-reflect.lib
        )
    endif()
else()
    message(WARNING "VULKAN_SDK environment variable not set. SPIRV-Cross reflection will not be available.")
endif()

# GLM: Use Vulkan-compatible [0,1] depth range instead of OpenGL [-1,1]
target_compile_definitions(_InfEngine PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(_InfEngine PRIVATE
    assimp::assimp
    glslang::glslang
    SDL3::SDL3
    glm::glm
    imgui
    stb
    Vulkan::Vulkan
    Jolt
    GPUOpen::VulkanMemoryAllocator
)

# ------------------------------------------------------------------------------
# External Dependencies
# ------------------------------------------------------------------------------
add_subdirectory(external)

# ------------------------------------------------------------------------------
# Pre-Build Cleanup: remove python __pycache__ directories recursively
# ------------------------------------------------------------------------------
add_custom_target(clean_python_pycache
    COMMAND ${CMAKE_COMMAND}
            -DPYTHON_DIR=${CMAKE_SOURCE_DIR}/python
            -P ${CMAKE_SOURCE_DIR}/cmake/clean_python_pycache.cmake
    COMMENT "Removing __pycache__ directories under python/ before build"
)

# ------------------------------------------------------------------------------
# Post-Build Steps
# ------------------------------------------------------------------------------

# Create a custom script to copy all DLL dependencies
configure_file(
    "${CMAKE_SOURCE_DIR}/copy_dependencies.cmake.in"
    "${CMAKE_BINARY_DIR}/copy_dependencies.cmake"
    @ONLY
)

add_custom_command(TARGET _InfEngine POST_BUILD
    # Copy the main .pyd file
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:_InfEngine>"
        "${PYTHON_TARGET_DIR}/$<TARGET_FILE_NAME:_InfEngine>"
    
    # Copy all DLL dependencies automatically
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/copy_dependencies.cmake"
    
    COMMENT "Copy _InfEngine.pyd and all dependent DLLs to python/InfEngine/lib"
)

# ------------------------------------------------------------------------------
# Code Formatting Target (clang-format)
# ------------------------------------------------------------------------------
file(GLOB_RECURSE ALL_CXX_SOURCE_FILES 
    "${CMAKE_SOURCE_DIR}/cpp/*.cpp"
    "${CMAKE_SOURCE_DIR}/cpp/*.h"
    "${CMAKE_SOURCE_DIR}/cpp/*.hpp"
)

add_custom_target(clang_format
    COMMAND clang-format -i ${ALL_CXX_SOURCE_FILES}
    COMMENT "Running clang-format on source files"
)

# ------------------------------------------------------------------------------
# Python Packaging and Installation
# ------------------------------------------------------------------------------
add_custom_target(package_and_install_python ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Building Python package via 'python -m build'..."
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}" 
            python -m build --no-isolation

    COMMAND ${CMAKE_COMMAND} -E echo "Installing built wheel via pip..."
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}" 
            ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/install_wheel.cmake"

    COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up .egg-info directory..."
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_SOURCE_DIR}/python/InfEngine.egg-info"

    COMMAND ${CMAKE_COMMAND} -E echo "Cleaning __pycache__ directories after packaging..."
    COMMAND ${CMAKE_COMMAND}
            -DPYTHON_DIR=${CMAKE_SOURCE_DIR}/python
            -P ${CMAKE_SOURCE_DIR}/cmake/clean_python_pycache.cmake

    COMMENT "Python packaging and installation after native module build"
)

add_dependencies(package_and_install_python _InfEngine)

# ------------------------------------------------------------------------------
# API Reference Generation (auto-introspect Python package → Markdown)
# ------------------------------------------------------------------------------
add_custom_target(generate_api_docs
    COMMAND ${CMAKE_COMMAND} -E echo "Generating API reference docs..."
    COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
            python "${CMAKE_SOURCE_DIR}/docs/wiki/generate_api_docs.py"
    COMMENT "Auto-generate API reference Markdown from Python stubs"
)

# ------------------------------------------------------------------------------
# Wiki Build (MkDocs)
# ------------------------------------------------------------------------------
add_custom_target(build_wiki_html
    COMMAND ${CMAKE_COMMAND} -E echo "Building MkDocs wiki..."
    COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
            python -m mkdocs build --clean -f "${CMAKE_SOURCE_DIR}/docs/wiki/mkdocs.yml"
            -d "${CMAKE_SOURCE_DIR}/docs/wiki/site"
    COMMENT "Generate wiki HTML into docs/wiki/site"
)
add_dependencies(build_wiki_html generate_api_docs)

# Ensure cleanup runs before any local build target compiles.
function(infengine_add_preclean_dependency _dir _dep_target)
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        if(_target STREQUAL "${_dep_target}")
            continue()
        endif()

        get_target_property(_is_imported ${_target} IMPORTED)
        if(_is_imported)
            continue()
        endif()

        get_target_property(_target_type ${_target} TYPE)
        if(_target_type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()

        add_dependencies(${_target} ${_dep_target})
    endforeach()

    get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        infengine_add_preclean_dependency("${_subdir}" "${_dep_target}")
    endforeach()
endfunction()

infengine_add_preclean_dependency("${CMAKE_SOURCE_DIR}" clean_python_pycache)
