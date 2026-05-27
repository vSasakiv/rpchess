cmake_minimum_required(VERSION 3.21)

project(E02 LANGUAGES CXX)

# --------------------
# Project layout
# --------------------
set(MAIN_DIR source)
set(SRC_DIR ${MAIN_DIR}/src)
set(INCLUDE_DIR ${MAIN_DIR}/include)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")


add_executable(${PROJECT_NAME} ${SRC_FILES}
        source/src/BufferObjects.hpp
        source/src/Vertexes.hpp
        source/src/Entities/PBRRenderable.hpp
)
target_include_directories(${PROJECT_NAME} PRIVATE "${INCLUDE_DIR}")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

set_source_files_properties(${SRC_DIR}/Libs.cpp PROPERTIES COMPILE_OPTIONS "-g0")

# --------------------
# Vulkan
# --------------------
find_package(Vulkan REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

# --------------------
# Dependencies: GLFW + GLM via FetchContent
# --------------------
include(FetchContent)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
# Deactivate non-essential components of GLFW to speed up the build
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM (header-only)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

target_link_libraries(${PROJECT_NAME} PRIVATE glfw glm::glm)

# --------------------
# Shader compilation (glslc)
# --------------------
set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/source/shaders")
set(SHADER_OUT_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders")

file(GLOB SHADERS CONFIGURE_DEPENDS
  "${SHADER_SRC_DIR}/*.vert"
  "${SHADER_SRC_DIR}/*.frag"
  "${SHADER_SRC_DIR}/*.comp"
)

if(NOT Vulkan_GLSLC_EXECUTABLE)
  find_program(Vulkan_GLSLC_EXECUTABLE glslc)
endif()

if(NOT Vulkan_GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc non trovato. Installa Vulkan SDK o un pacchetto che lo fornisce (shaderc/glslang-tools a seconda della piattaforma).")
endif()

set(SPIRV_OUTPUTS "")
foreach(SHADER ${SHADERS})
  get_filename_component(SHADER_FILE "${SHADER}" NAME)
  set(SPIRV "${CMAKE_CURRENT_BINARY_DIR}/spv/${SHADER_FILE}.spv")

  add_custom_command(
    OUTPUT "${SPIRV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/spv"
    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -o "${SPIRV}" "${SHADER}"
    DEPENDS "${SHADER}"
    COMMENT "Compilo shader: ${SHADER_FILE}"
    VERBATIM
  )
  list(APPEND SPIRV_OUTPUTS "${SPIRV}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_OUTPUTS})
add_dependencies(${PROJECT_NAME} shaders)

# Copio gli SPIR-V subito dopo la compilazione degli shader,
# non nel POST_BUILD dell'eseguibile (che non scatta se cambia solo lo shader)
add_custom_command(TARGET shaders POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${SPIRV_OUTPUTS}
    "${SHADER_OUT_DIR}"
  COMMENT "Copio SPIR-V in: ${SHADER_OUT_DIR}"
)

# --------------------
# Copy assets
# --------------------
set(ASSET_SRC_DIR "${CMAKE_SOURCE_DIR}/source/assets")
set(ASSET_DST_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${ASSET_SRC_DIR}"
    "${ASSET_DST_DIR}"
  COMMENT "Copio asset in: ${ASSET_DST_DIR}"
)