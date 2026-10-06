//
// Created by super on 10/11/2025.
//
#include "scene.h"

#include "../components.h"
#include "../types/context.h"
#include "../loader/glTFLoader.h"
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <algorithm>

#include "../util.h"

namespace cpt {
    scene::scene(entt::registry& registry)
        : rtc_scene(rtcNewScene(registry.ctx().get<EngineContext>().device)),
          registry(registry),
          model_manager(registry.ctx().get<EngineContext>().device, rtc_scene, registry)
    {
        registry.on_update<components::Position>().connect<&scene::on_transform_change>(this);
        registry.on_update<components::Rotation>().connect<&scene::on_transform_change>(this);
        registry.on_update<Light>().connect<&scene::on_light_change>(this);
        registry.on_update<Material>().connect<&scene::on_light_change>(this);
    }

    void scene::commit(RTCBuildQuality quality) const {
        rtcSetSceneBuildQuality(rtc_scene, quality);
        rtcCommitScene(rtc_scene);
        util::check_embree_error(registry.ctx().get<EngineContext>().device,
            "Commiting Embree Scene");
    }

    std::optional<HitInfo> scene::intersect(const Ray &ray) const {
        return model_manager.intersect(ray);
    }

    std::array<std::optional<HitInfo>, 4> scene::intersect4(const std::array<Ray, 4>& rays) const {
        return model_manager.intersect4(rays);
    }
    std::array<std::optional<HitInfo>, 8> scene::intersect8(const std::array<Ray, 8>& rays) const {
        return model_manager.intersect8(rays);
    }
    std::array<std::optional<HitInfo>, 16> scene::intersect16(const std::array<Ray, 16>& rays) const {
        return model_manager.intersect16(rays);
    }

    bool scene::occluded(Ray ray, float max_distance) const {
        return model_manager.occluded(ray, max_distance);
    }

    void scene::on_transform_change(entt::registry& registry, entt::entity entity) {
        if (const auto* light = registry.try_get<Light>(entity);
            light && light->owner_scene == rtc_scene)
            rebuild_light_cache();

        const auto* mesh = registry.try_get<Mesh>(entity);
        if (!mesh || mesh->embree_info.owner_scene != rtc_scene)
            return;
        update_mesh_transform(entity);
        commit();
        rebuild_light_cache();
    }

    void scene::update_mesh_transform(entt::entity entity) const {
        const auto& mesh = registry.get<Mesh>(entity);
        const auto& pos = registry.get<components::Position>(entity);
        const auto& rot = registry.get<components::Rotation>(entity);
        const auto geometry = rtcGetGeometry(rtc_scene, mesh.embree_info.instance);

        float cx = std::cos(rot.x);
        float sx = std::sin(rot.x);
        float cy = std::cos(rot.y);
        float sy = std::sin(rot.y);
        float cz = std::cos(rot.z);
        float sz = std::sin(rot.z);
        float transform[12] = {
            cy * cz,                 -cy * sz,                sy,       pos.x,
            sx * sy * cz + cx * sz,  -sx * sy * sz + cx * cz, -sx * cy, pos.y,
            -cx * sy * cz + sx * sz, cx * sy * sz + sx * cz,  cx * cy, pos.z
        };

        rtcSetGeometryTransform(
            geometry,
            0,
            RTC_FORMAT_FLOAT3X4_ROW_MAJOR,
            transform
        );

        rtcCommitGeometry(geometry);
    }

    void scene::on_light_change(entt::registry& registry, entt::entity entity) {
        const auto* light = registry.try_get<Light>(entity);
        if (light && light->owner_scene == rtc_scene)
            rebuild_light_cache();
        const auto* mesh = registry.try_get<Mesh>(entity);
        if (mesh && mesh->embree_info.owner_scene == rtc_scene)
            rebuild_light_cache();
    }

    const Material &scene::get_material(entt::entity entity) const {
        const auto* mat = registry.try_get<Material>(entity);
        if (!registry.valid(entity) || !mat)
            throw std::runtime_error("Invalid material entity");
        return *mat;
    }

