//
// Created by super on 10/11/2025.
//

#ifndef WORLD_H
#define WORLD_H

#include <embree4/rtcore_scene.h>
#include <entt/entt.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "model.h"

#include "../types/ray.h"
#include "../types/types.h"
#include "../rendering/mipmaps.h"

namespace cpt {

class scene {
public:
    scene(entt::registry &registry);
    ~scene();
    scene(const scene&) = delete;
    scene& operator=(const scene&) = delete;

    RTCScene rtc_scene;
    void load(std::string path);
    // Requires idle render workers. Use renderer::unload for a live scene.
    void unload(entt::entity entity);
    void set_asset_position(entt::entity asset, glm::vec3 position);
    void set_asset_rotation(entt::entity asset, glm::vec3 rotation);
    void commit(RTCBuildQuality quality = RTC_BUILD_QUALITY_MEDIUM) const;

    std::optional<HitInfo> intersect(const Ray &ray) const;
    std::array<std::optional<HitInfo>, 4>  intersect4 (const std::array<Ray, 4>&  rays) const;
    std::array<std::optional<HitInfo>, 8>  intersect8 (const std::array<Ray, 8>&  rays) const;
    std::array<std::optional<HitInfo>, 16> intersect16(const std::array<Ray, 16>& rays) const;

    const int get_total_point_lights() { return total_point_lights; }

    bool occluded(Ray ray, float max_distance) const;

    void add_point_light(glm::vec3 position,
        glm::vec3 color = {1.f, 1.f, 1.f},
        float power = 1.f,
        float radius = 0.2f
    );
    struct PointLightSample {
        glm::vec3 position;
        Light light;
        float probability;
    };
    std::optional<PointLightSample> sample_point_light(float random) const;
    std::optional<LightSample> sample_light(glm::vec3 random) const;


    const Material& get_material(entt::entity entity) const;
    glm::vec3 get_emission(entt::entity entity, glm::vec2 uv) const;
    [[maybe_unused]] const mipmaps::MippedTexture& get_mipped(size_t texture_index) const;
private:
    entt::registry &registry;
    entt::entity active_camera;

    int total_point_lights;

    model model_manager;

    std::vector<Texture> textures;
    std::unordered_map<size_t, mipmaps::MippedTexture> mip_cache;

    void on_transform_change(entt::registry& registry, entt::entity entity);
    void on_light_change(entt::registry& registry, entt::entity entity);
    void update_mesh_transform(entt::entity entity) const;

    struct CachedLight {
        glm::vec3 position;
        Light light;
        float weight;
        float cumulative_weight;
    };
    std::vector<CachedLight> light_cache;
    float total_light_weight = 0.0f;

    struct CachedAreaLight {
        glm::vec3 p0;
        glm::vec3 p1;
        glm::vec3 p2;
        glm::vec3 normal;
        glm::vec3 emission;
        glm::vec2 uv0;
        glm::vec2 uv1;
        glm::vec2 uv2;
        std::optional<size_t> texture;
        float area;
        float weight;
        float cumulative_weight;
    };
    std::vector<CachedAreaLight> area_light_cache;
    float total_area_light_weight = 0.0f;
    void rebuild_light_cache();};
} // cpt

#endif //WORLD_H
