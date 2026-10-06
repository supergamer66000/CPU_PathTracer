//
// Created by super on 25/10/2025.
//

#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


namespace cpt {
class input;
struct Ray;

class camera {
public:
    enum class mode { perspective, orthographic };

    mode current_mode = mode::perspective;
    float fov = 90.0f;                    // vertical FOV in degrees
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 2.f);  // good starting position for ray tracer
    float move_speed = 20.0f;               // units per second
    float rotate_speed = 60.0f;            // degrees per second (keyboard)

    camera();

    // Update from input (call every frame)
    void update(input& input, float deltaTime);

    // Core ray tracing function: get ray through normalized screen coords
    // u, v in [0,1] where (0,0) is bottom-left, (1,1) is top-right
    Ray get_ray(float u, float v, float aspect_ratio);

    // Optional: get view matrix if you ever need OpenGL preview
    glm::mat4 get_view_matrix() const;

    // Directions (useful for movement)
    glm::vec3 get_forward() const;
    glm::vec3 get_right() const;
    glm::vec3 get_up() const;

    const float& get_theta() const { return ray_theta; }
    const float& get_half_height() const { return ray_half_height; }
    const float& get_half_width() const { return ray_half_width; }

private:
    glm::quat orientation;  // yaw + pitch only (no roll)

    float ray_theta;
    float ray_half_height;
    float ray_half_width;

    float current_yaw = 0.0f;
    float current_pitch = 0.0f;

    void update_orientation_from_yaw_pitch();
    void recalculate_orientation(float yaw_delta_deg, float pitch_delta_deg);
};

} // cpt

#endif //CAMERA_H
