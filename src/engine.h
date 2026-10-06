//
// Created by super on 08/10/2025.
//

#ifndef ENGINE_H
#define ENGINE_H

#include <entt/entt.h>

struct GLFWwindow;

#include "scene/scene.h"
#include "input.h"
#include "rendering/renderer.h"
#include "rendering/ui.h"

namespace cpt {
struct EngineContext;
class engine {
public:
    engine();
    ~engine();

    int width = 1920;
    int height = 1080;

    input input;

    void start();
private:
    static entt::registry make_registry(EngineContext context);

    entt::registry registry{}; // Creation of registry
    GLFWwindow *m_window;

    scene scene;
    renderer renderer;
    ui ui;
};

} // cpt

#endif //ENGINE_H
