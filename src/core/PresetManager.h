#pragma once

#include <string>
#include <vector>
#include <map>

struct VoicePreset
{
    std::string name;
    std::string description;
    std::vector<float> weights;
    std::string profile;      // neutral, bright, dark, robot
    float blend = 0.65f;
    float intensity = 0.4f;
};

class PresetManager
{
public:
    PresetManager();

    // List all available presets
    std::vector<std::string> ListPresets() const;

    // Load preset by name
    bool LoadPreset(const std::string& name, VoicePreset& outPreset);

    // Save preset to disk
    bool SavePreset(const VoicePreset& preset);

    // Delete preset by name
    bool DeletePreset(const std::string& name);

    // Get preset path (config directory)
    static std::string GetPresetsDirectory();

private:
    std::map<std::string, VoicePreset> m_presets;

    void LoadPresetsFromDisk();
};
