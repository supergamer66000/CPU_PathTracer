//
// Created by super on 04/09/2026.
//

#ifndef CPU_PATHTRACER_MIPMAPS_H
#define CPU_PATHTRACER_MIPMAPS_H

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>
#include <utility>
#include "../types/types.h"

// ---------------------------------------------------------------------
// Mipmapped texture sampling.
//
// Usage:
//   1. Build the chain ONCE per texture, right after loading:
//        MippedTexture mipped = build_mipmaps(&texture);
//      Do NOT call build_mipmaps() per-sample — it walks the whole image.
//   2. Sample as many times as you like:
//        glm::vec4 c = sample(mipped, u, v, lod);
//
//   `lod` is level-of-detail, 0 = full resolution, increasing = blurrier.
//   You are responsible for computing lod from your ray footprint (see
//   compute_lod() below for a simple starting point using UV derivatives,
//   e.g. from ray differentials or a footprint/cone-tracing estimate).
//   This is isotropic trilinear filtering, not anisotropic — a single lod
//   value collapses u- and v-direction footprints together, so texture
//   viewed at a grazing angle will over-blur relative to true anisotropic
//   filtering. That's a reasonable next step if you need it later.
// ---------------------------------------------------------------------
namespace cpt::mipmaps {
        struct MipLevel {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels; // RGBA8, same layout as glTFLoader::Texture
        };

        struct MippedTexture {
            const cpt::Texture* base = nullptr; // level 0 (owns the original pixels)
            std::vector<MipLevel> mips;               // mips[0] = base/2, ..., mips.back() = 1x1
        };

        // Builds the full mip chain via 2x2 box filtering, down to 1x1.
        // Call this once per texture after loading, not on the sampling hot path.
        inline MippedTexture build_mipmaps(const cpt::Texture* texture)
        {
            MippedTexture mt;
            mt.base = texture;

            if (!texture || texture->width <= 0 || texture->height <= 0 ||
                texture->pixels.size() < static_cast<size_t>(texture->width) * texture->height * 4) {
                return mt;
            }

            int w = texture->width;
            int h = texture->height;
            const unsigned char* src = texture->pixels.data();

            while (w > 1 || h > 1) {
                const int nw = std::max(1, w / 2);
                const int nh = std::max(1, h / 2);

                MipLevel level;
                level.width = nw;
                level.height = nh;
                level.pixels.resize(static_cast<size_t>(nw) * nh * 4);

                for (int y = 0; y < nh; ++y) {
                    const int y0 = std::min(y * 2, h - 1);
                    const int y1 = std::min(y * 2 + 1, h - 1);

                    for (int x = 0; x < nw; ++x) {
                        const int x0 = std::min(x * 2, w - 1);
                        const int x1 = std::min(x * 2 + 1, w - 1);

                        for (int c = 0; c < 4; ++c) {
                            const int sum =
                                src[(static_cast<size_t>(y0) * w + x0) * 4 + c] +
                                src[(static_cast<size_t>(y0) * w + x1) * 4 + c] +
                                src[(static_cast<size_t>(y1) * w + x0) * 4 + c] +
                                src[(static_cast<size_t>(y1) * w + x1) * 4 + c];

                            level.pixels[(static_cast<size_t>(y) * nw + x) * 4 + c] =
                                static_cast<unsigned char>(sum / 4);
                        }
                    }
                }

                mt.mips.push_back(std::move(level));
                src = mt.mips.back().pixels.data();
                w = nw;
                h = nh;
            }

            return mt;
        }

        namespace detail {
            inline void get_level(const MippedTexture& tex, int level,
                                   const unsigned char*& pixels, int& w, int& h)
            {
                if (level <= 0 || tex.mips.empty()) {
                    pixels = tex.base->pixels.data();
                    w = tex.base->width;
                    h = tex.base->height;
                    return;
                }
                const size_t idx = std::min(static_cast<size_t>(level - 1), tex.mips.size() - 1);
                pixels = tex.mips[idx].pixels.data();
                w = tex.mips[idx].width;
                h = tex.mips[idx].height;
            }

