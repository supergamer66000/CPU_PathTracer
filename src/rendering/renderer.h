//
// Created by super on 18/11/2025.
//

#ifndef RENDERER_H
#define RENDERER_H

#include <thread>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <entt/entt.h>
#include "../types/types.h"

#include <OpenImageDenoise/oidn.hpp>
#include <glad/glad.h>

#include "../random.h"
#include "../types/ray.h"

namespace cpt {
class camera;
class scene;
struct Ray;

struct RendererSettings {
    bool HDR = true;
};

class renderer {
public:
    renderer(
        entt::registry &registry,
        int width,
        int height
    );
    ~renderer();

    const int TILE_COUNT = 12;
    int THREAD_COUNT = 2;

    bool accumulate = false;
    bool ray_trace = false;
    bool denoise = false;

    RendererSettings post_settings{};
    void update_settings_buffer() const;

    float light_scale = 1.f;

    void set_use_sky(bool sky);
    void set_sky_intensity(float power);
    void set_sky_angular_radius(float angle);
    void set_sky_color(glm::vec3 color);
    void set_sky_direction(glm::vec3 direction);

    [[nodiscard]] bool get_sky_enabled() const { return render_sky; }
    [[nodiscard]] float get_sky_angular_radius() const { return sky_angle_radius; };
    [[nodiscard]] float get_sky_intensity() const { return sky_intensity; };
    [[nodiscard]] glm::vec3 get_sky_color() const { return sky_color; };
    [[nodiscard]] glm::vec3 get_sky_direction() const { return sky_direction; };

    void set_fog_enabled(bool enabled);
    void set_fog_density(float sigma_t);
    void set_fog_albedo(glm::vec3 albedo);
    void set_fog_anisotropy(float g);

    [[nodiscard]] bool get_fog_enabled() const { return fog_enabled; }
    [[nodiscard]] float get_fog_density() const { return fog_density; }
    [[nodiscard]] glm::vec3 get_fog_albedo() const { return fog_albedo; }
    [[nodiscard]] float get_fog_anisotropy() const { return fog_g; }

    // BOUNCES //
    int max_bounces = 4;

    // Sampling //
    enum class RaySamples : int { ONE = 0, FOUR = 1, EIGHT = 2, SIXTEEN = 3 };
    RaySamples samples_per_pixel = RaySamples::ONE;
    static constexpr int to_spp_count(RaySamples s) {
        constexpr int counts[] = {1, 4, 8, 16};
        return counts[static_cast<int>(s)];
    }

    [[nodiscard]] camera* get_camera() const { return loaded_camera; }
    void load(scene* scene);
    void load(scene& scene, const std::string& path);
    void add_light(scene& scene, glm::vec3 position);
    void set_light(entt::entity entity, glm::vec3 color, float power, float radius);
    void set_material(entt::entity entity, Material material);
    void set_position(entt::entity entity, glm::vec3 position);
    void set_rotation(entt::entity entity, glm::vec3 rotation);
    void set_asset_position(scene& scene, entt::entity entity, glm::vec3 position);
    void set_asset_rotation(scene& scene, entt::entity entity, glm::vec3 rotation);
    void unload(scene& scene, entt::entity entity);

    void init();
    void update();
    void present_full() const;

    GLuint get_screen_texture_id() const { return screen_texture; }

    struct Size {
        int width;
        int height;

        Size(int w, int h) : width(w), height(h) {}

        operator int*() {
            return &width;
        }

        operator const int*() const {
            return &width;
        }
    };
    Size get_renderer_size() const { return size; }

    /*
     *  Allows you to resize the scene if
     *  that is changed
     */
    void set_renderer_size(Size size);

    void set_scale(float scale);
    [[nodiscard]] float get_scale() const { return render_scale; }

    void reset_accumulation();

private:
    Size size;
    Size render_size;
    float render_scale = 1.0f;
    std::atomic<float> pending_scale{1.0f};
    std::atomic<Size> pending_size{Size(0, 0)};
    int uploaded_width = 0, uploaded_height = 0;

    oidn::DeviceRef iodn_device;
    oidn::FilterRef iodn_filter;
    int iodn_width = -1, iodn_height = -1;
    void denoise_filter();

    GLuint post_process_program;
    GLuint post_process_shader;

