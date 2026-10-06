//
// Created by super on 25/10/2025.
//

#include "camera.h"
#include "../input.h"
#include "../types/ray.h"
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>

#include "glm/gtx/quaternion.hpp"

namespace cpt {
    camera::camera() {
        // Start looking towards -Z (common in graphics/ray tracing)
        // Yaw 180° to face -Z from positive Z position
        orientation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0, 1, 0));
        update_orientation_from_yaw_pitch();
    }

    glm::vec3 camera::get_forward() const {
        return orientation * glm::vec3(0, 0, -1);
    }

    glm::vec3 camera::get_right() const {
        return orientation * glm::vec3(1, 0, 0);
    }

    glm::vec3 camera::get_up() const {
        return orientation * glm::vec3(0, 1, 0);
    }

    glm::mat4 camera::get_view_matrix() const {
        glm::mat4 rot = glm::mat4_cast(glm::conjugate(orientation));
        glm::mat4 trans = glm::translate(glm::mat4(1.0f), -position);
        return rot * trans;
    }

    void camera::update_orientation_from_yaw_pitch() {
        float yaw_rad   = glm::radians(current_yaw);
        float pitch_rad = glm::radians(current_pitch);

        glm::quat rot_yaw   = glm::angleAxis(yaw_rad,   glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat rot_pitch = glm::angleAxis(pitch_rad, glm::vec3(1.0f, 0.0f, 0.0f));

        orientation = rot_yaw * rot_pitch;  // yaw first, then pitch
    }

    void camera::recalculate_orientation(float yaw_delta_deg, float pitch_delta_deg) {
        current_yaw   += yaw_delta_deg;
        current_pitch += pitch_delta_deg;

        current_pitch = std::clamp(current_pitch, -89.9f, 89.9f);

        update_orientation_from_yaw_pitch();
    }

    void camera::update(input& input, float deltaTime) {
        // --- Rotation (keyboard) ---
        float yaw_delta = 0.0f;
        float pitch_delta = 0.0f;

        if (input.keyHeld(KEY::KEY_Left)  || input.keyHeld(KEY::KEY_J)) yaw_delta   += rotate_speed * deltaTime;
        if (input.keyHeld(KEY::KEY_Right) || input.keyHeld(KEY::KEY_L)) yaw_delta   -= rotate_speed * deltaTime;
        if (input.keyHeld(KEY::KEY_Up)    || input.keyHeld(KEY::KEY_I)) pitch_delta += rotate_speed * deltaTime;
        if (input.keyHeld(KEY::KEY_Down)  || input.keyHeld(KEY::KEY_K)) pitch_delta -= rotate_speed * deltaTime;

        if (yaw_delta != 0.0f || pitch_delta != 0.0f) {
            recalculate_orientation(yaw_delta, pitch_delta);
        }

        // --- Movement (local to camera orientation) ---
        glm::vec3 move(0.0f);

        if (input.keyHeld(KEY::KEY_W)) move += get_forward();
        if (input.keyHeld(KEY::KEY_S)) move -= get_forward();
        if (input.keyHeld(KEY::KEY_D)) move += get_right();
        if (input.keyHeld(KEY::KEY_A)) move -= get_right();
        if (input.keyHeld(KEY::KEY_Space))       move.y += 1.0f;
        if (input.keyHeld(KEY::KEY_LeftShift))   move.y -= 1.0f;

        if (glm::length2(move) > 0.001f) {
            move = glm::normalize(move) * move_speed * deltaTime;
            position += move;
        }
    }

    Ray camera::get_ray(float u, float v, float aspect_ratio) {
        ray_theta = glm::radians(fov);
        ray_half_height = tan(ray_theta / 2.0f);
        ray_half_width = aspect_ratio * ray_half_height;

        glm::vec3 dir(
            (2.0f * u - 1.0f) * ray_half_width,
            (1.0f - 2.0f * v) * ray_half_height,
            -1.0f
        );

        glm::vec3 world_dir = orientation * glm::normalize(dir);

        return Ray{ position, world_dir };
    }
}
