//
// Created by super on 18/11/2025.
//

#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <iostream>

#include "brdf.h"
#include "phase.h"

#include "../scene/camera.h"
#include "../scene/scene.h"
#include "../components.h"
#include "sample.h"

constexpr const char* POST_PROCESS = R"glsl(
#version 460

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba32f, binding = 0) uniform image2D img;


// Narkowicz ACES fit
uniform float exposure = 1.0;
vec3 aces_tonemap(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 linear_to_srgb(vec3 c) {
    return mix(1.055 * pow(c, vec3(1.0/2.4)) - 0.055, c * 12.92, step(c, vec3(0.0031308)));
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(img);
    if (coord.x >= size.x || coord.y >= size.y) return;

    vec4 pixel = imageLoad(img, coord);
    vec3 hdr = pixel.rgb * exposure;
    vec3 mapped = aces_tonemap(hdr);
    vec3 srgb = linear_to_srgb(mapped);
    imageStore(img, coord, vec4(srgb, pixel.a));
}
)glsl";

namespace cpt {
    renderer::renderer(
        entt::registry &registry,
        int width,
        int height
    ) :
        size(width, height),
        render_size(width, height),
        tile_size({width / TILE_COUNT, height / TILE_COUNT}),
        registry(registry)
    {
        buffer_a     = std::make_unique<glm::vec3[]>(size.width * size.height);
        buffer_b     = std::make_unique<glm::vec3[]>(size.width * size.height);
        color_buf    = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
        albedo_buf   = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
        normal_buf   = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
        denoised_buf = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);

        front_buffer.store(buffer_a.get(), std::memory_order_relaxed);
        back_buffer = buffer_b.get();

        accum_buffer = std::make_unique<glm::vec3[]>(size.width * size.height);

        pending_size.store(size, std::memory_order_relaxed);

        // Instead of doing new scene(...) make a new ptr owned by the renderer so nothing
        default_scene = std::make_unique<scene>(registry); // Default scene if nothing is loaded
        default_scene->commit(); // Workers can trace the empty scene immediately.
        loaded_scene = default_scene.get();

        // TEMP
        loaded_camera = new camera();

