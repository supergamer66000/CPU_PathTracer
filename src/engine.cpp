//
// Created by super on 08/10/2025.
//

#include "engine.h"

#include <iostream>
#include <GLFW/glfw3.h>
#include <chrono>

#include "glad/glad.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "input.h"
#include "scene/camera.h"
#include "types/context.h"
#include "util.h"

namespace cpt {
    entt::registry engine::make_registry(EngineContext context) {
        entt::registry registry;
        registry.ctx().emplace<cpt::EngineContext>(context);
        return registry;
    }

    engine::engine() :
        registry(make_registry({rtcNewDevice(nullptr)})),
        scene(registry),
        renderer(registry, 2560, 2000),
        // renderer(registry, 1280, 960),
        ui(registry, &renderer)
    {
        // Check Embree Support //
        {
            static const auto device = registry.ctx().get<EngineContext>().device;
            if (static_cast<bool>(rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_NATIVE_RAY4_SUPPORTED)))
                std::cout << "Instance Ray 4 supported" << std::endl;
            if (static_cast<bool>(rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_NATIVE_RAY8_SUPPORTED)))
                std::cout << "Instance Ray 8 supported" << std::endl;
            if (static_cast<bool>(rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_NATIVE_RAY16_SUPPORTED)))
                std::cout << "Instance Ray 16 supported" << std::endl;
        }

        /*
         *  scene.load() loads the current scene and create a new ptr so
         *  that the renderer can point to whatever is in the scene data
         */
        // scene.add_point_light({-7, 3, 6.5}, {1, 0.8,0.2}, 100, 0.1);
        renderer.set_sky_direction({31, 7, -16});
        // scene.load(cpt::util::path::get_asset_path().append(R"(gltf-research-scenes/scenes/classroom/gltf/classroom_extended.gltf)").string());


        // scene.load(cpt::util::path::get_asset_path().append(R"(main_sponza\NewSponza_Main_glTF_003.gltf)").string());
        // scene.load(cpt::util::path::get_asset_path().append(R"(pkg_d_10k_candles/NewSponza_4_Combined_glTF.gltf)").string());

        scene.load(cpt::util::path::get_asset_path().append(R"(Models\PointLightIntensityTest\glTF-Binary\PointLightIntensityTest.glb)").string());

        renderer.load(&scene);

        renderer.set_scale(0.35f);

        // ImGUI //
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // GLFW //
        glfwSetErrorCallback([](int error_code, const char *description) {
            std::cerr << "GLFW error: " << description << std::endl;
        });

        if (!glfwInit()) {
            return;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_window = glfwCreateWindow(width, height, "CPU Path Tracer", nullptr, nullptr);
        if (!m_window) {
            glfwTerminate();
            exit(EXIT_FAILURE);
        }

        glfwMakeContextCurrent(m_window);
        gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));

        // Set pointer
        glfwSetWindowUserPointer(m_window, this);

        // Setup Key input with glfw
        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            // Get pointer
            if (auto* self = static_cast<class engine*>(glfwGetWindowUserPointer(window))) {
                 if (action == GLFW_RELEASE) {
                     self->input.updateKeys(static_cast<KEY>(key), KEY_STATE::RELEASED);
                 } else if (action == GLFW_REPEAT) {
                     self->input.updateKeys(static_cast<KEY>(key), KEY_STATE::REPEAT);
                 } else if (action == GLFW_PRESS) {
                     self->input.updateKeys(static_cast<KEY>(key), KEY_STATE::PRESSED);
                 }
            }

        });

        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        renderer.init();
    }

    void engine::start() {
        const std::string GPU = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        std::cout << "Selected GPU: " << GPU << std::endl;

        // Draw
        using clock = std::chrono::high_resolution_clock;
        auto lastTime = clock::now();
        double deltaTime = 0.0;

        int frameCount = 0;
        double fpsTimer = 0.0;
        double totalTime = 0.0;

        renderer.update();
        while (!glfwWindowShouldClose(m_window)) {
            auto currentTime = clock::now();
            std::chrono::duration<double> elapsed = currentTime - lastTime;
            deltaTime = elapsed.count();  // seconds since last frame
            lastTime = currentTime;

            frameCount++;
            fpsTimer += deltaTime;
            totalTime += deltaTime;

            renderer.update();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ui.show(scene);

            // ImGUI Update //
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);

            glfwPollEvents();
            glfwSwapBuffers(m_window);
        }
    }

    // Order of deconstruction
    // EMBREE -> IMGUI -> GLFW
    engine::~engine() {
        rtcReleaseDevice(registry.ctx().get<EngineContext>().device);

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
} // cpt
