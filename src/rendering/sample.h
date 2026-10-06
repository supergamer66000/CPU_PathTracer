//
// Created by super on 23/09/2026.
//

#ifndef SAMPLE_H
#define SAMPLE_H

#include <glm/glm.hpp>

namespace cpt::sample {
    inline glm::vec3 sample_sphere(glm::vec3 center, float radius, float u1, float u2) {
        auto z = 1.0f - 2.0f * u1;
        auto r = sqrtf(glm::max(0.0f, 1.0f - z * z));
        auto phi = 2.0f * glm::pi<float>() * u2;
        glm::vec3 direction(r * cosf(phi), r * sinf(phi), z);
        return center + direction * radius;
    }

    // Claude (Honestly I don't feel good about using it here but, I did)
    struct sphere_light_sample {
        glm::vec3 direction;
        float distance;
        float pdf_solid_angle;
    };

    inline void make_orthonormal_basis(const glm::vec3& n, glm::vec3& t1, glm::vec3& t2)
    {
        float sign = n.z >= 0.0f ? 1.0f : -1.0f;
        float a = -1.0f / (sign + n.z);
        float b = n.x * n.y * a;

        t1 = glm::vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
        t2 = glm::vec3(b, sign + n.y * n.y * a, -n.y);
    }

    inline sphere_light_sample sample_sphere_light(const glm::vec3& p, glm::vec3 center, float radius, float u1, float u2) {
        glm::vec3 to_center = center - p;
        float dist_sq = glm::dot(to_center, to_center);
        float dist = sqrtf(dist_sq);

        if (dist > radius) {
            // p is outside: only sample the visible cap, never the far side
            float sin_theta_max_sq = (radius * radius) / dist_sq;
            float cos_theta_max = sqrtf(glm::max(0.0f, 1.0f - sin_theta_max_sq));

            float cos_theta = 1.0f - u1 * (1.0f - cos_theta_max);
            float sin_theta = sqrtf(glm::max(0.0f, 1.0f - cos_theta * cos_theta));
            float phi = 2.0f * glm::pi<float>() * u2;

            glm::vec3 w = to_center / dist;
            glm::vec3 t1, t2;
            make_orthonormal_basis(w, t1, t2);

            glm::vec3 direction = glm::normalize(
                sin_theta * cosf(phi) * t1 +
                sin_theta * sinf(phi) * t2 +
                cos_theta * w);

            // recover the true hit distance on the sphere along `direction` (ray-sphere intersection)
            float t_ca = glm::dot(to_center, direction);
            float perp_sq = glm::max(0.0f, dist_sq - t_ca * t_ca);
            float t_hit = t_ca - sqrtf(glm::max(0.0f, radius * radius - perp_sq));

            float pdf = 1.0f / (2.0f * glm::pi<float>() * (1.0f - cos_theta_max));
            return { direction, t_hit, pdf };
        }

        // p is inside/overlapping the sphere: the whole sphere surrounds it,
        // there's no "cap" to restrict to -- this is your intersecting-object case
        glm::vec3 sample_point = sample_sphere(center, radius, u1, u2);
        glm::vec3 n = (sample_point - center) / radius;

        glm::vec3 to_sample = sample_point - p;
        float d_sq = glm::dot(to_sample, to_sample);
        float d = sqrtf(d_sq);
        glm::vec3 direction = to_sample / d;

        float cos_theta = glm::dot(n, direction); // always >= 0 here
        float pdf_area = 1.0f / (4.0f * glm::pi<float>() * radius * radius);
        float pdf = (cos_theta > 1e-6f) ? pdf_area * d_sq / cos_theta : 0.0f;

        return { direction, d, pdf };
    }
}

#endif //SAMPLE_H
