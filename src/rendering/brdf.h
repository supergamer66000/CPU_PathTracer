//
// Created by super on 06/09/2026.
//

#ifndef BRDF_H
#define BRDF_H

#include "glm/glm.hpp"
#include <glm/gtc/constants.hpp>
#include <limits>
#include <cmath>

#include "random"

// Reference https://www.shadertoy.com/view/MdVfW3

// TODO https://inria.hal.science/hal-00996995v1/document
// https://www.shadertoy.com/view/MtXczf

namespace cpt::brdf {
    inline float chiGGX(float v)
    {
        return v > 0 ? 1 : 0;
    }

    inline float GGX_Distribution(glm::vec3 n, glm::vec3 h, float alpha) {
        auto NoH = glm::dot(n,h);
        auto NoH2 = NoH * NoH;
        auto x = (1.f - (1.f - alpha) * NoH2);
        auto D = alpha / (glm::pi<float>() * x * x);
        return D;
    }

    // Helper for calculating the F0 Term
    inline glm::vec3 Fresnel_Schlick(float cosT, glm::vec3 F0)
    {
        return F0 + (1.f-F0) * glm::pow( 1.f - cosT, 5.f);
    }

    inline float fresnel_dielectric(float cosThetaI, float etaI, float etaT) {
        cosThetaI = glm::clamp(cosThetaI, -1.0f, 1.0f);

        if (cosThetaI < 0.0f) {
            std::swap(etaI, etaT);
            cosThetaI = -cosThetaI;
        }

        float sinThetaI = std::sqrt(std::max(0.0f, 1.0f - cosThetaI * cosThetaI));
        float sinThetaT = etaI / etaT * sinThetaI;

        if (sinThetaT >= 1.0f) return 1.0f;

        float cosThetaT = std::sqrt(std::max(0.0f, 1.0f - sinThetaT * sinThetaT));

        float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
                      ((etaT * cosThetaI) + (etaI * cosThetaT));
        float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
                      ((etaI * cosThetaI) + (etaT * cosThetaT));

        return (Rparl * Rparl + Rperp * Rperp) * 0.5f;
    }

    inline float GGX_PartialGeometryTerm(glm::vec3 v, glm::vec3 n, glm::vec3 h, float alpha)
    {
        const float NoV = glm::max(glm::dot(n, v), 0.0f);
        const float k = alpha * 0.5f;
        return NoV / (NoV * (1.0f - k) + k);
    }

