#include "Config.h"
#include <iostream>
#include <algorithm>

std::string Config::sanitize_path(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    // Check for obvious path traversal attempts
    if (path.find("..") != std::string::npos) {
        std::cerr << "Error: Path traversal detected in path: " << path << std::endl;
        return "";
    }

    // Convert to normalized path
    std::filesystem::path input_path(path);
    
    try {
        // Resolve any symbolic links and normalize the path
        std::filesystem::path canonical_path = std::filesystem::canonical(input_path);
        
        // Additional safety check: ensure the resolved path is within reasonable bounds
        std::string path_str = canonical_path.string();
        
        // Check for suspicious patterns
        if (path_str.find("/etc/") != std::string::npos ||
            path_str.find("/sys/") != std::string::npos ||
            path_str.find("/proc/") != std::string::npos) {
            std::cerr << "Error: Access to system directory denied: " << path_str << std::endl;
            return "";
        }
        
        return path_str;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: Invalid file path '" << path << "': " << e.what() << std::endl;
        return "";
    } catch (const std::exception& e) {
        std::cerr << "Error: Path validation failed for '" << path << "': " << e.what() << std::endl;
        return "";
    }
}