    GLuint post_buffer;

    glm::uvec2 tile_size;
    std::unique_ptr<scene> default_scene;
    camera* loaded_camera;
    scene* loaded_scene = nullptr;
    entt::registry &registry;

    bool render_sky = true;
    float sky_intensity = 1.0f;
    float sky_angle_radius = 90.f;
    glm::vec3 sky_color = {1., 1., 1.};
    glm::vec3 sky_direction = {22, 33, 8};

    bool fog_enabled = false;
    float fog_density = 0.05f;
    glm::vec3 fog_albedo = {1.f, 1.f, 1.f};
    float fog_g = 0.0f;

    unsigned int total_frames = 0;

    void start_threads(int threads);
    std::atomic<bool> running = true;
    int tile_index;
    std::mutex tile_mutex;
    std::deque<glm::uvec2> tile_queue;
    std::condition_variable tile_cv;
    int tiles_in_flight = 0;
    bool workers_paused = false;
    bool frame_in_progress = false;
    bool front_ready = false;

    /*-
     * Call this and allows you to make edits to the
     * loaded data without threads pointing to null data
     * in the stack
     */
    void edit(const std::function<void()>& edit);
    struct TileData {
        glm::uvec2 begin;
        glm::uvec2 end;
        glm::vec3* target;
    };
    TileData get_next_tile();

    void render_tile(glm::uvec2 begin, glm::uvec2 end, glm::vec3* target);
    std::vector<std::jthread> threads;

    GLuint screen_texture = 0;
    std::unique_ptr<glm::vec3[]> buffer_a, buffer_b,
        color_buf, albedo_buf, normal_buf, denoised_buf;

    std::atomic<glm::vec3*> front_buffer{nullptr};
    glm::vec3* back_buffer = nullptr;
    std::unique_ptr<glm::vec3[]> accum_buffer;
    unsigned accum_count = 0;
    bool prev_accumulate = false;

    static glm::vec3 skycolor(const Ray& ray);

    struct BaseSample {
        glm::vec4 base_color;
        glm::vec3 normal;
        bool skip;
    };

    BaseSample sample_base(
        const Ray &ray,
        const HitInfo &hit,
        const Material &material,
        float pixel_angle,
        random_distribution<float> &rng
    ) const;

    struct SurfaceSample {
        glm::vec3 albedo;
        glm::vec3 normal;
        float metallic;
        float roughness;
        float alpha; // roughness * roughness
        float transmission;
    };
    [[nodiscard]] SurfaceSample sample_surface(
        const HitInfo &hit,
        const Material &material,
        glm::vec3 normal,
        const glm::vec4 &base_color
    ) const;

    glm::vec3 sample_directional_light(
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const glm::vec3 &wi,
        const SurfaceSample &surf,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) const;

    glm::vec3 sample_point_light(
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const glm::vec3 &wi,
        const SurfaceSample &surf,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) const;

    glm::vec3 sample_area_light( // e.g. emisssives
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const glm::vec3 &wi,
        const SurfaceSample &surf,
        const HitInfo &hit,
        random_distribution<float> &rng
    ) const;

    glm::vec3 sample_fog_lighting(
        const glm::vec3 &scatter_position,
        const glm::vec3 &wo,
        random_distribution<float> &rng
    ) const;

    struct NextSample {
        Ray next_ray;
        glm::vec3 throughput_multiplier;
    };

    static std::optional<NextSample> sample_continuation(
        const glm::vec3 &wi,
        const glm::vec3 &hit_position,
        const glm::vec3 &normal,
        const SurfaceSample &surf,
        const Material &material,
        const HitInfo &hit,
        random_distribution<float> &rng
    );

    glm::vec3 trace_path(Ray current_ray, std::optional<HitInfo> first_hit,
                          float pixel_angle, random_distribution<float>& rng,
                          glm::vec3* out_albedo, glm::vec3* out_normal) const;

    [[nodiscard]] glm::vec3 calc_pixel_color(glm::uvec2 pos, glm::vec3* out_albedo, glm::vec3* out_normal) const;
    [[nodiscard]] glm::vec3 default_calc_pixel_color(glm::uvec2 pos, glm::vec3* out_albedo, glm::vec3* out_normal) const;
};

} // cpt

#endif //RENDERER_H