    glm::vec3 scene::get_emission(entt::entity entity, glm::vec2 uv) const {
        const auto& material = get_material(entity);
        glm::vec3 emission = material.emissive_factor * material.emissive_multiplier;
        if (material.emissive_texture)
            emission *= glm::vec3(mipmaps::sample(get_mipped(*material.emissive_texture), uv.x, uv.y));
        return emission;
    }

    const mipmaps::MippedTexture& scene::get_mipped(size_t texture_index) const {
        auto it = mip_cache.find(texture_index);
        if (it == mip_cache.end()) {
            throw std::out_of_range(
                "No mipped texture cached for texture index " + std::to_string(texture_index));
        }
        return it->second;
    }

    void scene::load(std::string path) {
        auto sys_path = std::filesystem::path(path);
        if (!exists(sys_path)) {
            throw std::runtime_error("Error loading Path: " + path);
        }

        const size_t texture_offset = textures.size();

        auto loader = glTFLoader(path);

        // Patch texture indices BEFORE iterator() hands out material copies,
        // or every entity gets unpatched indices.
        for (auto& mat : loader.get_materials()) {
            if (mat.base_color_texture)
                *mat.base_color_texture += texture_offset;
            if (mat.metallic_roughness_texture)
                *mat.metallic_roughness_texture += texture_offset;
            if (mat.normal_texture)
                mat.normal_texture->texture_index += texture_offset;
            if (mat.occlusion_texture)
                mat.occlusion_texture->texture_index += texture_offset;
            if (mat.emissive_texture)
                *mat.emissive_texture += texture_offset;
            if (mat.transmission_texture)
                *mat.transmission_texture += texture_offset;
        }

        const auto root = registry.create();
        registry.emplace<components::Position>(root, glm::vec3(0.0f));
        registry.emplace<components::Rotation>(root, glm::vec3(0.0f));
        auto& imported = registry.emplace<components::GltfAsset>(root);
        imported.name = sys_path.stem().string();
        imported.path = sys_path.string();

        std::vector<entt::entity> new_entities;
        std::vector<entt::entity> new_lights;

        loader.iterator([this, root, &new_entities](Mesh mesh, Material material) {
            auto entity = registry.create();
            registry.emplace<components::Position>(entity, glm::vec3(0.0f));
            registry.emplace<components::Rotation>(entity, glm::vec3(0.0f));
            registry.emplace<components::GltfChild>(entity, root);
            registry.emplace<Mesh>(entity, std::move(mesh));
            registry.emplace<Material>(entity, std::move(material));
            new_entities.push_back(entity);
        });

        // Load all the light for the scene
        for (const auto light : loader.get_lights()) {
            auto entity = registry.create();
            registry.emplace<components::Position>(entity, light.position);
            float range = 0.2f; // default value
            if (light.range.has_value()) range = *light.range;
            registry.emplace<Light>(entity,
                light.name, light.color, light.intensity, range, rtc_scene);
            registry.emplace<components::GltfChild>(entity, root);
            new_lights.push_back(entity);
            total_point_lights++;
        }

        imported.entities = new_entities;
        imported.entities.insert(imported.entities.end(), new_lights.begin(), new_lights.end());

        auto& new_textures = loader.get_textures();
        textures.insert(textures.end(),
                         std::make_move_iterator(new_textures.begin()),
                         std::make_move_iterator(new_textures.end()));

        // Loading can move the texture vector.
        for (auto& [index, mipped] : mip_cache)
            mipped.base = &textures[index];

        for (size_t i = texture_offset; i < textures.size(); ++i)
            mip_cache.emplace(i, mipmaps::build_mipmaps(&textures[i]));

        for (auto entity : new_entities) {
            model_manager.create(entity);
        }

        commit();
        rebuild_light_cache();
    }

