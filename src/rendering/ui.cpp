//
// Created by super on 27/05/2026.
//

#include "ui.h"

#include "../components.h"
#include "imgui.h"
#include "renderer.h"
#include "../scene/scene.h"
#include "../util.h"
#include "../types/types.h"

#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <string>
#include <iostream>

namespace cpt {
    ui::ui(entt::registry &registry,
            class renderer* renderer) : reg(registry), renderer(renderer) {
    }

    void ui::show(scene &scene) {
        {
            if (!renderer) { return; }
            ImGui::DockSpaceOverViewport();

            ImGui::Begin("Renderer");

            ImGui::Checkbox("Ray Trace", &renderer->ray_trace);
            if (renderer->ray_trace) {
                ImGui::SameLine();
                ImGui::Checkbox("Accumulate", &renderer->accumulate);
                ImGui::SameLine();
                ImGui::Checkbox("Denoise", &renderer->denoise);
            }

            static float scale = renderer->get_scale();
            if (ImGui::SliderFloat("Render Scale", &scale, 0.05f, 1.0f, "%.2f"))
                renderer->set_scale(scale);

            static auto size = renderer->get_renderer_size();
            if (ImGui::InputInt2("Resolution", size))
                renderer->set_renderer_size(size);

            ImGui::DragFloat("Light Scale", &renderer->light_scale, 0.00001);


            ImGui::Separator();

            if (ImGui::SliderInt("Bounces", &renderer->max_bounces, 1, 32))
                renderer->reset_accumulation();

            static const char* items[] = { "Sample 1", "Sample 4", "Sample 8", "Sample 16" };
            int index = static_cast<int>(renderer->samples_per_pixel);
            if (ImGui::Combo("Samples", &index, items, IM_ARRAYSIZE(items))) {
                renderer->samples_per_pixel = static_cast<renderer::RaySamples>(index);
                renderer->reset_accumulation();
            }

            if (ImGui::CollapsingHeader("Sky Options")) {
                static bool sky_checkbox = renderer->get_sky_enabled();
                if (ImGui::Checkbox("Sky", &sky_checkbox)) {
                    renderer->set_use_sky(sky_checkbox);
                }
                ImGui::SameLine();

                // Edit sun intensity
                static float i_intensity = renderer->get_sky_intensity();
                if (ImGui::DragFloat("Sky Intensity", &i_intensity, 0.05f))
                    renderer->set_sky_intensity(i_intensity);

                // Edit Sun angular radius
                static float i_angle = renderer->get_sky_angular_radius();
                if (ImGui::SliderAngle("Sky Angular Radius", &i_angle, 0.f, 180.f))
                    renderer->set_sky_angular_radius(i_angle);

                // Edit Sun color
                static glm::vec3 i_color = renderer->get_sky_color();
                if (ImGui::ColorEdit3("Sky Color", glm::value_ptr(i_color)))
                    renderer->set_sky_color(i_color);

                // Edit Sun direction
                static glm::vec3 i_direction = renderer->get_sky_direction();
                if (ImGui::DragFloat3("Sky Direction", glm::value_ptr(i_direction)))
                    renderer->set_sky_direction(i_direction);
            }

            if (ImGui::CollapsingHeader("Fog Options")) {
                static bool fog_checkbox = renderer->get_fog_enabled();
                if (ImGui::Checkbox("Fog", &fog_checkbox))
                    renderer->set_fog_enabled(fog_checkbox);

                static float i_density = renderer->get_fog_density();
                if (ImGui::DragFloat("Fog Density", &i_density, 0.001f, 0.0f, 5.0f))
                    renderer->set_fog_density(i_density);

                static glm::vec3 i_albedo = renderer->get_fog_albedo();
                if (ImGui::ColorEdit3("Fog Albedo", glm::value_ptr(i_albedo)))
                    renderer->set_fog_albedo(i_albedo);

                static float i_g = renderer->get_fog_anisotropy();
                if (ImGui::SliderFloat("Fog Anisotropy (g)", &i_g, -0.99f, 0.99f))
                    renderer->set_fog_anisotropy(i_g);
            }

            if (ImGui::CollapsingHeader("Post Processing")) {
                ImGui::Checkbox("Post Processing Options", &renderer->post_settings.HDR);
            }

            if (ImGui::CollapsingHeader("Camera")) {
                // static auto camera_pos = renderer->get_camera()->position;
                ImGui::InputFloat3("Camera Position", glm::value_ptr(renderer->get_camera()->position));
                static auto camera_speed =  renderer->get_camera()->move_speed;
                if (ImGui::DragFloat("Camera Speed", &camera_speed, 0.001f))
                    renderer->get_camera()->move_speed = camera_speed;
            }

            ImGui::End();
        }

        this->scene_info(scene);
        this->view_port();
    }

    void ui::view_port() {
        ImGui::Begin("Viewport");

        static auto [width, height] = renderer->get_renderer_size();
        const float aspect = static_cast<float>(renderer->get_renderer_size().width) /
                             static_cast<float>(renderer->get_renderer_size().height);

        float display_height = width / aspect;

        ImGui::Image(
            static_cast<ImTextureID>(static_cast<intptr_t>(renderer->get_screen_texture_id())),
            ImVec2(width, display_height),
            ImVec2(0, 1),
            ImVec2(1, 0)
        );

        ImGui::End();
    }

