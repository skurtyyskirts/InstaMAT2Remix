#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace InstaMAT2Remix {
    namespace fs = std::filesystem;

    class Utils {
    public:
        static std::string NormalizePath(const std::string& path) {
            std::string p = path;
            std::replace(p.begin(), p.end(), '\\', '/');
            return p;
        }

        static bool EnsureDirectory(const std::string& path) {
            try {
                if (!fs::exists(path)) {
                    return fs::create_directories(path);
                }
                return true;
            } catch (...) {
                return false;
            }
        }
        
        static std::string GetTempDirectory() {
            return fs::temp_directory_path().string();
        }
    };
}

