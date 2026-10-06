#ifndef CPU_PATHTRACER_UTIL_H
#define CPU_PATHTRACER_UTIL_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <glm/vec4.hpp>
#include <embree4/rtcore_device.h>

namespace fastgltf { struct Asset; }
namespace cpt { struct Texture; }

namespace cpt::util {
    namespace path {
        std::filesystem::path get_global_working_directory();
        std::filesystem::path get_asset_path();
    }

    std::string wide_string_to_string(const std::wstring& wstr);
    std::string open_file_dialog();

    fastgltf::Asset load_gltf(std::string name);
    glm::vec4 sample(const std::vector<Texture>& textures, size_t texture_index,
                     float u, float v) noexcept;
    void check_embree_error(RTCDevice device, const std::string& where);
}

#endif // CPU_PATHTRACER_UTIL_H