    // Renders a single row in the scene tree. Left-click selects (drives the
    // inspector panel), right-click opens a context menu with quick actions.
    void ui::scene_tree_node(scene& scene, entt::entity entity, entt::entity& to_unload) {
        auto* mesh = reg.try_get<Mesh>(entity);
        auto* light = reg.try_get<Light>(entity);

        std::string label;
        if (mesh && !mesh->name.empty())
            label = mesh->name;
        if (light)
            label = "Light " + std::to_string(static_cast<uint32_t>(entity));

        ImGui::PushID(static_cast<int>(entity));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                  | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected_entity == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        if (ImGui::IsItemClicked())
            selected_entity = entity;

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Unload")) {
                to_unload = entity;
            }
            ImGui::EndPopup();
        }

        if (open) {
            if (mesh) {
                ImGui::Text("Instance ID: %u", mesh->embree_info.instance);
                ImGui::Text("Geometry ID: %u", mesh->embree_info.geometry);
                ImGui::Text("Scene Ptr: %p", mesh->embree_info.object_scene);
            }
            if (light) {
                ImGui::Text("Color: (%.2f, %.2f, %.2f)", light->color.r, light->color.g, light->color.b);
                ImGui::Text("Power: %.2f", light->power);
            }
            if (ImGui::SmallButton("Unload")) {
                to_unload = entity;
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void ui::scene_tree_asset(scene& scene, entt::entity entity, entt::entity& to_unload) {
        auto& asset = reg.get<components::GltfAsset>(entity);
        ImGui::PushID(static_cast<int>(entity));

        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected_entity == entity)
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(asset.name.c_str(), flags);
        if (ImGui::IsItemClicked())
            selected_entity = entity;

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Unload glTF"))
                to_unload = entity;
            ImGui::EndPopup();
        }

        if (open) {
            for (const auto child : asset.entities) {
                if (reg.valid(child))
                    scene_tree_node(scene, child, to_unload);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // Details panel for whatever's currently selected in the scene tree.
    void ui::inspector_panel(scene& scene) {
        ImGui::Begin("Inspector");

        if (!reg.valid(selected_entity)) {
            ImGui::TextDisabled("No entity selected.");
            ImGui::End();
            return;
        }

        ImGui::Text("Entity: %u", static_cast<uint32_t>(selected_entity));
        ImGui::Separator();

        // --- Transform ---
        auto* pos = reg.try_get<components::Position>(selected_entity);
        auto* rot = reg.try_get<components::Rotation>(selected_entity);
        if (pos || rot) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (pos) {
                    glm::vec3 edited_pos = *pos;
                    if (ImGui::DragFloat3("Position", glm::value_ptr(edited_pos), 0.01f)) {
                        if (reg.all_of<components::GltfAsset>(selected_entity))
                            renderer->set_asset_position(scene, selected_entity, edited_pos);
                        else
                            renderer->set_position(selected_entity, edited_pos);
                    }
                }

                if (rot) {
                    glm::vec3 edited_rot = *rot;
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(edited_rot), 0.01f)) {
                        if (reg.all_of<components::GltfAsset>(selected_entity))
                            renderer->set_asset_rotation(scene, selected_entity, edited_rot);
                        else
                            renderer->set_rotation(selected_entity, edited_rot);
                    }
                }
            }
        }

        if (auto* asset = reg.try_get<components::GltfAsset>(selected_entity)) {
            if (ImGui::CollapsingHeader("glTF", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("%s", asset->path.c_str());
                ImGui::Text("Objects: %zu", asset->entities.size());
                // ImGui::Text("Lights: %zu", asset->lights.size());
                // ImGui::Text("Cameras: %zu", asset->cameras.size());
            }
        }

        // --- Lights --
        if (auto* light = reg.try_get<Light>(selected_entity)) {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                glm::vec3 color = light->color;
                float power = light->power;
                float radius = light->radius;
                const bool color_changed = ImGui::ColorEdit3("Color", glm::value_ptr(color));
                const bool power_changed = ImGui::DragFloat("Power", &power, 0.1f, 0.0f);
                const bool radius_changed = ImGui::DragFloat("Radius", &radius, 0.1f, 0.0f);
                if (color_changed || power_changed || radius_changed)
                    renderer->set_light(selected_entity, color, power, radius);
            }
        }

        // --- Mesh ---
        if (auto* mesh = reg.try_get<Mesh>(selected_entity)) {
            if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                char name_buf[128];
                std::snprintf(name_buf, sizeof(name_buf), "%s", mesh->name.c_str());
                if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
                    mesh->name = name_buf;
                }
                ImGui::Text("Vertices: %zu", mesh->vertices.size());
                ImGui::Text("Triangles: %zu", mesh->indices.size());
                ImGui::Text("UV Coords: %zu", mesh->uv.size());
                if (mesh->tangents)
                    ImGui::Text("Tangents: %zu", mesh->tangents.value().size());
            }
        }

        // --- Material ---
        if (auto* material = reg.try_get<Material>(selected_entity)) {
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                Material edited = *material;
                bool material_changed = false;
                material_changed |= ImGui::ColorEdit4("Base Color", glm::value_ptr(edited.base_color_factor));
                material_changed |= ImGui::SliderFloat("Metallic", &edited.metallic_factor, 0.0f, 1.0f);
                material_changed |= ImGui::SliderFloat("Roughness", &edited.roughness_factor, 0.0f, 1.0f);
                material_changed |= ImGui::DragFloat("Outside IOR", &edited.eta_i, 0.01f, 1.0f, 5.0f);
                material_changed |= ImGui::DragFloat("Inside IOR", &edited.eta_t, 0.01f, 1.0f, 5.0f);
                material_changed |= ImGui::SliderFloat("Transmission", &edited.transmission_factor, 0.0f, 1.0f);
                material_changed |= ImGui::ColorEdit3("Emissive", glm::value_ptr(edited.emissive_factor));
                material_changed |= ImGui::DragFloat("Emissive Multiplier", &edited.emissive_multiplier);

                static const char* alpha_modes[] = { "Opaque", "Mask", "Blend" };
                int current_mode = edited.alpha_mode;
                if (ImGui::Combo("Alpha Mode", &current_mode, alpha_modes, IM_ARRAYSIZE(alpha_modes))) {
                    edited.alpha_mode = static_cast<AlphaMode>(current_mode);
                    material_changed = true;
                }
                if (edited.alpha_mode == AlphaMode::Mask) {
                    material_changed |= ImGui::SliderFloat(
                        "Alpha Cutoff", &edited.alphaz_cutoff, 0.0f, 1.0f);
                }
                material_changed |= ImGui::Checkbox("Double Sided", &edited.double_sided);
                if (material_changed)
                    renderer->set_material(selected_entity, edited);

                ImGui::Separator();
                ImGui::TextDisabled("Textures (read-only)");

                {
                    auto opt_label = [](const std::optional<size_t>& idx) {
                        return idx.has_value() ? ("#" + std::to_string(*idx)) : std::string("None");
                    };
                    const std::string base_color_label = opt_label(material->base_color_texture);
                    const std::string mr_label = opt_label(material->metallic_roughness_texture);
                    const std::string emissive_label = opt_label(material->emissive_texture);
                    const std::string normal_label = material->normal_texture
                        ? ("#" + std::to_string(material->normal_texture->texture_index))
                        : std::string("None");
                    const std::string occlusion_label = material->occlusion_texture
                        ? ("#" + std::to_string(material->occlusion_texture->texture_index))
                        : std::string("None");
                    const std::string transmission_label = material->transmission_texture
                        ? ("#" + std::to_string(*material->transmission_texture))
                        : std::string("None");

                    ImGui::Text("Base Color: %s", base_color_label.c_str());
                    ImGui::Text("Metallic/Roughness: %s", mr_label.c_str());
                    ImGui::Text("Emissive: %s", emissive_label.c_str());
                    ImGui::Text("Normal: %s", normal_label.c_str());
                    ImGui::Text("Occlusion: %s", occlusion_label.c_str());
                    ImGui::Text("Transmission: %s", transmission_label.c_str());
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Unload Entity", ImVec2(-1, 0))) {
            renderer->unload(scene, selected_entity);
            selected_entity = entt::null;
        }

        ImGui::End();
    }

    void ui::scene_info(scene& scene) {
        ImGui::Begin("Scene");
        entt::entity to_unload = entt::null;

        if (ImGui::Button("Load")) {
            auto path = util::open_file_dialog();
            if (!path.empty()) {
                try {
                    renderer->load(scene, path);
                    load_error.clear();
                } catch (const std::exception& error) {
                    load_error = error.what();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Light")) {
            renderer->add_light(scene, glm::vec3(0.0f, 3.0f, 0.0f)); // drag it into place afterward
        }
        ImGui::Separator();

        if (!load_error.empty())
            ImGui::TextWrapped("Load failed: %s", load_error.c_str());

        ImGui::Text("Objects: %zu", reg.view<Mesh>().size());
        ImGui::Text("Lights: %zu", reg.view<Light>().size());
        ImGui::Separator();

        ImGui::Text("Objects: %zu", reg.view<Mesh>().size());
        ImGui::Separator();

        if (ImGui::BeginChild("SceneTree", ImVec2(0, 0), true)) {
            for (auto entity : reg.view<components::GltfAsset>()) {
                scene_tree_asset(scene, entity, to_unload);
            }
            for (auto entity : reg.view<Mesh>()) {
                if (!reg.all_of<components::GltfChild>(entity))
                    scene_tree_node(scene, entity, to_unload);
            }
            for (auto entity : reg.view<Light>(entt::exclude<components::GltfChild>)) {
                scene_tree_node(scene, entity, to_unload);
            }
        }
        ImGui::EndChild();

        ImGui::End();

        inspector_panel(scene);

        if (to_unload != entt::null) {
            if (selected_entity == to_unload)
                selected_entity = entt::null;
            renderer->unload(scene, to_unload);
        }
    }
}
