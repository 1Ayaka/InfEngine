/**
 * @file TagLayerManager.cpp
 * @brief Implementation of the Tag and Layer management system.
 */

#include "TagLayerManager.h"
#include <algorithm>
#include <core/log/InfLog.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace infengine
{

const std::string TagLayerManager::s_emptyString;

TagLayerManager &TagLayerManager::Instance()
{
    static TagLayerManager instance;
    return instance;
}

TagLayerManager::TagLayerManager()
{
    InitDefaults();
}

void TagLayerManager::InitDefaults()
{
    // Built-in tags (indices 0-6)
    m_tags.clear();
    m_tags.push_back("Untagged");       // 0
    m_tags.push_back("Respawn");        // 1
    m_tags.push_back("Finish");         // 2
    m_tags.push_back("EditorOnly");     // 3
    m_tags.push_back("MainCamera");     // 4
    m_tags.push_back("Player");         // 5
    m_tags.push_back("GameController"); // 6

    // 32 layers, built-in names for certain indices
    m_layers.resize(kMaxLayers);
    m_layers[0] = "Default";
    m_layers[1] = "TransparentFX";
    m_layers[2] = "IgnoreRaycast";
    m_layers[3] = "";
    m_layers[4] = "Water";
    m_layers[5] = "UI";
    for (int i = 6; i < kMaxLayers; ++i) {
        m_layers[i] = "";
    }

    // Default Unity-like behaviour: all layers collide with all layers.
    m_layerCollisionMasks.resize(kMaxLayers);
    for (int i = 0; i < kMaxLayers; ++i) {
        m_layerCollisionMasks[i] = 0xFFFFFFFFu;
    }
}

// ============================================================================
// Tags
// ============================================================================

const std::string &TagLayerManager::GetTag(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_tags.size())) {
        return m_tags[index];
    }
    return s_emptyString;
}

int TagLayerManager::GetTagIndex(const std::string &tag) const
{
    for (int i = 0; i < static_cast<int>(m_tags.size()); ++i) {
        if (m_tags[i] == tag)
            return i;
    }
    return -1;
}

int TagLayerManager::AddTag(const std::string &tag)
{
    if (tag.empty()) {
        INFLOG_WARN("TagLayerManager::AddTag: cannot add empty tag");
        return -1;
    }
    // Check for duplicates
    int existing = GetTagIndex(tag);
    if (existing >= 0) {
        return existing;
    }
    m_tags.push_back(tag);
    INFLOG_DEBUG("TagLayerManager::AddTag: added tag '", tag, "' at index ", m_tags.size() - 1);
    return static_cast<int>(m_tags.size()) - 1;
}

bool TagLayerManager::RemoveTag(const std::string &tag)
{
    if (IsBuiltinTag(tag)) {
        INFLOG_WARN("TagLayerManager::RemoveTag: cannot remove built-in tag '", tag, "'");
        return false;
    }
    int idx = GetTagIndex(tag);
    if (idx < 0) {
        return false;
    }
    m_tags.erase(m_tags.begin() + idx);
    INFLOG_DEBUG("TagLayerManager::RemoveTag: removed tag '", tag, "'");
    return true;
}

const std::vector<std::string> &TagLayerManager::GetAllTags() const
{
    return m_tags;
}

bool TagLayerManager::IsBuiltinTag(const std::string &tag) const
{
    int idx = GetTagIndex(tag);
    return idx >= 0 && idx < kBuiltinTagCount;
}

// ============================================================================
// Layers
// ============================================================================

const std::string &TagLayerManager::GetLayerName(int layer) const
{
    if (layer >= 0 && layer < kMaxLayers) {
        return m_layers[layer];
    }
    return s_emptyString;
}

int TagLayerManager::GetLayerByName(const std::string &name) const
{
    if (name.empty())
        return -1;
    for (int i = 0; i < kMaxLayers; ++i) {
        if (m_layers[i] == name)
            return i;
    }
    return -1;
}

bool TagLayerManager::SetLayerName(int layer, const std::string &name)
{
    if (layer < 0 || layer >= kMaxLayers) {
        INFLOG_WARN("TagLayerManager::SetLayerName: invalid layer index ", layer);
        return false;
    }
    if (IsBuiltinLayer(layer)) {
        INFLOG_WARN("TagLayerManager::SetLayerName: cannot rename built-in layer ", layer, " ('", m_layers[layer],
                    "')");
        return false;
    }
    m_layers[layer] = name;
    INFLOG_DEBUG("TagLayerManager::SetLayerName: layer ", layer, " = '", name, "'");
    return true;
}

const std::vector<std::string> &TagLayerManager::GetAllLayers() const
{
    return m_layers;
}

bool TagLayerManager::IsBuiltinLayer(int layer) const
{
    // Built-in layers: 0 (Default), 1 (TransparentFX), 2 (IgnoreRaycast), 4 (Water), 5 (UI)
    return layer == 0 || layer == 1 || layer == 2 || layer == 4 || layer == 5;
}

uint32_t TagLayerManager::GetLayerCollisionMask(int layer) const
{
    if (layer < 0 || layer >= kMaxLayers) {
        return 0;
    }
    return m_layerCollisionMasks[layer];
}

bool TagLayerManager::SetLayerCollisionMask(int layer, uint32_t mask)
{
    if (layer < 0 || layer >= kMaxLayers) {
        INFLOG_WARN("TagLayerManager::SetLayerCollisionMask: invalid layer index ", layer);
        return false;
    }

    m_layerCollisionMasks[layer] = mask;

    // Keep the matrix symmetric.
    for (int other = 0; other < kMaxLayers; ++other) {
        bool enabled = (mask & LayerToMask(other)) != 0;
        if (enabled)
            m_layerCollisionMasks[other] |= LayerToMask(layer);
        else
            m_layerCollisionMasks[other] &= ~LayerToMask(layer);
    }
    return true;
}