    inline glm::mat3 MakeTBN(glm::vec3 N)
    {
        glm::vec3 T;

        if (glm::abs(N.z) < 0.999f) {
            T = glm::normalize(
                glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), N)
            );
        } else {
            T = glm::normalize(
                glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), N)
            );
        }

        glm::vec3 B = glm::cross(N, T);

        return glm::mat3(T, B, N);
    }

    inline glm::mat3 MakeTBNFromTangent(glm::vec3 N, glm::vec3 tangent, float handedness)
    {
        glm::vec3 T = tangent - N * glm::dot(N, tangent);
        const float len2 = glm::dot(T, T);
        if (!(len2 > 1e-12f) || !std::isfinite(len2)) {
            return MakeTBN(N);
        }

        T *= 1.0f / std::sqrt(len2);
        const float h = handedness != 0.0f ? handedness : 1.0f;
        glm::vec3 B = glm::cross(N, T) * h;
        return glm::mat3(T, B, N);
    }

    inline float Smith_Lambda(float NoV, float alpha)
    {
        if (NoV <= 0.0f)
            return std::numeric_limits<float>::infinity();

        float alpha2 = alpha * alpha;
        float NoV2 = NoV * NoV;

        return (
            -1.0f +
            glm::sqrt(
                1.0f +
                alpha2 * (1.0f - NoV2) / NoV2
            )
        ) * 0.5f;
    }

    inline float Smith_G1(float NoV, float alpha)
    {
        if (NoV <= 0.0f)
            return 0.0f;

        float lambda = Smith_Lambda(NoV, alpha);

        return 1.0f / (1.0f + lambda);
    }

    inline float Smith_G2(float NoV,
                          float NoL,
                          float alpha
    ) {
        if (NoV <= 0.0f || NoL <= 0.0f)
            return 0.0f;

        float lambdaV = Smith_Lambda(NoV, alpha);
        float lambdaL = Smith_Lambda(NoL, alpha);

        return 1.0f /
            (1.0f + lambdaV + lambdaL);
    }

    inline float Smith_G2_over_G1(float NoV, float NoL, float alpha)
    {
        if (NoV <= 0.0f || NoL <= 0.0f)
            return 0.0f;

        const float lambdaV = Smith_Lambda(NoV, alpha);
        const float lambdaL = Smith_Lambda(NoL, alpha);

        return glm::clamp((1.0f + lambdaV) / (1.0f + lambdaV + lambdaL), 0.0f, 1.0f);
    }

    inline glm::vec3 SampleGGXVNDF(glm::vec3 V,
                                   float alpha,
                                   float u1,
                                   float u2
    ) {
        // Transform view direction into the stretched GGX hemisphere.
        glm::vec3 Vh = glm::normalize(
            glm::vec3(
                alpha * V.x,
                alpha * V.y,
                V.z
            )
        );

        // Build tangent frame around Vh.
        float lensq = Vh.x * Vh.x + Vh.y * Vh.y;

        glm::vec3 T1;

        if (lensq > 0.0f) {
            T1 = glm::vec3(-Vh.y, Vh.x, 0.0f) * (1.0f / glm::sqrt(lensq));
        } else {
            T1 = glm::vec3(1.0f, 0.0f, 0.0f);
        }

        glm::vec3 T2 = glm::cross(Vh, T1);

        // Uniform sample on projected disk.
        float r = glm::sqrt(u1);
        float phi = 2.0f * glm::pi<float>() * u2;

        float t1 = r * glm::cos(phi);
        float t2 = r * glm::sin(phi);

        // Warp disk sample according to view direction.
        float s = 0.5f * (1.0f + Vh.z);

        t2 = (1.0f - s) * glm::sqrt(glm::max(0.0f, 1.0f - t1 * t1)) + s * t2;

        // Reproject onto hemisphere.
        glm::vec3 Nh = t1 * T1 + t2 * T2 + glm::sqrt(glm::max(0.0f, 1.0f - t1 * t1 - t2 * t2 )) * Vh;

        glm::vec3 H = glm::normalize(glm::vec3(alpha * Nh.x, alpha * Nh.y, glm::max(0.0f, Nh.z)));

        return H;
    }

    inline float GGX_VNDF_PDF_H(glm::vec3 Vt, glm::vec3 Ht, float alpha)
    {
        const float NoV = glm::max(Vt.z, 0.0f);
        if (NoV <= 0.0f) return 0.0f;

        const float VoH = glm::max(glm::dot(Vt, Ht), 0.0f);
        const float D = GGX_Distribution(glm::vec3(0.0f, 0.0f, 1.0f), Ht, alpha);
        const float G1 = Smith_G1(NoV, alpha);

        return (G1 * D * VoH) / NoV;
    }

    // pdf of the reflected direction L (includes the H -> L Jacobian, 1/(4*VoH))
    inline float GGX_VNDF_PDF_L(glm::vec3 Vt, glm::vec3 Ht, float alpha)
    {
        const float VoH = glm::max(glm::dot(Vt, Ht), 0.0f);
        if (VoH <= 0.0f) return 0.0f;

        return GGX_VNDF_PDF_H(Vt, Ht, alpha) / (4.0f * VoH);
    }

    struct BSDFSample
    {
        glm::vec3 direction;
        glm::vec3 weight;
        float pdf;
    };

    inline glm::vec3 ggx_brdf(glm::vec3 wi,
                              glm::vec3 L,
                              glm::vec3 N,
                              glm::vec3 mateColor,
                              float alpha,
                              float metallic
    ) {
        auto NoV = glm::max(glm::dot(N, wi), 0.0f);
        auto NoL = glm::max(glm::dot(N, L), 0.0f);
        glm::vec3 V_plus_L = wi + L;
        auto H = glm::normalize(V_plus_L);
        if (glm::dot(V_plus_L, V_plus_L) > 0.0001f) {
            H = glm::normalize(V_plus_L);
        } else {
            H = N;
        }
        auto VoH = glm::max(glm::dot(wi, H), 0.0f);

        auto F0 = glm::mix(glm::vec3(0.04), mateColor, metallic);
        auto F = Fresnel_Schlick(VoH, F0);

        auto D = GGX_Distribution(N, H, alpha);
        auto G = Smith_G2(NoV, NoL, alpha);

        auto numerator = D * F * G;
        auto denominator = 4.0f * (NoV * NoL + glm::epsilon<float>());

        auto specular = numerator / denominator;

        auto kD = (glm::vec3(1.0f) - F) * (1.0f - metallic);
        auto diffuse = kD * mateColor / glm::pi<float>();

        return specular + diffuse;
    }

    // V is the wo
    // N is normal
    inline std::optional<BSDFSample> sample_ggx(glm::vec3 wi,
                                                glm::vec3 N,
                                                glm::vec3 albedo,
                                                float alpha, // roughness ^ 2
                                                float metallic,
                                                const glm::vec3& tangent,
                                                float tangent_sign,
                                                random_distribution<float>& rng
    ) {
        const auto TBN =
            brdf::MakeTBNFromTangent(N, tangent, tangent_sign);

        const glm::vec3 Vt = glm::transpose(TBN) * wi;
        if (Vt.z <= 0.0f)
            return std::nullopt;

        const auto Ht = brdf::SampleGGXVNDF(Vt, alpha, rng.uniform(), rng.uniform());
        const auto Lt = glm::reflect(-Vt, Ht);
        if (Lt.z <= 0.0f)
            return std::nullopt;

        const auto L = TBN * Lt;

        const auto pdf = GGX_VNDF_PDF_L(Vt, Ht, alpha);
        if (pdf <= 0.0f)
            return std::nullopt;

        const auto NoL =
            glm::max(glm::dot(N, L), 0.0f);

        const auto F = brdf::ggx_brdf(wi, L, N, albedo, alpha, metallic);

        return BSDFSample{
            .direction = L,
            .weight = F * NoL / pdf,
            .pdf = pdf
        };
    }

    inline std::optional<BSDFSample> sample_dielectric(glm::vec3 wi,
                                                       glm::vec3 N,
                                                       float outside_eta,
                                                       float inside_eta,
                                                       float alpha,
                                                       const glm::vec3& tangent,
                                                       float tangent_sign,
                                                       random_distribution<float>& rng
    ) {
        bool entering = glm::dot(N, wi) > 0.0f;
        float etaI = entering ? outside_eta : inside_eta;
        float etaT = entering ? inside_eta : outside_eta;
        glm::vec3 Nf = entering ? N : -N;

        const auto TBN = brdf::MakeTBNFromTangent(Nf, tangent, tangent_sign);
        const auto Vt = glm::transpose(TBN) * wi;
        if (Vt.z <= 0.0f)
            return std::nullopt;

        const auto Ht = brdf::SampleGGXVNDF(Vt, alpha, rng.uniform(), rng.uniform());

        const float cosThetaI = glm::dot(Vt, Ht);
        const float F = fresnel_dielectric(cosThetaI, etaI, etaT);

        if (rng.uniform() < F) {
            glm::vec3 Lt = glm::reflect(-Vt, Ht);
            float NoV = Vt.z;
            float NoL = std::abs(Lt.z);
            float ratio = brdf::Smith_G2_over_G1(NoV, NoL, alpha);

            glm::vec3 L = TBN * Lt;
            return BSDFSample{ glm::normalize(L), glm::vec3(ratio), 1.0f };
        } else {
            glm::vec3 Lt = glm::refract(-Vt, Ht, etaI / etaT);
            if (glm::length2(Lt) < 1e-12f) return std::nullopt;

            float NoV = Vt.z;
            float NoL = std::abs(Lt.z);
            float ratio = brdf::Smith_G2_over_G1(NoV, NoL, alpha);
            float weight = ratio * (etaI * etaI) / (etaT * etaT);

            glm::vec3 L = TBN * Lt;
            return BSDFSample{ glm::normalize(L), glm::vec3(weight), 1.0f };
        }
    }
}
#endif //BRDF_H
