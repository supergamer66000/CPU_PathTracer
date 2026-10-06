//
// Created by super on 09/01/2026.
//

#ifndef CPU_PATHTRACER_MODEL_H
#define CPU_PATHTRACER_MODEL_H

#include <optional>
#include <embree4/rtcore.h>

#include "../types/ray.h"

namespace cpt {
    /*
     * Texture data is contained in the material now
     * To get texture data go to glTFLoader::Material base_color_texture
     * that returns a pointer to the said texture
     */
    struct EmbreeUserData {
        entt::entity entity; // The pass for the intersection phase
        RTCScene object_scene;
        bool has_tangents;
    };

    class model {
    public:
        model(RTCDevice device, RTCScene scene, entt::registry& registry);

        void create(entt::entity entity) const;
        void destroy(entt::entity entity) const;

        std::optional<HitInfo> intersect(const Ray& ray) const;
        std::array<std::optional<HitInfo>, 4>  intersect4 (const std::array<Ray, 4>& rays, int active_count = 4)   const;
        std::array<std::optional<HitInfo>, 8>  intersect8 (const std::array<Ray, 8>& rays, int active_count = 8)   const;
        std::array<std::optional<HitInfo>, 16> intersect16(const std::array<Ray, 16>& rays, int active_count = 16) const;

        bool occluded(Ray ray, float max_distance) const;
    private:
        RTCDevice device;
        RTCScene scene;
        entt::registry& registry;

        std::optional<HitInfo> instance(unsigned instID, unsigned geomID, unsigned primID,
                            float u, float v, float tnear, float tfar,glm::vec3 Ng) const;
    };
} // cpt

#endif //CPU_PATHTRACER_MODEL_H
