cmake_minimum_required(VERSION 3.22)
include(FetchContent)

project(vulkan-starter-app LANGUAGES C CXX)

add_executable(${PROJECT_NAME} source/main.cpp source/graphics_internal.cpp
    source/application.cpp source/gpu_resources.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE GLFW_INCLUDE_NONE)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(WIN32)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201 /utf-8)
		target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /entry:mainCRTStartup)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
		target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
	endif()
	target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES NOMINMAX)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

# The four dependencies use the same source revisions as Lab 2.
FetchContent_Declare(glfw SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw")
FetchContent_Declare(vk-bootstrap SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vk-bootstrap")
FetchContent_Declare(VulkanMemoryAllocator SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkanmemoryallocator")
FetchContent_Declare(imgui SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
)

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_sources(${PROJECT_NAME} PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK.")
endif()

set(RUNTIME_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${RUNTIME_DIRECTORY}")
set(SHADER_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spirv")
file(MAKE_DIRECTORY "${SHADER_BUILD_DIRECTORY}")

foreach(SHADER IN ITEMS icosahedron.vert icosahedron.frag background.vert background.frag)
    set(SHADER_BINARY "${SHADER_BUILD_DIRECTORY}/${SHADER}.spv")
    # Relative arguments keep Unicode directory names out of the glslc arguments.
    file(RELATIVE_PATH SHADER_OUTPUT_ARGUMENT "${SHADER_SOURCE_DIRECTORY}" "${SHADER_BINARY}")
    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND "${GLSLC_EXECUTABLE}" "${SHADER}" -o "${SHADER_OUTPUT_ARGUMENT}"
        DEPENDS "${SHADER_SOURCE_DIRECTORY}/${SHADER}"
        WORKING_DIRECTORY "${SHADER_SOURCE_DIRECTORY}"
        VERBATIM
    )
    list(APPEND SHADER_BINARIES "${SHADER_BINARY}")
endforeach()

add_custom_target(runtime_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RUNTIME_DIRECTORY}/shaders" "${RUNTIME_DIRECTORY}/textures"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${SHADER_BINARIES} "${RUNTIME_DIRECTORY}/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/textures" "${RUNTIME_DIRECTORY}/textures"
    DEPENDS ${SHADER_BINARIES}
    VERBATIM
)
add_dependencies(${PROJECT_NAME} runtime_assets)

set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${RUNTIME_DIRECTORY}")
