#include "core/PresetManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace
{
std::string PresetFilePath(const std::string& dir, const std::string& name)
{
    std::string safe = name;
    for (char& c : safe)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
    }
    if (safe.empty()) safe = "preset";
    return dir + "/" + safe + ".preset";
}

std::vector<float> ParseWeights(const std::string& csv)
{
    std::vector<float> out;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (token.empty()) continue;
        try
        {
            out.push_back(std::stof(token));
        }
        catch (...)
        {
        }
    }
    return out;
}

std::string SerializeWeights(const std::vector<float>& w)
{
    std::ostringstream oss;
    for (size_t i = 0; i < w.size(); ++i)
    {
        if (i > 0) oss << ',';
        oss << w[i];
    }
    return oss.str();
}
}

std::string PresetManager::GetPresetsDirectory()
{
    std::string home;
    
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile && *userProfile)
    {
        home = userProfile;
    }
    else
    {
        const char* hd = std::getenv("HOMEDRIVE");
        const char* hp = std::getenv("HOMEPATH");
        if (hd && hp) home = std::string(hd) + std::string(hp);
    }
    if (home.empty()) home = ".";
#else
    const char* homeEnv = std::getenv("HOME");
    home = homeEnv ? homeEnv : "/tmp";
#endif

    std::string presetDir = home + "/Documents/voice-changer/presets";
    return presetDir;
}

PresetManager::PresetManager()
{
    LoadPresetsFromDisk();
}

void PresetManager::LoadPresetsFromDisk()
{
    m_presets.clear();

    std::string presetsDir = GetPresetsDirectory();
    if (!fs::exists(presetsDir))
    {
        return;  // No presets directory yet
    }

    for (const auto& entry : fs::directory_iterator(presetsDir))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".preset") continue;

        std::ifstream in(entry.path());
        if (!in) continue;

        VoicePreset preset;
        preset.name = entry.path().stem().string();

        std::string line;
        while (std::getline(in, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);

            if (key == "name") preset.name = value;
            else if (key == "description") preset.description = value;
            else if (key == "profile") preset.profile = value;
            else if (key == "blend")
            {
                try { preset.blend = std::stof(value); } catch (...) {}
            }
            else if (key == "intensity")
            {
                try { preset.intensity = std::stof(value); } catch (...) {}
            }
            else if (key == "weights")
            {
                preset.weights = ParseWeights(value);
            }
        }

        if (!preset.name.empty())
        {
            m_presets[preset.name] = preset;
        }
    }
}

std::vector<std::string> PresetManager::ListPresets() const
{
    std::vector<std::string> names;
    for (const auto& [name, _] : m_presets)
    {
        names.push_back(name);
    }
    return names;
}

bool PresetManager::LoadPreset(const std::string& name, VoicePreset& outPreset)
{
    auto it = m_presets.find(name);
    if (it == m_presets.end())
    {
        return false;
    }
    outPreset = it->second;
    return true;
}

bool PresetManager::SavePreset(const VoicePreset& preset)
{
    if (preset.name.empty()) return false;

    std::string presetsDir = GetPresetsDirectory();

    // Create directory if needed
    try
    {
        fs::create_directories(presetsDir);
    }
    catch (const std::exception&)
    {
        return false;
    }

    const std::string filePath = PresetFilePath(presetsDir, preset.name);
    std::ofstream out(filePath, std::ios::trunc);
    if (!out) return false;

    out << "name=" << preset.name << "\n";
    out << "description=" << preset.description << "\n";
    out << "profile=" << preset.profile << "\n";
    out << "blend=" << preset.blend << "\n";
    out << "intensity=" << preset.intensity << "\n";
    out << "weights=" << SerializeWeights(preset.weights) << "\n";

    if (!out.good()) return false;

    m_presets[preset.name] = preset;

    return true;
}

bool PresetManager::DeletePreset(const std::string& name)
{
    auto it = m_presets.find(name);
    if (it == m_presets.end())
    {
        return false;
    }

    const std::string path = PresetFilePath(GetPresetsDirectory(), name);
    std::error_code ec;
    fs::remove(path, ec);

    m_presets.erase(it);
    return true;
}