bool TagLayerManager::GetLayersCollide(int layerA, int layerB) const
{
    if (layerA < 0 || layerA >= kMaxLayers || layerB < 0 || layerB >= kMaxLayers) {
        return false;
    }
    return (m_layerCollisionMasks[layerA] & LayerToMask(layerB)) != 0;
}

bool TagLayerManager::SetLayersCollide(int layerA, int layerB, bool shouldCollide)
{
    if (layerA < 0 || layerA >= kMaxLayers || layerB < 0 || layerB >= kMaxLayers) {
        INFLOG_WARN("TagLayerManager::SetLayersCollide: invalid layer pair ", layerA, ", ", layerB);
        return false;
    }

    const uint32_t maskA = LayerToMask(layerA);
    const uint32_t maskB = LayerToMask(layerB);
    if (shouldCollide) {
        m_layerCollisionMasks[layerA] |= maskB;
        m_layerCollisionMasks[layerB] |= maskA;
    } else {
        m_layerCollisionMasks[layerA] &= ~maskB;
        m_layerCollisionMasks[layerB] &= ~maskA;
    }
    return true;
}

// ============================================================================
// Layer mask helpers
// ============================================================================

uint32_t TagLayerManager::LayerToMask(int layer)
{
    if (layer < 0 || layer >= kMaxLayers)
        return 0;
    return 1u << static_cast<uint32_t>(layer);
}

uint32_t TagLayerManager::GetMask(const std::vector<std::string> &layerNames) const
{
    uint32_t mask = 0;
    for (const auto &name : layerNames) {
        int idx = GetLayerByName(name);
        if (idx >= 0) {
            mask |= LayerToMask(idx);
        }
    }
    return mask;
}

// ============================================================================
// Serialization
// ============================================================================

std::string TagLayerManager::Serialize() const
{
    json j;
    j["schema_version"] = 1;

    // Only serialize custom tags (indices >= kBuiltinTagCount)
    json customTags = json::array();
    for (int i = kBuiltinTagCount; i < static_cast<int>(m_tags.size()); ++i) {
        customTags.push_back(m_tags[i]);
    }
    j["custom_tags"] = customTags;

    // Serialize all 32 layers (built-in + custom names)
    json layers = json::array();
    for (int i = 0; i < kMaxLayers; ++i) {
        layers.push_back(m_layers[i]);
    }
    j["layers"] = layers;

    json collisionMasks = json::array();
    for (int i = 0; i < kMaxLayers; ++i) {
        collisionMasks.push_back(m_layerCollisionMasks[i]);
    }
    j["layer_collision_masks"] = collisionMasks;

    return j.dump(2);
}

bool TagLayerManager::Deserialize(const std::string &jsonStr)
{
    try {
        json j = json::parse(jsonStr);

        // Re-initialize defaults first
        InitDefaults();

        // Restore custom tags
        if (j.contains("custom_tags") && j["custom_tags"].is_array()) {
            for (const auto &tag : j["custom_tags"]) {
                std::string tagStr = tag.get<std::string>();
                if (!tagStr.empty() && GetTagIndex(tagStr) < 0) {
                    m_tags.push_back(tagStr);
                }
            }
        }

        // Restore layer names (only user-customizable layers)
        if (j.contains("layers") && j["layers"].is_array()) {
            const auto &layersArr = j["layers"];
            for (int i = 0; i < kMaxLayers && i < static_cast<int>(layersArr.size()); ++i) {
                std::string name = layersArr[i].get<std::string>();
                if (!IsBuiltinLayer(i)) {
                    m_layers[i] = name;
                }
                // Built-in layers keep their default names
            }
        }

        if (j.contains("layer_collision_masks") && j["layer_collision_masks"].is_array()) {
            const auto &maskArr = j["layer_collision_masks"];
            for (int i = 0; i < kMaxLayers && i < static_cast<int>(maskArr.size()); ++i) {
                m_layerCollisionMasks[i] = maskArr[i].get<uint32_t>();
            }

            // Re-symmetrize defensively in case the file was edited manually.
            for (int a = 0; a < kMaxLayers; ++a) {
                for (int b = a + 1; b < kMaxLayers; ++b) {
                    bool collides = ((m_layerCollisionMasks[a] & LayerToMask(b)) != 0) ||
                                    ((m_layerCollisionMasks[b] & LayerToMask(a)) != 0);
                    SetLayersCollide(a, b, collides);
                }
            }
        }

        INFLOG_DEBUG("TagLayerManager: deserialized ", m_tags.size() - kBuiltinTagCount, " custom tags");
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("TagLayerManager::Deserialize failed: ", e.what());
        return false;
    }
}

bool TagLayerManager::SaveToFile(const std::string &path) const
{
    try {
        std::string jsonStr = Serialize();
        std::ofstream file(path);
        if (!file.is_open()) {
            INFLOG_ERROR("TagLayerManager::SaveToFile: cannot open '", path, "'");
            return false;
        }
        file << jsonStr;
        file.close();
        INFLOG_INFO("TagLayerManager: saved to '", path, "'");
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("TagLayerManager::SaveToFile failed: ", e.what());
        return false;
    }
}

bool TagLayerManager::LoadFromFile(const std::string &path)
{
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            INFLOG_DEBUG("TagLayerManager::LoadFromFile: file not found '", path, "', using defaults");
            return false;
        }
        std::string jsonStr((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        return Deserialize(jsonStr);
    } catch (const std::exception &e) {
        INFLOG_ERROR("TagLayerManager::LoadFromFile failed: ", e.what());
        return false;
    }
}

} // namespace infengine
