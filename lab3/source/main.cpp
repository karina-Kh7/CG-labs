
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include "graphics_internal.hpp"
#include "application.hpp"

namespace {

    constexpr int32_t default_window_width = 1280;
    constexpr int32_t default_window_height = 720;

    constexpr char default_window_title[] = "CG Lab 3 - Texture Mapping";

    GLFWwindow* glfw_window = nullptr;

    std::filesystem::path assetDirectory(const char* executable) {
#ifdef _WIN32
        wchar_t path[32768];
        const auto length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
        if (length > 0 && length < std::size(path))
            return std::filesystem::path(path, path + length).parent_path();
#elif defined(__linux__)
        std::error_code error;
        const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
        if (!error)
            return path.parent_path();
#endif
        return std::filesystem::absolute(executable).parent_path();
    }

} // namespace

int main(int, char** argv)
{
    int status = EXIT_SUCCESS;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfw_window = glfwCreateWindow(
        default_window_width,
        default_window_height,
        default_window_title,
        nullptr,
        nullptr
    );

    if (glfw_window == nullptr) {
        std::cerr << "Failed to create GLFW window\n";
        status = EXIT_FAILURE;
        goto err_null_window;
    }

    glfwSetFramebufferSizeCallback(
        glfw_window,
        [](GLFWwindow*, int width, int height)
        {
            if (width == 0 || height == 0) {
                return;
            }

            graphics::internal::resize(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            );
        }
    );

    // Создаём контекст ImGui
    if (ImGui::CreateContext() == nullptr) {
        std::cerr << "Failed to create ImGui context\n";
        status = EXIT_FAILURE;
        goto err_imgui_init;
    }

    // Инициализация GLFW backend для ImGui
    if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
        std::cerr
            << "Failed to initialize ImGui GLFW backend for Vulkan renderer\n";

        status = EXIT_FAILURE;
        goto err_imgui_glfw_init;
    }

    // Инициализация Vulkan и Vulkan backend ImGui
    if (!graphics::internal::initialize(glfw_window)) {
        std::cerr << "Failed to initialize graphics\n";
        status = EXIT_FAILURE;
        goto err_graphics_init;
    }

    // Инициализация пользовательской части приложения
    if (!application::initialize(assetDirectory(argv[0]))) {
        std::cerr << "Failed to initialize application\n";
        status = EXIT_FAILURE;
        goto err_application_init;
    }

    while (!glfwWindowShouldClose(glfw_window)) {
        const double time = glfwGetTime();

        glfwPollEvents();

        // Важно:
        // новый кадр должен быть начат и для Vulkan backend,
        // и для GLFW backend.
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        application::update(time);

        ImGui::Render();

        graphics::internal::FrameData fd =
            graphics::internal::prepare();

        application::render(fd);

        graphics::internal::submitAndPresent();
    }

    application::shutdown();

err_application_init:
    graphics::internal::shutdown();

err_graphics_init:
    ImGui_ImplGlfw_Shutdown();

err_imgui_glfw_init:
    ImGui::DestroyContext();

err_imgui_init:
    glfwDestroyWindow(glfw_window);

err_null_window:
    glfwTerminate();

    return status;
}
