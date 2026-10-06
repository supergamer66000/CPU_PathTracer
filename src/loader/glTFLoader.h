//
// Created by super on 11/05/2026.
//

#ifndef CPU_PATHTRACER_GLTFLOADER_H
#define CPU_PATHTRACER_GLTFLOADER_H

#include <glm/glm.hpp>
#include <filesystem>
#include <functional>
#include <optional>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include "../types/types.h"


namespace cpt {
    namespace Types {
        enum class LightType { Directional, Spot, Point };
        struct LightInfo {
            std::string name;
            LightType type = LightType::Point;
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 color = glm::vec3(1.0f);
            float intensity = 1.0f;
            std::optional<float> range;
            float inner_cone_angle = 0.0f;
            float outer_cone_angle = glm::quarter_pi<float>();
        };

        enum class CameraPerspective { Perspective, Orthographic };
        struct CameraInfo {
            std::string name;
            CameraPerspective type = CameraPerspective::Perspective;
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
            float yfov = glm::half_pi<float>();
            std::optional<float> aspect_ratio;
            float znear = 0.01f;
            std::optional<float> zfar;
            float xmag = 1.0f;
            float ymag = 1.0f;
        };
    }

    class glTFLoader {
    public:
        /**
         * Loads gltf
         * @param path path to .gltf or .glb
         */
        glTFLoader(std::filesystem::path path);

        /**
         * Iterates through every mesh and gets every
         * texture material connect to said mesh
         * @param fun (auto mesh, auto material)
         */
        void iterator(const std::function<void(Mesh, Material)> &fun) const
        {
            for (const auto &mesh : meshes)
            {
                const Material* mat = mesh.material_index < materials.size()
                    ? &materials[mesh.material_index] : nullptr;
                fun(mesh, mat ? *mat : Material{});
            }
        }

        /**
         * Iterates with a size_t as material
         * @param fun
         */
        void iterator(const std::function<void(Mesh, size_t)> &fun) const
        {
            for (const auto &mesh : meshes)
            {
                size_t material_index = mesh.material_index < materials.size()
                    ? mesh.material_index
                    : SIZE_MAX; // sentinel for "no material" / out of range
                fun(mesh, material_index);
            }
        }

        /**
         * Iterates with a size_t as material
         * @param fun
         */
        void iterator(const std::function<void(size_t, size_t)> &fun) const
        {
            for (int i = 0; i < meshes.size() - 1; ++i) {
                auto mesh = meshes[i];
                size_t material_index = mesh.material_index < materials.size()
                    ? mesh.material_index
                    : SIZE_MAX; // sentinel for "no material" / out of range
                fun(i, material_index);
            }
        }

        /*
         * This moves ownership from loader to what ever I am loading into
         * This helps keep what own what clear
         */
        [[maybe_unused]] const std::vector<Mesh>& get_meshes() const { return meshes; }
        [[maybe_unused]] std::vector<Mesh>& get_meshes() { return meshes; }

        [[maybe_unused]] const std::vector<Texture>& get_textures() const { return textures; }
        [[maybe_unused]] std::vector<Texture>& get_textures() { return textures; }

        [[maybe_unused]] const std::vector<Material>& get_materials() const { return materials; }
        [[maybe_unused]] std::vector<Material>& get_materials() { return materials; }

        [[maybe_unused]] const std::vector<Types::LightInfo>& get_lights() const { return lights; }
        [[maybe_unused]] const std::vector<Types::CameraInfo>& get_cameras() const { return cameras; }
    private:
        // Extensions commonly used by real-world / production glTF scenes
        // (Intel's sample/ORCA scenes among them). Without these enabled,
        // fastgltf's Parser will hard-fail with "extensions required but
        // not enabled" on any asset that uses them, even though the file
        // itself is perfectly valid glTF.
        static constexpr auto supported_extensions =
                  fastgltf::Extensions::KHR_texture_transform
                | fastgltf::Extensions::KHR_materials_variants
                | fastgltf::Extensions::KHR_materials_unlit
                | fastgltf::Extensions::KHR_materials_emissive_strength
                | fastgltf::Extensions::KHR_materials_ior
                | fastgltf::Extensions::KHR_materials_specular
                | fastgltf::Extensions::KHR_materials_transmission
                | fastgltf::Extensions::KHR_materials_volume
                | fastgltf::Extensions::KHR_materials_clearcoat
                | fastgltf::Extensions::KHR_materials_sheen
                | fastgltf::Extensions::KHR_materials_iridescence
                | fastgltf::Extensions::KHR_lights_punctual
                | fastgltf::Extensions::KHR_texture_basisu
                | fastgltf::Extensions::KHR_mesh_quantization
                | fastgltf::Extensions::EXT_meshopt_compression
                | fastgltf::Extensions::EXT_mesh_gpu_instancing
                | fastgltf::Extensions::EXT_texture_webp;
                // NOTE: KHR_draco_mesh_compression is deliberately NOT
                // included here. fastgltf only decodes Draco-compressed
                // meshes if it was itself built with FASTGLTF_ENABLE_DRACO.
                // Enabling the flag without that build option will report
                // the extension as "supported" but then fail differently
                // during decompression. If you need it, rebuild fastgltf
                // with Draco support, then add the flag above.

        std::filesystem::path base_path;
        fastgltf::Parser parser;
        std::optional<fastgltf::Asset> asset;

        std::vector<Mesh> meshes;
        std::vector<Texture> textures;
        std::vector<Material> materials;

        std::vector<Types::LightInfo> lights;
        std::vector<Types::CameraInfo> cameras;

        template<typename Opt>
        std::optional<size_t> resolve_texture(const Opt& info);

        /**
         *  Loads mesh from the .glb or .gltf files for scenes
         */
        void load_mesh();

        /**
         * Recursively walks a node and its children, accumulating the
         * parent-to-node world transform, and emits a Mesh for every
         * primitive found on the way. Required for scenes (e.g. Sponza,
         * Bistro, and other Intel/ORCA-style test assets) that place
         * geometry several levels deep in the node hierarchy rather than
         * directly under the scene root.
         * @param node_index index of the node to process
         * @param parent_transform accumulated world transform of the parent
         */
        void process_node(size_t node_index, const glm::mat4& parent_transform);

        /**
         * Loads all the material in gltf
         */
        void load_materials();

        /**
         * Loads all the textures for a given gltf asset
         */
         void load_textures();


    };
}

#endif //CPU_PATHTRACER_GLTFLOADER_H