    void scene::unload(entt::entity entity) {
        if (!registry.valid(entity))
            return;
        if (auto* asset = registry.try_get<components::GltfAsset>(entity)) {
            for (const auto child : asset->entities) {
                if (!registry.valid(child))
                    continue;
                if (auto* mesh = registry.try_get<Mesh>(child);
                    mesh && mesh->embree_info.owner_scene == rtc_scene)
                    model_manager.destroy(child);
                registry.destroy(child);
            }
            registry.destroy(entity);
            commit();
            rebuild_light_cache();
            return;
        }
        if (const auto* light = registry.try_get<Light>(entity)) {
            if (light->owner_scene != rtc_scene)
                return;
        }
        if (auto* mesh = registry.try_get<Mesh>(entity)) {
            if (mesh->embree_info.owner_scene != rtc_scene)
                return; // owned by a different scene leave it alone
            model_manager.destroy(entity);
            commit();
        }
        if (const auto* child = registry.try_get<components::GltfChild>(entity);
            child && registry.valid(child->parent)) {
            auto& siblings = registry.get<components::GltfAsset>(child->parent).entities;
            std::erase(siblings, entity);
        }
        registry.destroy(entity);
        rebuild_light_cache();
    }

    void scene::set_asset_position(entt::entity entity, glm::vec3 position) {
        auto* asset = registry.try_get<components::GltfAsset>(entity);
        auto* root_position = registry.try_get<components::Position>(entity);
        if (!asset || !root_position)
            return;

        const glm::vec3 delta = position - glm::vec3(*root_position);
        static_cast<glm::vec3&>(*root_position) = position;
        for (const auto child : asset->entities) {
            if (!registry.valid(child))
                continue;
            auto& child_position = registry.get<components::Position>(child);
            static_cast<glm::vec3&>(child_position) += delta;
            update_mesh_transform(child);
        }
        commit();
        rebuild_light_cache();
    }

    void scene::set_asset_rotation(entt::entity entity, glm::vec3 rotation) {
        auto* asset = registry.try_get<components::GltfAsset>(entity);
        auto* root_rotation = registry.try_get<components::Rotation>(entity);
        if (!asset || !root_rotation)
            return;

        static_cast<glm::vec3&>(*root_rotation) = rotation;
        for (const auto child : asset->entities) {
            if (!registry.valid(child))
                continue;
            auto& child_rotation = registry.get<components::Rotation>(child);
            static_cast<glm::vec3&>(child_rotation) = rotation;
            update_mesh_transform(child);
        }
        commit();
        rebuild_light_cache();
    }

    void scene::add_point_light(glm::vec3 position, glm::vec3 color, float power, float radius) {
        auto entity = registry.create();
        registry.emplace<components::Position>(entity, position);
        registry.emplace<Light>(entity,
            "", color, power, radius, rtc_scene);
        rebuild_light_cache();
    }

