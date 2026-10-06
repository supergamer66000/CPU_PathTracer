#include "util.h"
#include "types/types.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <fastgltf/core.hpp>

#if WIN32
#include <windows.h>
#endif
#include <shobjidl.h>

namespace cpt::util {
    namespace path {
#if WIN32
        std::filesystem::path get_global_working_directory() {
            WCHAR pathW[260];
            GetModuleFileNameW(nullptr, pathW, MAX_PATH);
            std::filesystem::path path = pathW;
            return path.parent_path();
        }
#else
        std::filesystem::path get_global_working_directory() {
            throw "Unable the get current working path";
        }
#endif
        std::filesystem::path get_asset_path() {
            #ifdef CPT_ASSET_DIRECTORY
            return std::filesystem::path(CPT_ASSET_DIRECTORY);
#else
            return get_global_working_directory().append("assets");
#endif
        }
    }

    fastgltf::Asset load_gltf(std::string name) {
        auto path = path::get_asset_path().append(name);
        if (!std::filesystem::exists(path)) {
            std::cerr << "Failed to find " << path << '\n';
        }
        fastgltf::Parser parser;

        constexpr auto gltfOptions =
            fastgltf::Options::DontRequireValidAssetMember |
            fastgltf::Options::AllowDouble |
            fastgltf::Options::LoadExternalBuffers |
            fastgltf::Options::LoadExternalImages |
            fastgltf::Options::GenerateMeshIndices;

        auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
        if (!static_cast<bool>(gltfFile)) {
            std::cerr << "Failed to open glTF file: " << fastgltf::getErrorMessage(gltfFile.error()) << '\n';
        }

        auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
        if (asset.error() != fastgltf::Error::None) {
            std::cerr << "Failed to load glTF: " << fastgltf::getErrorMessage(asset.error()) << '\n';
        }

        if (!asset) {
            std::cerr << "Failed to load glTF" << fastgltf::getErrorMessage(asset.error()) << "\n";
        }
        return std::move(asset.get());
    }

    glm::vec4 sample(const std::vector<Texture>& textures, size_t texture_index, float u, float v) noexcept
    {
        if (texture_index >= textures.size())
            return {1, 0, 1, 1};

        const auto& texture = textures[texture_index];

        if (texture.pixels.size() < 4 || texture.width <= 0 || texture.height <= 0)
            return {1, 0, 1, 1};

        u = u - floorf(u);
        v = v - floorf(v);

        const int x = std::min(static_cast<int>(u * static_cast<float>(texture.width)), texture.width - 1);
        const int y = std::min(static_cast<int>(v * static_cast<float>(texture.height)), texture.height - 1);

        const size_t idx = (static_cast<size_t>(y) * texture.width + x) * 4;

        if (idx + 3 >= texture.pixels.size())
            return {1, 0, 1, 1};

        const unsigned char* p = texture.pixels.data() + idx;

        constexpr float inv255 = 1.0f / 255.0f;

        return {
            p[0] * inv255,
            p[1] * inv255,
            p[2] * inv255,
            p[3] * inv255
        };
    }

    std::string wide_string_to_string(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    std::string open_file_dialog() {
        std::string filePath = "";

        // 1. Initialize the COM library
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr)) return "";

        // 2. Create the FileOpenDialog object
        IFileOpenDialog* pFileOpen = nullptr;
        hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                              IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

        if (SUCCEEDED(hr)) {
            // 3. Set file type filters (Optional: Modify or remove if you want all files)
            COMDLG_FILTERSPEC fileTypes[] = {
                { L"Text Files (*.gltf)", L"*.gltf" },
                { L"Text Files (*.glb)", L"*.glb" },
                { L"All Files (*.*)", L"*.*" }
            };
            pFileOpen->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);

            // 4. Show the Open dialog box
            hr = pFileOpen->Show(NULL);

            // 5. Retrieve the file name if the user clicked "Open"
            if (SUCCEEDED(hr)) {
                IShellItem* pItem = nullptr;
                hr = pFileOpen->GetResult(&pItem);

                if (SUCCEEDED(hr)) {
                    PWSTR pszFilePath = nullptr;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                    if (SUCCEEDED(hr)) {
                        // Convert the wide string path to std::string
                        filePath = wide_string_to_string(pszFilePath);
                        CoTaskMemFree(pszFilePath); // Free allocated memory
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }

        // 6. Uninitialize the COM library
        CoUninitialize();

        return filePath;
    }

    void check_embree_error(RTCDevice device, const std::string& where) {
        RTCError error = rtcGetDeviceError(device);
        if (error != RTC_ERROR_NONE) {
            std::cerr << "Embree error at " << where << ": ";
            switch (error) {
                case RTC_ERROR_INVALID_ARGUMENT: std::cerr << "Invalid argument"; break;
                case RTC_ERROR_OUT_OF_MEMORY: std::cerr << "Out of memory"; break;
                case RTC_ERROR_UNSUPPORTED_CPU: std::cerr << "Unsupported CPU"; break;
                case RTC_ERROR_CANCELLED: std::cerr << "Cancelled"; break;
                default: std::cerr << "Unknown error (" << error << ")"; break;
            }
            std::cerr << std::endl;
        }
    }
}

