//
// Created by super on 26/05/2026.
//

#include "glTFLoader.h"
#include <fastgltf/tools.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace cpt {
    glTFLoader::glTFLoader(std::filesystem::path path)
        : base_path(path.parent_path()),
          parser(supported_extensions) {

        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Error loading path: " + path.string());
        }

        // GenerateMeshIndices only synthesizes an index buffer for
        // non-indexed primitives; it does NOT convert triangle-strip/fan
        // primitives into a triangle list, so those are still filtered
        // out explicitly in load_mesh().
        constexpr auto options =
                         fastgltf::Options::GenerateMeshIndices
                       | fastgltf::Options::LoadExternalBuffers
                       | fastgltf::Options::DecomposeNodeMatrices
                       | fastgltf::Options::LoadExternalImages;

        auto start = std::chrono::steady_clock::now();

        // GltfDataBuffer::FromPath sniffs the file's magic bytes itself,
        // and parser.loadGltf() auto-detects glTF (JSON) vs GLB (binary)
        // from that buffer — there is no separate "GLB path" to call.
        // .glb files were already being routed through the right loader;
        // the reason they (and many .gltf files) failed to load was the
        // Parser below having no extensions enabled, and errors from
        // this call/loadGltf being silently swallowed rather than checked.
        auto data = fastgltf::GltfDataBuffer::FromPath(path);
        if (data.error() != fastgltf::Error::None) {
            throw std::runtime_error(
                "Error reading glTF/GLB file '" + path.string() + "': " +
                std::string(fastgltf::getErrorMessage(data.error())));
        }

        auto result = parser.loadGltf(data.get(), path.parent_path(), options);
        if (result.error() != fastgltf::Error::None) {
            throw std::runtime_error(
                "Error parsing glTF/GLB asset '" + path.string() + "': " +
                std::string(fastgltf::getErrorMessage(result.error())));
        }
        asset.emplace(std::move(result.get()));

        try {
            load_mesh();
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Error loading meshes from '" + path.string() + "': " + e.what());
        }

        try {
            load_textures();
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Error loading textures from '" + path.string() + "': " + e.what());
        }

        try {
            load_materials();
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Error loading materials from '" + path.string() + "': " + e.what());
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << path.filename() << " Load Time: " << elapsed.count() << "ms\n";
    }

    namespace {
        // fastgltf::math matrices don't guarantee a layout-compatible
        // memory dump into glm, so copy element-by-element rather than
        // assuming pointer-cast safety.
        glm::mat4 to_glm(const fastgltf::math::fmat4x4& m) {
            glm::mat4 result;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    result[col][row] = m[col][row];
                }
            }
            return result;
        }

        thread_local std::unordered_set<size_t> visited_nodes;
    }

    void glTFLoader::load_mesh() {
        // NOTE: this loads every scene in the asset, matching the
        // previous behaviour. If you only want the asset's default
        // scene, iterate asset->scenes[asset->defaultScene.value_or(0)]
        // instead.
        visited_nodes.clear();
        for (const auto& scene : asset->scenes) {
            for (const auto nodeIndex : scene.nodeIndices) {
                if (nodeIndex >= asset->nodes.size()) {
                    throw std::runtime_error(
                        "Scene references out-of-range node index " + std::to_string(nodeIndex));
                }
                process_node(nodeIndex, glm::mat4(1.0f));
            }
        }
    }

    void glTFLoader::process_node(size_t node_index, const glm::mat4& parent_transform) {
        if (node_index >= asset->nodes.size()) {
            throw std::runtime_error(
                "Node index " + std::to_string(node_index) + " out of range");
        }

        if (!visited_nodes.insert(node_index).second) {
            throw std::runtime_error(
                "Cycle detected in node hierarchy at node " + std::to_string(node_index));
        }

        const auto& node = asset->nodes[node_index];

        // getFastLocalTransformMatrix handles both the TRS and raw-matrix
        // node representations for us, so this works whether or not
        // DecomposeNodeMatrices ends up applying to a given node.
        const glm::mat4 local_transform = to_glm(fastgltf::getLocalTransformMatrix(node));
        const glm::mat4 world_transform = parent_transform * local_transform;

        if (node.lightIndex) {
            if (*node.lightIndex >= asset->lights.size())
                throw std::runtime_error("Node references an out-of-range light");

            const auto& source = asset->lights[*node.lightIndex];
            Types::LightInfo light;
            light.name = source.name.empty()
                ? "Light " + std::to_string(*node.lightIndex)
                : std::string(source.name);
            light.position = glm::vec3(world_transform[3]);
            const glm::vec3 direction = glm::mat3(world_transform) * glm::vec3(0.0f, 0.0f, -1.0f);
            if (glm::dot(direction, direction) > 1e-12f)
                light.direction = glm::normalize(direction);
            light.color = { source.color.x(), source.color.y(), source.color.z() };
            light.intensity = static_cast<float>(source.intensity);
            if (source.range)
                light.range = static_cast<float>(*source.range);
            if (source.innerConeAngle)
                light.inner_cone_angle = static_cast<float>(*source.innerConeAngle);
            if (source.outerConeAngle)
                light.outer_cone_angle = static_cast<float>(*source.outerConeAngle);

            switch (source.type) {
                case fastgltf::LightType::Directional:
                    light.type = Types::LightType::Directional;
                    break;
                case fastgltf::LightType::Spot:
                    light.type = Types::LightType::Spot;
                    break;
                case fastgltf::LightType::Point:
                    light.type = Types::LightType::Point;
                    break;
            }
            lights.push_back(std::move(light));
        }

        if (node.cameraIndex) {
            if (*node.cameraIndex >= asset->cameras.size())
                throw std::runtime_error("Node references an out-of-range camera");

            const auto& source = asset->cameras[*node.cameraIndex];
            Types::CameraInfo camera;
            camera.name = source.name.empty()
                ? "Camera " + std::to_string(*node.cameraIndex)
                : std::string(source.name);
            camera.position = glm::vec3(world_transform[3]);
            const glm::mat3 orientation(world_transform);
            const glm::vec3 direction = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::dot(direction, direction) > 1e-12f)
                camera.direction = glm::normalize(direction);
            if (glm::dot(up, up) > 1e-12f)
                camera.up = glm::normalize(up);

            if (const auto* perspective =
                    std::get_if<fastgltf::Camera::Perspective>(&source.camera)) {
                camera.type = Types::CameraPerspective::Perspective;
                camera.yfov = static_cast<float>(perspective->yfov);
                camera.znear = static_cast<float>(perspective->znear);
                if (perspective->aspectRatio)
                    camera.aspect_ratio = static_cast<float>(*perspective->aspectRatio);
                if (perspective->zfar)
                    camera.zfar = static_cast<float>(*perspective->zfar);
            } else if (const auto* orthographic =
                           std::get_if<fastgltf::Camera::Orthographic>(&source.camera)) {
                camera.type = Types::CameraPerspective::Orthographic;
                camera.xmag = static_cast<float>(orthographic->xmag);
                camera.ymag = static_cast<float>(orthographic->ymag);
                camera.znear = static_cast<float>(orthographic->znear);
                camera.zfar = static_cast<float>(orthographic->zfar);
            }
            cameras.push_back(std::move(camera));
        }

        if (node.meshIndex.has_value()) {
            if (node.meshIndex.value() >= asset->meshes.size()) {
                throw std::runtime_error(
                    "Node " + std::to_string(node_index) + " references out-of-range mesh index " +
                    std::to_string(node.meshIndex.value()));
            }
            const auto& mesh = asset->meshes[node.meshIndex.value()];

            // Normals/tangents transform by the inverse-transpose (the
            // "normal matrix") so non-uniform scale doesn't distort them;
            // positions transform directly by the world matrix.
            const glm::mat3 normal_matrix =
                glm::transpose(glm::inverse(glm::mat3(world_transform)));

            for (const auto& primitive : mesh.primitives) {
                // Checked up-front now: GenerateMeshIndices only synthesizes
                // an index buffer for non-indexed primitives, it does not
                // convert strip/fan topology into a triangle list, so those
                // primitives must be skipped before any buffers are built,
                // not partway through index construction.
                if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                    continue;
                }

                Mesh mesh_inst;
                if (!mesh.name.empty()) {
                    mesh_inst.name = mesh.name;
                } else {
                    mesh_inst.name = "MESH: " + std::to_string(node.meshIndex.value());
                }

                if (primitive.materialIndex.has_value()) {
                    mesh_inst.material_index = primitive.materialIndex.value();
                }

                // INDICES //
                if (primitive.indicesAccessor.has_value()) {
                    if (primitive.indicesAccessor.value() >= asset->accessors.size()) {
                        throw std::runtime_error(
                            "Primitive references out-of-range indices accessor " +
                            std::to_string(primitive.indicesAccessor.value()));
                    }
                    const auto& accessor =
                        asset->accessors[primitive.indicesAccessor.value()];

                    std::vector<uint32_t> flat_indices(accessor.count);

                    fastgltf::iterateAccessorWithIndex<uint32_t>(
                        *asset,
                        accessor,
                        [&](uint32_t index, std::size_t idx) {
                            flat_indices[idx] = index;
                        });

                    mesh_inst.indices.resize(flat_indices.size() / 3);
                    for (size_t i = 0; i + 2 < flat_indices.size(); i += 3) {
                        mesh_inst.indices[i / 3] = glm::uvec3(
                            flat_indices[i],
                            flat_indices[i + 1],
                            flat_indices[i + 2]
                        );
                    }
                }

                // VERTICES // — baked into world space via world_transform
                auto posAttribute = primitive.findAttribute("POSITION");
                if (posAttribute != primitive.attributes.end()) {
                    if (posAttribute->accessorIndex >= asset->accessors.size()) {
                        throw std::runtime_error(
                            "Primitive references out-of-range POSITION accessor " +
                            std::to_string(posAttribute->accessorIndex));
                    }
                    const auto& accessor =
                        asset->accessors[posAttribute->accessorIndex];
                    mesh_inst.vertices.resize(accessor.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                        *asset,
                        accessor,
                        [&](fastgltf::math::fvec3 pos, std::size_t idx) {
                            const glm::vec4 world_pos = world_transform *
                                glm::vec4(pos.x(), pos.y(), pos.z(), 1.0f);
                            mesh_inst.vertices[idx] = glm::vec3(world_pos);
                        });
                }

                // NORMALS // — baked into world space via normal_matrix
                auto normAttribute = primitive.findAttribute("NORMAL");
                if (normAttribute != primitive.attributes.end()) {
                    if (normAttribute->accessorIndex >= asset->accessors.size()) {
                        throw std::runtime_error(
                            "Primitive references out-of-range NORMAL accessor " +
                            std::to_string(normAttribute->accessorIndex));
                    }
                    const auto& accessor =
                        asset->accessors[normAttribute->accessorIndex];
                    mesh_inst.normals.resize(accessor.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                        *asset,
                        accessor,
                        [&](fastgltf::math::fvec3 n, std::size_t idx) {
                            mesh_inst.normals[idx] = glm::normalize(
                                normal_matrix * glm::vec3(n.x(), n.y(), n.z()));
                        });
                } else {
                    // No NORMAL attribute in source data. Left empty rather
                    // than guessed at — generate flat face normals from
                    // mesh_inst.indices/vertices downstream if your
                    // pipeline requires one per vertex.
                }

                // TEX COORDS //
                auto texAttribute = primitive.findAttribute("TEXCOORD_0");
                if (texAttribute != primitive.attributes.end()) {
                    if (texAttribute->accessorIndex >= asset->accessors.size()) {
                        throw std::runtime_error(
                            "Primitive references out-of-range TEXCOORD_0 accessor " +
                            std::to_string(texAttribute->accessorIndex));
                    }
                    const auto& accessor =
                        asset->accessors[texAttribute->accessorIndex];
                    mesh_inst.uv.resize(accessor.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                        *asset,
                        accessor,
                        [&](fastgltf::math::fvec2 uv, std::size_t idx) {
                            mesh_inst.uv[idx] = { uv.x(), uv.y() };
                        });
                }

                // TANGENTS // — xyz baked via world_transform (not the
                // normal matrix — tangents follow the surface direction,
                // not the surface normal), w (handedness) passed through.
                auto tanAttribute = primitive.findAttribute("TANGENT");
                if (tanAttribute != primitive.attributes.end()) {
                    if (tanAttribute->accessorIndex >= asset->accessors.size()) {
                        throw std::runtime_error(
                            "Primitive references out-of-range TANGENT accessor " +
                            std::to_string(tanAttribute->accessorIndex));
                    }
                    const auto& accessor = asset->accessors[tanAttribute->accessorIndex];
                    mesh_inst.tangents.emplace(accessor.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                        *asset, accessor,
                        [&](fastgltf::math::fvec4 t, std::size_t idx) {
                            const glm::vec3 transformed = glm::mat3(world_transform) *
                                glm::vec3(t.x(), t.y(), t.z());
                            const float length_sq = glm::dot(transformed, transformed);
                            const glm::vec3 world_tangent = length_sq > 1e-12f
                                ? transformed / std::sqrt(length_sq)
                                : glm::vec3(1.0f, 0.0f, 0.0f);
                            (*mesh_inst.tangents)[idx] = glm::vec4(world_tangent, t.w());
                        });
                } else {
                    mesh_inst.tangents = std::nullopt;
                }

                if (mesh_inst.vertices.empty())
                    continue;

                if (mesh_inst.indices.empty()) {
                    mesh_inst.indices.reserve(mesh_inst.vertices.size() / 3);
                    for (size_t i = 0; i + 2 < mesh_inst.vertices.size(); i += 3)
                        mesh_inst.indices.emplace_back(i, i + 1, i + 2);
                }

                if (mesh_inst.indices.empty())
                    continue;

                if (mesh_inst.uv.size() != mesh_inst.vertices.size())
                    mesh_inst.uv.resize(mesh_inst.vertices.size(), glm::vec2(0.0f));

                if (mesh_inst.tangents &&
                    mesh_inst.tangents->size() != mesh_inst.vertices.size())
                    mesh_inst.tangents.reset();

                meshes.push_back(mesh_inst);
            }
        }

        // Recurse into children so meshes attached deeper in the node
        // hierarchy (as in Sponza/Bistro-style scenes) actually get
        // loaded instead of silently skipped.
        for (const auto child_index : node.children) {
            process_node(child_index, world_transform);
        }

        visited_nodes.erase(node_index);
    }

    template<typename Opt>
    std::optional<size_t> glTFLoader::resolve_texture(const Opt& info) {
        if (!info.has_value()) return std::nullopt;

        size_t tex_index = info->textureIndex; // inherited member — works for all 3 types
        if (tex_index >= asset->textures.size()) return std::nullopt;

        const auto& gltfTexture = asset->textures[tex_index];
        if (!gltfTexture.imageIndex.has_value()) return std::nullopt;

        size_t image_slot = gltfTexture.imageIndex.value();
        if (image_slot >= textures.size()) return std::nullopt;

        return image_slot;
    }

    void glTFLoader::load_materials() {
        materials.reserve(asset->materials.size());

        for (const auto& mat : asset->materials) {
            Material mat_inst;

            mat_inst.base_color_factor = glm::vec4(
                mat.pbrData.baseColorFactor.x(), mat.pbrData.baseColorFactor.y(),
                mat.pbrData.baseColorFactor.z(), mat.pbrData.baseColorFactor.w()
            );
            mat_inst.metallic_factor = mat.pbrData.metallicFactor;
            mat_inst.roughness_factor = mat.pbrData.roughnessFactor;
            mat_inst.eta_t = static_cast<float>(mat.ior);
            mat_inst.unlit = mat.unlit;

            mat_inst.emissive_factor = glm::vec3(
                mat.emissiveFactor.x(), mat.emissiveFactor.y(), mat.emissiveFactor.z()
            );
            mat_inst.emissive_multiplier = static_cast<float>(mat.emissiveStrength);

            mat_inst.alpha_mode = static_cast<AlphaMode>(mat.alphaMode);
            mat_inst.alphaz_cutoff = mat.alphaCutoff;
            mat_inst.double_sided = mat.doubleSided;

            mat_inst.base_color_texture = resolve_texture(mat.pbrData.baseColorTexture);
            mat_inst.metallic_roughness_texture = resolve_texture(mat.pbrData.metallicRoughnessTexture);
            mat_inst.emissive_texture = resolve_texture(mat.emissiveTexture);

            // Pass texture reference with scaler
            if (auto tex = resolve_texture(mat.normalTexture)) {
                mat_inst.normal_texture = NormalTextureRef{ *tex, mat.normalTexture->scale };
            }

            if (auto tex = resolve_texture(mat.occlusionTexture)) {
                mat_inst.occlusion_texture = OcclusionTextureRef{ *tex, mat.occlusionTexture->strength };
            }

            if (mat.transmission != nullptr) {
                mat_inst.transmission_factor =
                    static_cast<float>(mat.transmission->transmissionFactor);
                if (auto tex = resolve_texture(mat.transmission->transmissionTexture)) {
                    mat_inst.transmission_texture = *tex;
                }
            }

            materials.push_back(mat_inst);
        }
    }

    void glTFLoader::load_textures() {
        textures.resize(asset->images.size());

        for (size_t i = 0; i < asset->images.size(); i++) {
            int width, height, channels;
            unsigned char *data = nullptr;

            std::visit(fastgltf::visitor {
                [](auto&) {
                    std::cerr << "Unknown Type\n";
                },

                [&](fastgltf::sources::URI &path) {
                    std::cout << "[Texture " << i << "] Loaded from URI: "
                              << path.uri.fspath().string() << "\n";

                    const auto image_path = base_path / path.uri.fspath();
                    data = stbi_load(
                        image_path.string().c_str(),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    channels = 4;
                },

                [&](fastgltf::sources::Vector &vector) {
                    std::cout << "[Texture " << i << "] Loaded from in-memory Vector ("
                              << vector.bytes.size() << " bytes)\n";

                    data = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(vector.bytes.data()),
                        static_cast<int>(vector.bytes.size()),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    channels = 4;
                },
                [&](fastgltf::sources::BufferView &view) {
                    if (view.bufferViewIndex >= asset->bufferViews.size()) {
                        std::cerr << "Texture " << i << " references out-of-range bufferView "
                                  << view.bufferViewIndex << "\n";
                        return;
                    }
                    const auto &bufferView = asset->bufferViews[view.bufferViewIndex];

                    if (bufferView.bufferIndex >= asset->buffers.size()) {
                        std::cerr << "Texture " << i << " bufferView references out-of-range buffer "
                                  << bufferView.bufferIndex << "\n";
                        return;
                    }
                    const auto &buffer = asset->buffers[bufferView.bufferIndex];

                    std::cout << "[Texture " << i << "] Loaded from BufferView "
                              << view.bufferViewIndex << " (buffer "
                              << bufferView.bufferIndex << ", offset "
                              << bufferView.byteOffset << ", length "
                              << bufferView.byteLength << ")\n";

                    std::visit(fastgltf::visitor{
                        [&](const fastgltf::sources::Vector &vector) {

                            const std::byte* base = vector.bytes.data();
                            const std::byte* bytes = base + bufferView.byteOffset;
                            size_t size = bufferView.byteLength;

                            if (bufferView.byteOffset + size > vector.bytes.size()) {
                                std::cerr << "Out-of-bounds bufferView\n";
                                return;
                            }

                            data = stbi_load_from_memory(
                                reinterpret_cast<const stbi_uc*>(bytes),
                                static_cast<int>(size),
                                &width,
                                &height,
                                &channels,
                                STBI_rgb_alpha
                            );

                            channels = 4;
                        },

                        // fastgltf's own examples source embedded buffer-view data as
                        // sources::Array rather than sources::Vector depending on load
                        // options, so this branch is needed too or embedded images can
                        // silently fail to load.
                        [&](const fastgltf::sources::Array &array) {

                            const std::byte* base = array.bytes.data();
                            const std::byte* bytes = base + bufferView.byteOffset;
                            size_t size = bufferView.byteLength;

                            if (bufferView.byteOffset + size > array.bytes.size()) {
                                std::cerr << "Out-of-bounds bufferView\n";
                                return;
                            }

                            data = stbi_load_from_memory(
                                reinterpret_cast<const stbi_uc*>(bytes),
                                static_cast<int>(size),
                                &width,
                                &height,
                                &channels,
                                STBI_rgb_alpha
                            );

                            channels = 4;
                        },

                        [&](const auto&) {
                            std::cerr << "Unsupported buffer type\n";
                        }

                    }, buffer.data);
                },
                [&](fastgltf::sources::ByteView &view) {
                    std::cout << "[Texture " << i << "] Loaded from ByteView ("
                              << view.bytes.size() << " bytes)\n";

                    data = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(view.bytes.data()),
                        static_cast<int>(view.bytes.size()),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    channels = 4;
                },
                [&](fastgltf::sources::Array &array) {
                    std::cout << "[Texture " << i << "] Loaded from Array ("
                              << array.bytes.size() << " bytes)\n";

                    data = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(array.bytes.data()),
                        static_cast<int>(array.bytes.size()),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    channels = 4;
                },
            }, asset->images[i].data);

            if (!data) {
                std::cerr << "Failed to load image " << i << ": "
                          << stbi_failure_reason() << "\n";
                continue;
            }

            if (width <= 0 || height <= 0) {
                std::cerr << "Invalid dimensions for image " << i << ": "
                          << width << "x" << height << "\n";
                stbi_image_free(data);
                continue;
            }

            const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
            constexpr size_t max_pixel_count = static_cast<size_t>(16384) * 16384;
            if (pixel_count > max_pixel_count) {
                std::cerr << "Image " << i << " exceeds maximum supported dimensions: "
                          << width << "x" << height << "\n";
                stbi_image_free(data);
                continue;
            }

            try {
                Texture tex{};
                tex.width = width;
                tex.height = height;
                tex.pixels.assign(data, data + (pixel_count * 4));
                textures[i] = std::move(tex);
            } catch (const std::exception& e) {
                std::cerr << "Failed to store pixel data for image " << i << ": " << e.what() << "\n";
                stbi_image_free(data);
                continue;
            }

            stbi_image_free(data);
        }
    }
}