    void scene::rebuild_light_cache() {
        light_cache.clear();
        total_light_weight = 0.0f;
        for (auto entity : registry.view<components::Position, Light>()) {
            const auto& light = registry.get<Light>(entity);
            if (light.owner_scene != rtc_scene)
                continue;

            const auto color = glm::max(light.color, glm::vec3(0.0f));
            const float luminance = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
            const float weight = std::max(0.0f, light.power) * luminance;
            if (weight <= 0.0f)
                continue;

            total_light_weight += weight;
            light_cache.push_back({
                registry.get<components::Position>(entity),
                light,
                weight,
                total_light_weight
            });
        }

        area_light_cache.clear();
        total_area_light_weight = 0.0f;
        for (auto entity : registry.view<Mesh, Material, components::Position>()) {
            const auto& mesh = registry.get<Mesh>(entity);
            const auto& material = registry.get<Material>(entity);
            if (mesh.embree_info.owner_scene != rtc_scene)
                continue;

            const glm::vec3 emission = material.emissive_factor * material.emissive_multiplier;
            const float luminance = glm::dot(glm::max(emission, glm::vec3(0.0f)),
                                             glm::vec3(0.2126f, 0.7152f, 0.0722f));
            if (luminance <= 0.0f)
                continue;

            const auto& position = registry.get<components::Position>(entity);
            const auto* rotation = registry.try_get<components::Rotation>(entity);
            const glm::vec3 rot = rotation ? glm::vec3(*rotation) : glm::vec3(0.0f);
            const float cx = std::cos(rot.x), sx = std::sin(rot.x);
            const float cy = std::cos(rot.y), sy = std::sin(rot.y);
            const float cz = std::cos(rot.z), sz = std::sin(rot.z);
            const glm::mat3 transform(
                cy * cz, sx * sy * cz + cx * sz, -cx * sy * cz + sx * sz,
                -cy * sz, -sx * sy * sz + cx * cz, cx * sy * sz + sx * cz,
                sy, -sx * cy, cx * cy
            );

            for (const auto& index : mesh.indices) {
                const glm::vec3 p0 = transform * mesh.vertices[index.x] + glm::vec3(position);
                const glm::vec3 p1 = transform * mesh.vertices[index.y] + glm::vec3(position);
                const glm::vec3 p2 = transform * mesh.vertices[index.z] + glm::vec3(position);
                const glm::vec3 area_vector = glm::cross(p1 - p0, p2 - p0);
                const float area = 0.5f * glm::length(area_vector);
                if (area <= 1e-12f)
                    continue;

                const float weight = area * luminance;
                total_area_light_weight += weight;
                area_light_cache.push_back({
                    p0, p1, p2, glm::normalize(area_vector), emission,
                    mesh.uv[index.x], mesh.uv[index.y], mesh.uv[index.z],
                    material.emissive_texture, area, weight, total_area_light_weight
                });
            }
        }
    }

    std::optional<scene::PointLightSample> scene::sample_point_light(float random) const {
        if (light_cache.empty() || total_light_weight <= 0.0f)
            return std::nullopt;

        const float target = std::clamp(random, 0.0f, 0.99999994f) * total_light_weight;
        const auto it = std::lower_bound(light_cache.begin(), light_cache.end(), target,
            [](const CachedLight& light, float value) {
                return light.cumulative_weight < value;
            });
        return PointLightSample{it->position, it->light, it->weight / total_light_weight};
    }

    std::optional<LightSample> scene::sample_light(glm::vec3 random) const {
        if (area_light_cache.empty() || total_area_light_weight <= 0.0f)
            return std::nullopt;

        const float target = std::clamp(random.z, 0.0f, 0.99999994f) * total_area_light_weight;
        const auto it = std::lower_bound(area_light_cache.begin(), area_light_cache.end(), target,
            [](const CachedAreaLight& light, float value) {
                return light.cumulative_weight < value;
            });

        const float root = std::sqrt(std::clamp(random.x, 0.0f, 1.0f));
        const float b0 = 1.0f - root;
        const float b1 = root * (1.0f - random.y);
        const float b2 = root * random.y;
        glm::vec3 emission = it->emission;
        if (it->texture) {
            const glm::vec2 uv = b0 * it->uv0 + b1 * it->uv1 + b2 * it->uv2;
            emission *= glm::vec3(mipmaps::sample(get_mipped(*it->texture), uv.x, uv.y));
        }

        const float triangle_probability = it->weight / total_area_light_weight;
        return LightSample{
            b0 * it->p0 + b1 * it->p1 + b2 * it->p2,
            it->normal,
            emission,
            triangle_probability / it->area
        };
    }

    scene::~scene() {
        registry.on_update<components::Position>().disconnect<&scene::on_transform_change>(this);
        registry.on_update<components::Rotation>().disconnect<&scene::on_transform_change>(this);
        registry.on_update<Light>().disconnect<&scene::on_light_change>(this);
        registry.on_update<Material>().disconnect<&scene::on_light_change>(this);
        for (auto entity : registry.view<Mesh>())
            model_manager.destroy(entity);
        rtcReleaseScene(this->rtc_scene);
    }
} // cpt