            // Bilinear sample within a single mip level, with wrap-around addressing
            // to match the original function's `u - floor(u)` repeat behaviour.
            inline glm::vec4 bilinear(const unsigned char* pixels, int w, int h, float u, float v)
            {
                u = u - floorf(u);
                v = v - floorf(v);

                const float fx = u * static_cast<float>(w) - 0.5f;
                const float fy = v * static_cast<float>(h) - 0.5f;

                const int x0 = static_cast<int>(floorf(fx));
                const int y0 = static_cast<int>(floorf(fy));
                const float tx = fx - static_cast<float>(x0);
                const float ty = fy - static_cast<float>(y0);

                auto wrap = [](int value, int n) {
                    value %= n;
                    if (value < 0) value += n;
                    return value;
                };

                const int x0w = wrap(x0, w);
                const int x1w = wrap(x0 + 1, w);
                const int y0w = wrap(y0, h);
                const int y1w = wrap(y0 + 1, h);

                constexpr float inv255 = 1.0f / 255.0f;
                auto fetch = [&](int x, int y) {
                    const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
                    return glm::vec4(
                        pixels[idx + 0] * inv255,
                        pixels[idx + 1] * inv255,
                        pixels[idx + 2] * inv255,
                        pixels[idx + 3] * inv255
                    );
                };

                const glm::vec4 c00 = fetch(x0w, y0w);
                const glm::vec4 c10 = fetch(x1w, y0w);
                const glm::vec4 c01 = fetch(x0w, y1w);
                const glm::vec4 c11 = fetch(x1w, y1w);

                const glm::vec4 top = glm::mix(c00, c10, tx);
                const glm::vec4 bottom = glm::mix(c01, c11, tx);
                return glm::mix(top, bottom, ty);
            }

        } // namespace detail

        // Trilinear-filtered sample: bilinear within each of the two nearest mip
        // levels, blended by the fractional part of `lod`.
        inline glm::vec4 sample(const MippedTexture& texture, float u, float v, float lod = 0.0f) noexcept
        {
            if (!texture.base || texture.base->width <= 0 || texture.base->height <= 0 ||
                texture.base->pixels.size() < static_cast<size_t>(texture.base->width) * texture.base->height * 4) {
                return {1, 0, 1, 1};
            }

            const int max_level = static_cast<int>(texture.mips.size());
            lod = std::clamp(lod, 0.0f, static_cast<float>(max_level));

            const int level0 = static_cast<int>(floorf(lod));
            const int level1 = std::min(level0 + 1, max_level);
            const float frac = lod - static_cast<float>(level0);

            const unsigned char* p0; int w0, h0;
            detail::get_level(texture, level0, p0, w0, h0);
            const glm::vec4 c0 = detail::bilinear(p0, w0, h0, u, v);

            if (frac <= 0.0f || level0 == level1) {
                return c0;
            }

            const unsigned char* p1; int w1, h1;
            detail::get_level(texture, level1, p1, w1, h1);
            const glm::vec4 c1 = detail::bilinear(p1, w1, h1, u, v);

            return glm::mix(c0, c1, frac);
        }

        // Optional helper: turns per-pixel UV derivatives (e.g. from ray
        // differentials, or a footprint estimate for a path-traced ray) into a
        // lod value. `duv_dx`/`duv_dy` are how much u,v change per screen pixel
        // in the x and y directions respectively.
        inline float compute_lod(const MippedTexture& texture, glm::vec2 duv_dx, glm::vec2 duv_dy)
        {
            if (!texture.base || texture.base->width <= 0 || texture.base->height <= 0) {
                return 0.0f;
            }

            const glm::vec2 texel_dx = duv_dx * glm::vec2(texture.base->width, texture.base->height);
            const glm::vec2 texel_dy = duv_dy * glm::vec2(texture.base->width, texture.base->height);

            const float max_len_sq = std::max(glm::dot(texel_dx, texel_dx), glm::dot(texel_dy, texel_dy));
            if (max_len_sq <= 0.0f) return 0.0f;

            return std::max(0.0f, 0.5f * log2f(max_len_sq));
        }

        // world_footprint_radius: how big one pixel is, in world units, at this hit's distance.
        // uv_density: HitInfo::uv_density, the triangle-intrinsic uv-area/world-area ratio.
        inline float compute_lod_from_footprint(const MippedTexture& texture,
                                                 float world_footprint_radius,
                                                 float uv_density)
        {
            if (!texture.base || texture.base->width <= 0 || texture.base->height <= 0) {
                return 0.0f;
            }

            const float world_area = world_footprint_radius * world_footprint_radius; // square approx, no pi
            const float texel_area_per_world_area =
                uv_density * static_cast<float>(texture.base->width) * static_cast<float>(texture.base->height);

            const float texels_covered = world_area * texel_area_per_world_area;
            return std::max(0.0f, 0.5f * log2f(std::max(1.0f, texels_covered)));
        }
}
#endif //CPU_PATHTRACER_MIPMAPS_H
