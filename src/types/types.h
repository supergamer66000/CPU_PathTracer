//
// Created by super on 07/09/2026.
//

#ifndef GLTF_TYPES_H
#define GLTF_TYPES_H

#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <embree4/rtcore_geometry.h>

namespace cpt {
    /*
     * Contains all of the data for a embree Mesh
     * This is for Object instancing for translation
     */
    struct EmbreeMeshInfo {
        RTCScene owner_scene = nullptr;
        RTCScene object_scene = nullptr;
        unsigned int instance = RTC_INVALID_GEOMETRY_ID;
        unsigned int geometry = RTC_INVALID_GEOMETRY_ID;
    };

    /**
     * Contains all of the data to create a mesh
     */
    struct Mesh {
        std::string name;
        EmbreeMeshInfo embree_info;

        std::vector<glm::vec3> vertices;
        std::vector<glm::vec3> normals;
        std::vector<glm::uvec3> indices;
        std::optional<std::vector<glm::vec4>> tangents;

        /**
         *  Texture Coords (e.g UV's)
         */
        std::vector<glm::vec2> uv;

        /**
         *  Stores the index for material
         */
        size_t material_index = SIZE_MAX;
    };

    /**
     * Stores a loaded texture
     */
    struct Texture {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels; // RGBA8 format (0xAABBGGRR)
    };
    struct NormalTextureRef {
        size_t texture_index = SIZE_MAX;
        float scale = 1.0f;
    };

    struct OcclusionTextureRef {
        size_t texture_index = SIZE_MAX;
        float strength = 1.0f;
    };

    struct TextureRef {
        size_t texture_index = SIZE_MAX; // SIZE_MAX = "not present"
        size_t tex_coord_index = 0;
    };

    enum AlphaMode {
        Opaque,
        Mask,
        Blend,
    };

    /**
     *
     */
    struct Material {
        glm::vec4 base_color_factor = glm::vec4(1.0f);           // RGBA
        float metallic_factor = 1.0f;
        float roughness_factor = 1.0f;
        float eta_i = 1.0f;
        float eta_t = 1.5f;
        float transmission_factor = 0.0f;

        std::optional<size_t> base_color_texture;
        std::optional<size_t> metallic_roughness_texture;

        // Other common textures
        std::optional<NormalTextureRef> normal_texture;
        std::optional<OcclusionTextureRef> occlusion_texture;
        std::optional<size_t> transmission_texture;
        std::optional<size_t> emissive_texture;

        glm::vec3 emissive_factor = glm::vec3(0.0f);
        float emissive_multiplier = 1.0f;

        // Alpha / Transparency
        AlphaMode alpha_mode = Opaque;
        float alphaz_cutoff = 0.5f;

        // Rendering
        bool double_sided = false;
        bool unlit = false;
    };

    /*
     *  This is where the data for lights are located
     *  This is for types power and color;
     */
    struct Light {
        std::string name;
        glm::vec3 color = {1.f, 1.f, 1.f};
        float power = 1.f;
        float radius = 0.2f;
        RTCScene owner_scene = nullptr;
    };

    struct LightSample {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 emission;
        float pdf_area;
    };
};

#endif //GLTF_TYPES_H
