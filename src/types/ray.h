//
// Created by super on 27/10/2025.
//

#ifndef RAY_H
#define RAY_H

#include <glm/glm.hpp>

#include <entt/entt.h>

namespace cpt {
    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction;
    };

    struct HitInfo {
        glm::vec3 normal;
        glm::vec2 uv;
        std::optional<glm::vec4> tangent;
        entt::entity material;
        float     t_far,
                  t_near;

        float     uv_density;
        bool      hit;
    };
}

#endif //RAY_H