        const auto hardware_threads = std::jthread::hardware_concurrency();
        this->THREAD_COUNT = hardware_threads > 8 ? hardware_threads - 2 : 1u;
        start_threads(THREAD_COUNT);
    }

    void renderer::start_threads(int threads) {
        for (int i = 0; i < threads; ++i) {
            this->threads.emplace_back(
                [this] {
                    while (running) {
                        auto tile = this->get_next_tile();
                        if (!running) break;
                        render_tile(tile.begin, tile.end, tile.target);
                    }
                }
            );
        }
    }

    void renderer::init() {
        iodn_device = oidn::newDevice(oidn::DeviceType::CPU);
        iodn_device.commit();

        screen_texture = 0;
        glGenTextures(1, &screen_texture); glBindTexture(GL_TEXTURE_2D, screen_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F,
            render_size.width, render_size.height, 0, GL_RGB, GL_FLOAT, nullptr);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            printf("Error in glTexImage2D: 0x%X  (render_size.width=%d, render_size.height=%d)\n", err, render_size.width, render_size.height);
        }
        uploaded_width = render_size.width;
        uploaded_height = render_size.height;

        // Create SSBO for post processing
        glCreateBuffers(0, &post_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, post_buffer);
        this->update_settings_buffer();

        // Create Post process GPU program
        post_process_program = glCreateProgram();
        post_process_shader = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(post_process_shader, 1, &POST_PROCESS, nullptr);
        glCompileShader(post_process_shader);

        glAttachShader(post_process_program, post_process_shader);
        glLinkProgram(post_process_program);
        glDeleteShader(post_process_shader);

        denoise_filter();
    }

    void renderer::update_settings_buffer() const {
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(post_settings), &post_settings, GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, post_buffer);
    }

    void renderer::denoise_filter() {
        if (iodn_width == render_size.width && iodn_height == render_size.height)
            return; // still valid, reuse as-is

        std::cout << "OIDN filter at " << render_size.width << "x" << render_size.height << "\n";
        iodn_filter = iodn_device.newFilter("RT");
        iodn_filter.setImage("color",  color_buf.get(),  oidn::Format::Float3, render_size.width, render_size.height);
        iodn_filter.setImage("albedo", albedo_buf.get(), oidn::Format::Float3, render_size.width, render_size.height);
        iodn_filter.setImage("normal", normal_buf.get(), oidn::Format::Float3, render_size.width, render_size.height);
        iodn_filter.setImage("output", denoised_buf.get(), oidn::Format::Float3, render_size.width, render_size.height);
        iodn_filter.set("hdr", true);
        iodn_filter.commit();

        const char* filter_err;
        if (iodn_device.getError(filter_err) != oidn::Error::None)
            std::cerr << "OIDN filter commit error: " << filter_err << "\n";

        iodn_width = render_size.width;
        iodn_height = render_size.height;
    }

    void renderer::set_renderer_size(Size size) {
        this->edit([&]() {
            pending_size.store(size, std::memory_order_relaxed);
        });
    }

    void renderer::load(scene *scene) {
        if (scene == nullptr) {
            throw std::runtime_error("Error Loading Scene");
        }
        edit([this, scene] { loaded_scene = scene; });
    }

    void renderer::unload(scene& scene, entt::entity entity) {
        edit([&scene, entity] { scene.unload(entity); });
    }

    void renderer::load(scene& scene, const std::string& path) {
        edit([&scene, &path] { scene.load(path); });
    }

    void renderer::add_light(scene& scene, glm::vec3 position) {
        edit([&scene, position] { scene.add_point_light(position); });
    }

    void renderer::set_use_sky(bool sky) {
        edit([this, sky] { render_sky = sky; });
    }

    void renderer::set_sky_direction(glm::vec3 direction) {
        edit([this, direction] { sky_direction = direction; });
    }

    void renderer::set_sky_intensity(float power) {
        edit([this, power] { sky_intensity = power; });
    }

    void renderer::set_sky_angular_radius(float angle) {
        edit([this, angle] { sky_angle_radius = angle; });
    }

    void renderer::set_sky_color(glm::vec3 color) {
        edit([this, color] { sky_color = color; });
    }

    void renderer::set_fog_enabled(bool enabled) {
        edit([this, enabled] { fog_enabled = enabled; });
    }
    void renderer::set_fog_density(float sigma_t) {
        edit([this, sigma_t] { fog_density = glm::max(0.0f, sigma_t); });
    }
    void renderer::set_fog_albedo(glm::vec3 albedo) {
        edit([this, albedo] { fog_albedo = glm::clamp(albedo, glm::vec3(0.0f), glm::vec3(1.0f)); });
    }
    void renderer::set_fog_anisotropy(float g) {
        edit([this, g] { fog_g = glm::clamp(g, -0.99f, 0.99f); });
    }

    void renderer::set_light(entt::entity entity, glm::vec3 color, float power, float radius) {
        edit([this, entity, color, power, radius] {
            if (!registry.valid(entity) || !registry.all_of<Light>(entity))
                return;
            registry.patch<Light>(entity, [color, power, radius](Light& light) {
                light.color = color;
                light.power = power;
                light.radius = radius;
            });
        });
    }

    void renderer::set_material(entt::entity entity, Material material) {
        edit([this, entity, material] {
            if (!registry.valid(entity) || !registry.all_of<Material>(entity))
                return;
            registry.patch<Material>(entity, [material](Material& entity_material) {
                entity_material = material;
            });
        });
    }

    void renderer::set_asset_position(scene& scene, entt::entity entity, glm::vec3 position) {
        edit([&scene, entity, position] { scene.set_asset_position(entity, position); });
    }

    void renderer::set_asset_rotation(scene& scene, entt::entity entity, glm::vec3 rotation) {
        edit([&scene, entity, rotation] { scene.set_asset_rotation(entity, rotation); });
    }

    void renderer::set_position(entt::entity entity, glm::vec3 position) {
        edit([this, entity, position] {
            if (!registry.valid(entity) || !registry.all_of<components::Position>(entity))
                return;
            registry.patch<components::Position>(entity, [position](auto& p) {
                static_cast<glm::vec3&>(p) = position;
            });
        });
    }

    void renderer::set_rotation(entt::entity entity, glm::vec3 rotation) {
        edit([this, entity, rotation] {
            if (!registry.valid(entity) || !registry.all_of<components::Position>(entity))
                return;
            registry.patch<components::Rotation>(entity, [rotation](auto& p) {
                static_cast<glm::vec3&>(p) = rotation;
            });
        });
    }

    void renderer::edit(const std::function<void()>& edit) {
        std::unique_lock lock(tile_mutex);
        workers_paused = true;
        tile_cv.wait(lock, [this] { return tiles_in_flight == 0; });
        try {
            edit();
            tile_queue.clear(); // Discard the unfinished frame from the old scene.
            frame_in_progress = false;
            std::fill_n(accum_buffer.get(),
                        static_cast<size_t>(render_size.width) * render_size.height,
                        glm::vec3(0.0f));
            accum_count = 0;
            prev_accumulate = false;
        } catch (...) {
            workers_paused = false;
            tile_cv.notify_all();
            throw;
        }
        workers_paused = false;
        tile_cv.notify_all();
    }

    void renderer::set_scale(float scale) {
        scale = std::clamp(scale, 0.05f, 4.0f);
        // Lock-free: just publish the request. No waiting, no buffer/GL
        // touching here, so this is safe from any thread (e.g. ImGui code).
        pending_scale.store(scale, std::memory_order_relaxed);
    }


    void renderer::reset_accumulation() {
        edit([] {});
    }


    glm::vec3 renderer::skycolor(const Ray &ray) {
        const double vertical = ray.direction.y * 0.5f + 0.5f;
        return glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.25f, 0.34f, 0.85), vertical);
    }

    renderer::BaseSample renderer::sample_base(
        const Ray &ray,
        const HitInfo &hit,
        const Material &material,
        float pixel_angle,
        random_distribution<float> &rng
    ) const {
        glm::vec3 normal = hit.normal;
        glm::vec4 base_color = material.base_color_factor;

        if (material.base_color_texture.has_value()) {
            const auto &mipped = loaded_scene->get_mipped(material.base_color_texture.value());
            const float world_footprint_radius = pixel_angle * hit.t_far;
            const auto lod = mipmaps::compute_lod_from_footprint(mipped, world_footprint_radius, hit.uv_density);
            base_color *= mipmaps::sample(mipped, hit.uv.x, hit.uv.y, lod);
        }

        const bool back_face = glm::dot(normal, -ray.direction) < 0.0f;
        const bool transmissive = material.transmission_factor > 0.0f;
        if (back_face && material.double_sided && !transmissive)
            normal = -normal;

        const bool alpha_masked = material.alpha_mode == AlphaMode::Mask &&
                                   base_color.a < material.alphaz_cutoff;
        const bool alpha_blended = material.alpha_mode == AlphaMode::Blend &&
                                    rng.uniform() > glm::clamp(base_color.a, 0.0f, 1.0f);
        const bool one_sided_miss = !material.double_sided && back_face && !transmissive;

        return BaseSample{base_color, normal, one_sided_miss || alpha_masked || alpha_blended};
    }


    renderer::SurfaceSample renderer::sample_surface(
        const HitInfo &hit,
        const Material &material,
        glm::vec3 normal,
        const glm::vec4 &base_color
    ) const {
        float metallic = glm::clamp(material.metallic_factor, 0.0f, 1.0f);
        float roughness = glm::clamp(material.roughness_factor, 0.001f, 1.0f);
        if (material.metallic_roughness_texture) {
            const auto packed = mipmaps::sample(
                loaded_scene->get_mipped(*material.metallic_roughness_texture),
                hit.uv.x, hit.uv.y);
            roughness = glm::clamp(roughness * packed.g, 0.001f, 1.0f);
            metallic = glm::clamp(metallic * packed.b, 0.0f, 1.0f);
        }

        if (material.normal_texture) {
            glm::vec3 mapped = glm::vec3(mipmaps::sample(
                loaded_scene->get_mipped(material.normal_texture->texture_index),
                hit.uv.x, hit.uv.y)) * 2.0f - 1.0f;
            mapped.x *= material.normal_texture->scale;
            mapped.y *= material.normal_texture->scale;
            const auto frame = hit.tangent
                ? brdf::MakeTBNFromTangent(normal, glm::vec3(*hit.tangent), hit.tangent->w)
                : brdf::MakeTBN(normal);
            normal = glm::normalize(frame * mapped);
        }

        float transmission = glm::clamp(material.transmission_factor, 0.0f, 1.0f);
        if (material.transmission_texture) {
            transmission *= mipmaps::sample(
                loaded_scene->get_mipped(*material.transmission_texture),
                hit.uv.x, hit.uv.y).r;
        }

        return SurfaceSample{
            glm::vec3(base_color),
            normal,
            metallic,
            roughness,
            roughness * roughness,
            transmission
        };
    }

    glm::vec3 jitter_direction(const glm::vec3& dir, float angular_radius,
                                random_distribution<float>& rng)
    {
        const float cos_theta_max = glm::cos(angular_radius);

        const float u1 = rng.uniform();
        const float u2 = rng.uniform();

        const float cos_theta = 1.0f - u1 * (1.0f - cos_theta_max);
        const float sin_theta = glm::sqrt(glm::max(0.0f, 1.0f - cos_theta * cos_theta));
        const float phi = 2.0f * glm::pi<float>() * u2;

        glm::vec3 t1, t2;
        sample::make_orthonormal_basis(dir, t1, t2);

        return glm::normalize(
            sin_theta * cosf(phi) * t1 +
            sin_theta * sinf(phi) * t2 +
            cos_theta * dir);
    }

    glm::vec3 renderer::sample_directional_light(
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const glm::vec3 &wi,
        const SurfaceSample &surf,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) const {
        const float cos_theta_max = glm::cos(sky_angle_radius);
        const glm::vec3 jittered_dir = jitter_direction(sky_direction, sky_angle_radius, rng);

        const float n_dot_l = glm::dot(normal, jittered_dir);
        if (n_dot_l <= 0.0f) return glm::vec3(0.0f);

        const Ray shadow_ray(hit_position + normal * 0.0001f, jittered_dir);
        if (loaded_scene->occluded(shadow_ray, 1e20f)) return glm::vec3(0.0f);

        const glm::vec3 F = brdf::ggx_brdf(wi, jittered_dir, normal,
                                            surf.albedo, surf.alpha, surf.metallic);

        // Solid-angle pdf of picking `jittered_dir` via the cone-sampling above.
        const float light_pdf = 1.0f / (2.0f * glm::pi<float>() * (1.0f - cos_theta_max));

        // pdf that BRDF-based sampling (sample_continuation) would have produced
        // this same direction, for the balance heuristic.
        const auto tangent = hit.tangent ? glm::vec3(*hit.tangent) : glm::vec3(0.0f);
        const auto tangent_sign = hit.tangent ? hit.tangent->w : 1.0f;
        const auto TBN = brdf::MakeTBNFromTangent(normal, tangent, tangent_sign);
        const glm::vec3 Vt = glm::transpose(TBN) * wi;
        const glm::vec3 Lt = glm::transpose(TBN) * jittered_dir;
        const glm::vec3 Ht = glm::normalize(Vt + Lt);
        const float bsdf_pdf = brdf::GGX_VNDF_PDF_L(Vt, Ht, surf.alpha);

        const float mis_weight = light_pdf / (light_pdf + bsdf_pdf);

        const glm::vec3 intensity = sky_color * sky_intensity;
        return F * intensity * n_dot_l * mis_weight / light_pdf;
    }


    glm::vec3 renderer::sample_point_light(
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const glm::vec3 &wi,
        const SurfaceSample &surf,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) const {
        const auto sampled = loaded_scene->sample_point_light(rng.uniform());
        if (!sampled) return glm::vec3(0.0f);

        const auto light_sample = sample::sample_sphere_light(
            hit_position, sampled->position, sampled->light.radius / 10.f, // TODO FIX
            rng.uniform(), rng.uniform());
        if (light_sample.pdf_solid_angle <= 0.0f) return glm::vec3(0.0f);

        const glm::vec3& light_dir = light_sample.direction;
        const float dist = light_sample.distance;
        const float dist_sq = dist * dist;
        const float n_dot_l = glm::dot(normal, light_dir);
        if (n_dot_l <= 0.0f) return glm::vec3(0.0f);

        const Ray shadow_ray(hit_position + normal * 0.0001f, light_dir);
        if (loaded_scene->occluded(shadow_ray, dist)) return glm::vec3(0.0f);

        const auto tangent = hit.tangent ? glm::vec3(*hit.tangent) : glm::vec3(0.0f);
        const auto tangent_sign = hit.tangent ? hit.tangent->w : 1.0f;
        const auto sample = brdf::sample_ggx(wi, normal, surf.albedo, surf.alpha, surf.metallic,
                                              tangent, tangent_sign, rng);
        if (!sample) return glm::vec3(0.0f);

        const glm::vec3 intensity =
            (sampled->light.color * sampled->light.power) / (dist_sq * sampled->probability);
        return sample->weight * intensity * n_dot_l * light_scale;
    }

    glm::vec3 renderer::sample_area_light(
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const glm::vec3 &wi,
        const SurfaceSample &surf,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) const {
        const auto sampled = loaded_scene->sample_light(rng.uniform3());
        if (!sampled) return glm::vec3(0.0f);

        const glm::vec3 to_light = sampled->position - hit_position;
        const float dist_sq = glm::dot(to_light, to_light);
        if (dist_sq <= 1e-8f || sampled->pdf_area <= 0.0f) return glm::vec3(0.0f);

        const float dist = std::sqrt(dist_sq);
        const glm::vec3 light_dir = to_light / dist;
        const float cos_surface = glm::max(glm::dot(normal, light_dir), 0.0f);
        const float cos_light = glm::max(glm::dot(sampled->normal, -light_dir), 0.0f);
        if (cos_surface <= 0.0f || cos_light <= 0.0f) return glm::vec3(0.0f);

        const Ray shadow_ray(hit_position + normal * 0.0001f, light_dir);
        if (loaded_scene->occluded(shadow_ray, dist - 0.0002f)) return glm::vec3(0.0f);

        const auto tangent = hit.tangent ? glm::vec3(*hit.tangent) : glm::vec3(0.0f);
        const auto tangent_sign = hit.tangent ? hit.tangent->w : 1.0f;
        const auto sample = brdf::sample_ggx(wi, normal, surf.albedo, surf.alpha, surf.metallic,
                                              tangent, tangent_sign, rng);
        if (!sample) return glm::vec3(0.0f);

        const float geometry = cos_surface * cos_light / dist_sq;
        return sample->weight * sampled->emission * geometry / sampled->pdf_area;
    }

    glm::vec3 renderer::sample_fog_lighting(
        const glm::vec3 &scatter_position,
        const glm::vec3 &wo,
        random_distribution<float> &rng
    ) const {
               glm::vec3 result(0.0f);

        // Sun / sky, cone-sampled with MIS against phase sampling -- mirrors
        // sample_directional_light's surface version, minus the cosine term.
        if (render_sky) {
            const float cos_theta_max = glm::cos(sky_angle_radius);
            const glm::vec3 jittered_dir = jitter_direction(sky_direction, sky_angle_radius, rng);

            const Ray shadow_ray(scatter_position, jittered_dir);
            if (!loaded_scene->occluded(shadow_ray, 1e20f)) {
                const float cos_theta = glm::dot(-wo, jittered_dir);
                const float phase_val = phase::henyey_greenstein(cos_theta, fog_g);
                const float light_pdf = 1.0f / (2.0f * glm::pi<float>() * (1.0f - cos_theta_max));
                const float mis_weight = light_pdf / (light_pdf + phase_val);

                result += fog_albedo * phase_val * (sky_color * sky_intensity) * mis_weight / light_pdf;
            }
        }

        // Point lights
        if (const auto sampled = loaded_scene->sample_point_light(rng.uniform())) {
            const glm::vec3 to_light = sampled->position - scatter_position;
            const float dist_sq = glm::dot(to_light, to_light);
            if (dist_sq > 1e-8f) {
                const float dist = std::sqrt(dist_sq);
                const glm::vec3 light_dir = to_light / dist;

                const Ray shadow_ray(scatter_position, light_dir);
                if (!loaded_scene->occluded(shadow_ray, dist)) {
                    const float phase_val = phase::henyey_greenstein(glm::dot(-wo, light_dir), fog_g);
                    const glm::vec3 intensity =
                        (sampled->light.color * sampled->light.power) / (dist_sq * sampled->probability);
                    result += fog_albedo * phase_val * intensity;
                }
            }
        }

        // Emissive triangles (area lights)
        if (const auto sampled = loaded_scene->sample_light(rng.uniform3())) {
            const glm::vec3 to_light = sampled->position - scatter_position;
            const float dist_sq = glm::dot(to_light, to_light);
            if (dist_sq > 1e-8f && sampled->pdf_area > 0.0f) {
                const float dist = std::sqrt(dist_sq);
                const glm::vec3 light_dir = to_light / dist;
                const float cos_light = glm::max(glm::dot(sampled->normal, -light_dir), 0.0f);

                if (cos_light > 0.0f) {
                    const Ray shadow_ray(scatter_position, light_dir);
                    if (!loaded_scene->occluded(shadow_ray, dist - 0.0002f)) {
                        const float phase_val = phase::henyey_greenstein(glm::dot(-wo, light_dir), fog_g);
                        const float geometry = cos_light / dist_sq;
                        result += fog_albedo * phase_val * sampled->emission * geometry / sampled->pdf_area;
                    }
                }
            }
        }

        return result;
    }


    std::optional<renderer::NextSample> renderer::sample_continuation(
        const glm::vec3 &wi,
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const SurfaceSample &surf,
        const Material &material,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) {
        const auto tangent = hit.tangent ? glm::vec3(*hit.tangent) : glm::vec3(0.0f);
        const auto tangent_sign = hit.tangent ? hit.tangent->w : 1.0f;

        const float effective_transmission = surf.transmission * (1.0f - surf.metallic);
        if (effective_transmission > 0.0f && rng.uniform() < effective_transmission) {
            auto sample = brdf::sample_dielectric(
                wi, normal, material.eta_i, material.eta_t, surf.alpha, tangent, tangent_sign, rng);
            if (!sample) return std::nullopt;

            return NextSample{
                Ray(hit_position + sample->direction * 0.0001f, sample->direction),
                sample->weight
            };
        }

        auto sample = brdf::sample_ggx(wi, normal, surf.albedo, surf.alpha, surf.metallic, tangent, tangent_sign, rng);
        if (!sample) return std::nullopt;

        return NextSample{
            Ray(hit_position + normal * 0.0001f, sample->direction),
            sample->weight
        };
    }

    glm::vec3 renderer::trace_path(Ray current_ray, std::optional<HitInfo> first_hit, float pixel_angle, random_distribution<float> &rng,
        glm::vec3* out_albedo, glm::vec3* out_normal) const {
        glm::vec3 radiance(0.0f);
        glm::vec3 throughput(1.0f);
        bool first_iteration = true;

        for (int bounce = 0; bounce < max_bounces; ++bounce) {
            std::optional<HitInfo> maybe = first_iteration
                ? first_hit
                : loaded_scene->intersect(current_ray);
            first_iteration = false;

            if (fog_enabled && fog_density > 0.0f) {
                const float t_far = maybe ? maybe->t_far : 1e6f;
                const float t_scatter = -std::log(1.0f - rng.uniform()) / fog_density;

                if (t_scatter < t_far) {
                    const glm::vec3 scatter_position = current_ray.origin + current_ray.direction * t_scatter;
                    throughput *= fog_albedo;
                    radiance += throughput * sample_fog_lighting(scatter_position, -current_ray.direction, rng);
                    break;
                }
            }

            if (!maybe) {
                if (render_sky) radiance += throughput * skycolor(current_ray);
                break;
            }
            const auto hit = maybe.value();
            const auto material = loaded_scene->get_material(hit.material);
            const auto base = sample_base(current_ray, hit, material, pixel_angle, rng);
            if (base.skip) {
                current_ray.origin += current_ray.direction * (hit.t_far + 0.0001f);
                --bounce;
                continue;
            }
            const auto surface = sample_surface(hit, material, base.normal, base.base_color);
            const glm::vec3& normal = surface.normal;

            if (bounce == 0) { // for the denoiser
                if (out_albedo) *out_albedo = surface.albedo;
                if (out_normal) *out_normal = surface.normal;
            }

            const glm::vec3 hit_position =
                current_ray.origin + current_ray.direction * hit.t_far;

            const glm::vec3 surface_emission = loaded_scene->get_emission(hit.material, hit.uv);
            if (glm::any(glm::greaterThan(surface_emission, glm::vec3(0.0f)))) {
                if (bounce == 0)
                    radiance += throughput * surface_emission;
                break;
            }

            if (material.unlit) {
                radiance += throughput * surface.albedo;
                break;
            }

            const glm::vec3 wi = -current_ray.direction;

            if (render_sky) radiance += throughput * sample_directional_light(hit_position, normal, wi, surface, hit, rng);
            radiance += throughput * sample_point_light(hit_position, normal, wi, surface, hit, rng);
            radiance += throughput * sample_area_light(hit_position, normal, wi, surface, hit, rng);

            auto continuation = sample_continuation(wi, hit_position, normal, surface, material, hit, rng);
            if (!continuation)
                break;

            throughput *= continuation->throughput_multiplier;
            if (bounce > 3) {
                float p = glm::max(throughput.r, glm::max(throughput.g, throughput.b));
                p = glm::clamp(p, 0.05f, 1.0f);
                if (rng.uniform() > p) break;
                throughput /= p;
            }
            current_ray = continuation->next_ray;
        }
        return radiance;
    }


    glm::vec3 renderer::calc_pixel_color(glm::uvec2 pos, glm::vec3* out_albedo, glm::vec3* out_normal) const {
        thread_local cpt::random_distribution<float> rng;
        if (loaded_scene == nullptr) {
            std::cerr << "No loaded scene or camera.";
            return glm::vec3(0.0f);
        }

        const auto ratio = static_cast<float>(render_size.width) / static_cast<float>(render_size.height);
        const auto pixel_angle = 2.0f * tanf(loaded_camera->get_theta() * 0.5f) / static_cast<float>(render_size.height);

        // spp == 1 stays exactly as it was — centered, unjittered — so accumulate-mode
        // convergence behavior doesn't change for anyone not opting into multisampling.
        float division = 1;
        if (samples_per_pixel == RaySamples::ONE) {
            const auto u = (static_cast<float>(pos.x) + 0.5f) / static_cast<float>(render_size.width);
            const auto v = (static_cast<float>(render_size.height) - (static_cast<float>(pos.y) + 0.5f)) / static_cast<float>(render_size.height);
            Ray ray = loaded_camera->get_ray(u, v, ratio);
            auto first_hit = loaded_scene->intersect(ray);
            return trace_path(ray, first_hit, pixel_angle, rng, out_albedo, out_normal);
        }

        auto jittered_ray = [&] {
            const auto u = (static_cast<float>(pos.x) + rng.uniform()) / static_cast<float>(render_size.width);
            const auto v = (static_cast<float>(render_size.height) - (static_cast<float>(pos.y) + rng.uniform())) / static_cast<float>(render_size.height);
            return loaded_camera->get_ray(u, v, ratio);
        };

        glm::vec3 total(0.0f);
        switch (samples_per_pixel) {
            case RaySamples::FOUR: {
                division = 4;
                std::array<Ray, 4> batch;
                for (auto& r : batch) r = jittered_ray();
                auto hits = loaded_scene->intersect4(batch);
                for (int i = 0; i < 4; ++i) total += trace_path(batch[i], hits[i], pixel_angle, rng, out_albedo, out_normal);
            }
            break;

            case RaySamples::EIGHT: {
                division = 8;
                std::array<Ray, 8> batch;
                for (auto& r : batch) r = jittered_ray();
                auto hits = loaded_scene->intersect8(batch);
                for (int i = 0; i < 8; ++i) total += trace_path(batch[i], hits[i], pixel_angle, rng, out_albedo, out_normal);
            }
            break;

            case RaySamples::SIXTEEN: {
                division = 16;
                std::array<Ray, 16> batch;
                for (auto& r : batch) r = jittered_ray();
                auto hits = loaded_scene->intersect16(batch);
                for (int i = 0; i < 16; ++i) total += trace_path(batch[i], hits[i], pixel_angle, rng, out_albedo, out_normal);
            }
            break;
            default: break;
        }
        return total / division;
    }

    glm::vec3 renderer::default_calc_pixel_color(glm::uvec2 pos, glm::vec3* out_albedo, glm::vec3* out_normal) const {    thread_local cpt::random_distribution<float> rng;
        if (loaded_scene == nullptr) {
            std::cerr << "No loaded scene or camera.";
            return {0, 0, 0};
        }

        const auto u = (static_cast<float>(pos.x) + 0.5f) / static_cast<float>(render_size.width);
        const auto v = (static_cast<float>(render_size.height) - (static_cast<float>(pos.y) + 0.5f)) / static_cast<float>(render_size.height);
        const auto ratio = static_cast<float>(render_size.width) / static_cast<float>(render_size.height);

        const Ray current_ray = loaded_camera->get_ray(u, v, ratio);
        const auto pixel_angle = 2.0f * tanf(loaded_camera->get_theta() * 0.5f) / static_cast<float>(render_size.height);

        if (auto maybe = loaded_scene->intersect(current_ray)) {
            const auto hit = maybe.value();
            const auto material = loaded_scene->get_material(hit.material);
            const glm::vec3 hit_position =
                current_ray.origin +
                current_ray.direction * hit.t_far;

            auto albedo = glm::vec3(material.base_color_factor);
            if (material.base_color_texture.has_value()) {
                const auto& mipped = loaded_scene->get_mipped(*material.base_color_texture);
                const auto world_footprint_radius = pixel_angle * hit.t_far;
                const auto lod = mipmaps::compute_lod_from_footprint(mipped, world_footprint_radius, hit.uv_density);
                albedo = mipmaps::sample(mipped, hit.uv.x, hit.uv.y, lod);
            }

            const glm::vec3 normal = glm::normalize(hit.normal);

            if (out_albedo) *out_albedo = albedo;
            if (out_normal) *out_normal = normal;

            glm::vec3 diffuse(0.0f);
            if (const auto sampled = loaded_scene->sample_point_light(rng.uniform())) {
                const glm::vec3 to_light = sampled->position - hit_position;
                const float dist_sq = glm::dot(to_light, to_light);

                if (dist_sq > 1e-8f) {
                    const float dist = sqrtf(dist_sq);
                    const glm::vec3 light_dir = to_light / dist;
                    const float n_dot_l = glm::max(glm::dot(normal, light_dir), 0.0f);

                    if (n_dot_l > 0.0f) {
                        const Ray shadow_ray(hit_position + normal * 0.0001f, light_dir);
                        if (!loaded_scene->occluded(shadow_ray, dist)) {
                            const glm::vec3 intensity =
                                (sampled->light.color * sampled->light.power) /
                                (dist_sq * sampled->probability);
                            diffuse = albedo * intensity * n_dot_l;
                        }
                    }
                }
            }

            return diffuse;
        }

        // Miss: no surface, so no albedo/normal to report
        if (out_albedo) *out_albedo = glm::vec3(0.0f);
        if (out_normal) *out_normal = glm::vec3(0.0f);
        return skycolor(current_ray);
    }

    renderer::TileData renderer::get_next_tile() {
        std::unique_lock lock(tile_mutex);

        tile_cv.wait(lock, [this] {
            return !running || (!workers_paused &&
                               (!tile_queue.empty() || tiles_in_flight == 0));
        });

        if (!running) {
            return {glm::uvec2(0), glm::uvec2(0), back_buffer};
        }

        if (tile_queue.empty()) {
            // Only show frames that finished all their tiles.
            if (frame_in_progress) {
                glm::vec3* old_front = front_buffer.load(std::memory_order_relaxed);
                front_buffer.store(back_buffer, std::memory_order_release);
                back_buffer = old_front;
                front_ready = true;
                ++total_frames;
            }

            const float requested_scale = pending_scale.load(std::memory_order_relaxed);
            const Size requested_size = pending_size.load(std::memory_order_relaxed);
            const bool size_changed = requested_size.width  != size.width ||
                                       requested_size.height != size.height;
            if (requested_scale != render_scale || size_changed) {
                render_scale = requested_scale;
                size = requested_size;
                render_size.width  = std::max(1, static_cast<int>(size.width  * render_scale));
                render_size.height = std::max(1, static_cast<int>(size.height * render_scale));

                tile_size = glm::uvec2(render_size.width / TILE_COUNT, render_size.height / TILE_COUNT);

                buffer_a = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                buffer_b = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                front_buffer.store(buffer_a.get(), std::memory_order_relaxed);
                back_buffer = buffer_b.get();
                front_ready = false;

                accum_buffer = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                accum_count = 0;
                prev_accumulate = false;

                color_buf    = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                albedo_buf   = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                normal_buf   = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                denoised_buf = std::make_unique<glm::vec3[]>(render_size.width * render_size.height);
                denoise_filter();
            }

            if (accumulate) {
                if (!prev_accumulate) {
                    std::fill_n(accum_buffer.get(),
                                static_cast<size_t>(render_size.width) * render_size.height,
                                glm::vec3(0.0f));
                    accum_count = 0;
                }
                ++accum_count;
            } else {
                accum_count = 0;
            }
            prev_accumulate = accumulate;
            frame_in_progress = true;

            for (int ty = 0; ty < TILE_COUNT; ++ty) {
                for (int tx = 0; tx < TILE_COUNT; ++tx) {
                    tile_queue.push_back({
                        static_cast<unsigned>(tx),
                        static_cast<unsigned>(ty)
                    });
                }
            }
            tile_cv.notify_all();
        }

        const auto tile_coord = tile_queue.front();
        tile_queue.pop_front();
        ++tiles_in_flight;

        auto begin = tile_coord * tile_size;
        auto end = (tile_coord + glm::uvec2(1)) * tile_size;

        if (tile_coord.x == TILE_COUNT - 1) end.x = render_size.width;
        if (tile_coord.y == TILE_COUNT - 1) end.y = render_size.height;

        return {begin, end, back_buffer};
    }

    void renderer::render_tile(const glm::uvec2 begin, const glm::uvec2 end, glm::vec3* target) {
        const bool accumulating = accumulate;
        const unsigned count = accum_count == 0 ? 1u : accum_count;

        for (int y = begin.y; y < end.y; ++y) {
            for (int x = begin.x; x < end.x; ++x) {
                auto i = x + y * render_size.width;
                glm::vec3 sample;
                glm::vec3 alb(0.0f), norm(0.0f);
                if (ray_trace) {
                    sample = calc_pixel_color({x, y}, &alb, &norm);
                } else {
                    sample = default_calc_pixel_color({x,y}, &alb, &norm);
                }

                albedo_buf[i] = alb;
                normal_buf[i] = norm;

                if (accumulating) {
                    accum_buffer[i] += sample;
                    target[i] = accum_buffer[i] / static_cast<float>(count);
                } else {
                    target[i] = sample;
                }
            }
        }

        std::lock_guard lock(tile_mutex);
        --tiles_in_flight;
        if (tiles_in_flight == 0)
            tile_cv.notify_all();
    }

    void renderer::update() {
        std::lock_guard lock(tile_mutex);
        if (!front_ready)
            return;
        const glm::vec3 *buf = front_buffer.load(std::memory_order_acquire);
        const int rw = render_size.width;
        const int rh = render_size.height;

        const glm::vec3* upload_src = buf;
        if (ray_trace && denoise) {
            std::memcpy(color_buf.get(), buf, sizeof(glm::vec3) * rw * rh);
            iodn_filter.execute();

            const char* error;
            if (iodn_device.getError(error) != oidn::Error::None)
                std::cerr << "OIDN error: " << error << "\n";

            upload_src = denoised_buf.get();
        }

        glBindTexture(GL_TEXTURE_2D, screen_texture);
        if (rw != uploaded_width || rh != uploaded_height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, rw, rh, 0,
                         GL_RGB, GL_FLOAT, nullptr);
            uploaded_width = rw;
            uploaded_height = rh;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh,
                        GL_RGB, GL_FLOAT, upload_src);

        if (post_settings.HDR) {
            glUseProgram(post_process_program);
            glBindImageTexture(0, screen_texture, 0, GL_FALSE,
                0, GL_READ_WRITE, GL_RGBA32F);
            glDispatchCompute(render_size.width / 16,
                render_size.height / 16, 1);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }
    }

    void renderer::present_full() const {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, size.width, size.height, 0, -1, 1);

        glMatrixMode(GL_MODELVIEW); glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glEnable(GL_TEXTURE_2D);

        glColor3f(1.0f, 1.0f, 1.0f);


        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(0,     0);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(size.width, 0);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(size.width, size.height);
        glTexCoord2f(0.0f, 0.0f); glVertex2i(0,     size.height);
        glEnd();

        glDisable(GL_TEXTURE_2D);
    }

    renderer::~renderer() {
        running = false;
        {
            std::lock_guard lock(tile_mutex);
            tile_cv.notify_all();
        }
        for (auto& t : threads) {
            if (t.joinable())
                t.join();
        }

        if (screen_texture) glDeleteTextures(1, &screen_texture);
        delete loaded_camera;
    }
} // cpt