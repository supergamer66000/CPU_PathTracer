//
// Created by super on 08/11/2025.
//

#ifndef COMPONENTS_H
#define COMPONENTS_H

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "scene/camera.h"
#include "loader/glTFLoader.h"

namespace cpt::components {
    struct Position : glm::vec3 {
        using glm::vec3::vec3;

        //Position() = default;
        explicit Position(const glm::vec3& v) : glm::vec3(v) {}
    };

    struct Rotation : glm::vec3 {
        using glm::vec3::vec3;

        //Position() = default;
        explicit Rotation(const glm::vec3& v) : glm::vec3(v) {}
    };

    struct GltfAsset {
        std::string name;
        std::string path;
        std::vector<entt::entity> entities;
        // std::vector<ImportedLight> lights;
        // std::vector<ImportedCamera> cameras;
    };

    struct GltfChild {
        entt::entity parent = entt::null;
    };
}

#endif //COMPONENTS_H
