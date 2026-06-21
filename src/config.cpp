// ============================================================================
//  CONFIGURATION SYSTEM — Implementation
//  Simple JSON-based config file parsing and writing.
//  No external dependencies — we roll our own because we can.
// ============================================================================

#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
namespace config {

// Global settings instance
Settings g_settings;

// ============================================================================
//  JSON Helper — minimal parser for our flat config format
// ============================================================================

namespace json_util {

    static std::string Trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        auto end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    }

    // Remove quotes from a string value
    static std::string Unquote(const std::string& s) {
        std::string t = Trim(s);
        if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
            return t.substr(1, t.size() - 2);
        }
        return t;
    }

    static bool ToBool(const std::string& s) {
        std::string t = Trim(s);
        return (t == "true" || t == "1");
    }

    static float ToFloat(const std::string& s) {
        try { return std::stof(Trim(s)); }
        catch (...) { return 0.0f; }
    }

    static int ToInt(const std::string& s) {
        try { return std::stoi(Trim(s)); }
        catch (...) { return 0; }
    }

    // Parse a flat JSON file into key-value pairs
    // Handles nested objects with dot-separated keys
    static std::unordered_map<std::string, std::string> Parse(const std::string& content) {
        std::unordered_map<std::string, std::string> result;
        std::string section;
        size_t pos = 0;

        while (pos < content.size()) {
            // Skip whitespace and structural chars
            while (pos < content.size()) {
                char c = content[pos];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && 
                    c != ',' && c != '{' && c != '}') break;
                
                if (c == '}' && !section.empty()) {
                    section.clear();
                }
                pos++;
            }
            if (pos >= content.size()) break;

            // Parse key
            if (content[pos] == '"') {
                pos++;
                size_t end = content.find('"', pos);
                if (end == std::string::npos) break;
                std::string key = content.substr(pos, end - pos);
                pos = end + 1;

                // Skip to colon
                while (pos < content.size() && content[pos] != ':') pos++;
                pos++;

                // Skip whitespace
                while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
                if (pos >= content.size()) break;

                if (content[pos] == '{') {
                    section = key;
                    pos++;
                } else if (content[pos] == '"') {
                    // String value
                    pos++;
                    size_t valEnd = content.find('"', pos);
                    if (valEnd == std::string::npos) break;
                    std::string fullKey = section.empty() ? key : section + "." + key;
                    result[fullKey] = content.substr(pos, valEnd - pos);
                    pos = valEnd + 1;
                } else {
                    // Number or boolean value
                    size_t valStart = pos;
                    while (pos < content.size() && content[pos] != ',' && 
                           content[pos] != '}' && content[pos] != '\n') pos++;
                    std::string fullKey = section.empty() ? key : section + "." + key;
                    result[fullKey] = Trim(content.substr(valStart, pos - valStart));
                }
            } else {
                pos++;
            }
        }

        return result;
    }

} // namespace json_util

// ============================================================================
//  Load
// ============================================================================

bool Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto kv = json_util::Parse(content);
    if (kv.empty()) return false;

    // Map values to settings
    auto get = [&](const std::string& key) -> std::string* {
        auto it = kv.find(key);
        if (it != kv.end()) return &it->second;
        return nullptr;
    };

    // Features
    if (auto* v = get("features.espEnabled"))    g_settings.espEnabled = json_util::ToBool(*v);
    if (auto* v = get("features.showBoxes"))     g_settings.showBoxes = json_util::ToBool(*v);
    if (auto* v = get("features.showSnaplines")) g_settings.showSnaplines = json_util::ToBool(*v);
    if (auto* v = get("features.showHealth"))    g_settings.showHealth = json_util::ToBool(*v);
    if (auto* v = get("features.showShield"))    g_settings.showShield = json_util::ToBool(*v);
    if (auto* v = get("features.showDistance"))  g_settings.showDistance = json_util::ToBool(*v);
    if (auto* v = get("features.showNames"))     g_settings.showNames = json_util::ToBool(*v);
    if (auto* v = get("features.showHeadDot"))   g_settings.showHeadDot = json_util::ToBool(*v);
    if (auto* v = get("features.showSkeleton"))  g_settings.showSkeleton = json_util::ToBool(*v);

    // Visuals
    if (auto* v = get("visuals.boxThickness"))      g_settings.boxThickness = json_util::ToFloat(*v);
    if (auto* v = get("visuals.snaplineThickness")) g_settings.snaplineThickness = json_util::ToFloat(*v);
    if (auto* v = get("visuals.maxDistance"))        g_settings.maxDistance = json_util::ToFloat(*v);

    // Keybinds
    if (auto* v = get("keybinds.toggleKey"))    g_settings.toggleKey = json_util::ToInt(*v);
    if (auto* v = get("keybinds.espToggleKey")) g_settings.espToggleKey = json_util::ToInt(*v);
    if (auto* v = get("keybinds.exitKey"))      g_settings.exitKey = json_util::ToInt(*v);

    // Target
    if (auto* v = get("target.window"))  g_settings.targetWindow = *v;
    if (auto* v = get("target.process")) g_settings.targetProcess = *v;

    return true;
}

// ============================================================================
//  Save
// ============================================================================

bool Save(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";

    // Features
    file << "    \"features\": {\n";
    file << "        \"espEnabled\": " << (g_settings.espEnabled ? "true" : "false") << ",\n";
    file << "        \"showBoxes\": " << (g_settings.showBoxes ? "true" : "false") << ",\n";
    file << "        \"showSnaplines\": " << (g_settings.showSnaplines ? "true" : "false") << ",\n";
    file << "        \"showHealth\": " << (g_settings.showHealth ? "true" : "false") << ",\n";
    file << "        \"showShield\": " << (g_settings.showShield ? "true" : "false") << ",\n";
    file << "        \"showDistance\": " << (g_settings.showDistance ? "true" : "false") << ",\n";
    file << "        \"showNames\": " << (g_settings.showNames ? "true" : "false") << ",\n";
    file << "        \"showHeadDot\": " << (g_settings.showHeadDot ? "true" : "false") << ",\n";
    file << "        \"showSkeleton\": " << (g_settings.showSkeleton ? "true" : "false") << "\n";
    file << "    },\n";

    // Visuals
    file << "    \"visuals\": {\n";
    file << "        \"boxThickness\": " << g_settings.boxThickness << ",\n";
    file << "        \"snaplineThickness\": " << g_settings.snaplineThickness << ",\n";
    file << "        \"maxDistance\": " << g_settings.maxDistance << "\n";
    file << "    },\n";

    // Keybinds
    file << "    \"keybinds\": {\n";
    file << "        \"toggleKey\": " << g_settings.toggleKey << ",\n";
    file << "        \"espToggleKey\": " << g_settings.espToggleKey << ",\n";
    file << "        \"exitKey\": " << g_settings.exitKey << "\n";
    file << "    },\n";

    // Target
    file << "    \"target\": {\n";
    file << "        \"window\": \"" << g_settings.targetWindow << "\",\n";
    file << "        \"process\": \"" << g_settings.targetProcess << "\"\n";
    file << "    }\n";

    file << "}\n";

    return true;
}

} // namespace config
