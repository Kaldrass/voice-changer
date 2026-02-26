// src/app/Preset.h
#pragma once
#include <string>

enum class PresetType
{
    None,
    Girl,
    Demon,
    Robot,
    Radio
};

inline PresetType ParsePreset(const std::string& s)
{
    if (s == "girl")  return PresetType::Girl;
    if (s == "demon") return PresetType::Demon;
    if (s == "robot") return PresetType::Robot;
    if (s == "radio") return PresetType::Radio;
    return PresetType::None;
}