//
// Created by super on 26/09/2026.
//

#ifndef PHASE_H
#define PHASE_H

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "brdf.h" // reuse MakeTBN for the local scattering frame

// Henyey-Greenstein phase function for isotropic participating media
// (fog, haze). g in (-1, 1): 0 = isotropic, positive = forward-scattering
// (fog/cloud droplets are typically g = 0.5-0.9), negative = back-scattering.
namespace cpt::phase {
    inline float henyey_greenstein(float cos_theta, float g)
    {
        const float denom = 1.0f + g * g - 2.0f * g * cos_theta;
        return glm::one_over_pi<float>() * 0.25f * (1.0f - g * g) /
               (denom * glm::sqrt(glm::max(denom, 1e-6f)));
    }

    struct PhaseSample {
        glm::vec3 direction;
        float pdf;
    };

    // wo points back toward the previous vertex (i.e. -ray.direction),
    // matching the wi/wo convention used in brdf.h.
    inline PhaseSample sample_henyey_greenstein(glm::vec3 wo, float g, float u1, float u2)
    {
        float cos_theta;
        if (glm::abs(g) < 1e-3f) {
            cos_theta = 1.0f - 2.0f * u1;
        } else {
            const float sqr_term = (1.0f - g * g) / (1.0f + g - 2.0f * g * u1);
            cos_theta = -(1.0f + g * g - sqr_term * sqr_term) / (2.0f * g);
        }

        const float sin_theta = glm::sqrt(glm::max(0.0f, 1.0f - cos_theta * cos_theta));
        const float phi = 2.0f * glm::pi<float>() * u2;

        // Frame built around the incoming travel direction (-wo) so that
        // cos_theta == 1 continues straight ahead, as sampling above assumes.
        const glm::mat3 frame = brdf::MakeTBN(-wo);
        const glm::vec3 local(sin_theta * cosf(phi), sin_theta * sinf(phi), cos_theta);
        const glm::vec3 direction = glm::normalize(frame * local);

        return { direction, henyey_greenstein(cos_theta, g) };
    }
}

#endif //PHASE_H