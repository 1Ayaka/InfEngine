#pragma once

#include <function/scene/Component.h>
#include <string>

namespace infengine
{

/**
 * @brief AudioListener component — represents the "ears" in the scene.
 *
 * Only one AudioListener should be active at a time (typically on the
 * main camera).  The listener's Transform position is used for 3D
 * spatialization of all AudioSources.
 *
 * Unity API alignment:
 * - AudioListener is a component attached to a GameObject
 * - Only one active listener per scene
 * - Position/orientation from the Transform drives 3D audio
 *
 * Wwise extensibility:
 * - gameObjectId() → owning GameObject ID for Wwise listener registration
 */
class AudioListener : public Component
{
  public:
    AudioListener() = default;
    ~AudioListener() override = default;

    [[nodiscard]] const char *GetTypeName() const override
    {
        return "AudioListener";
    }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    void Awake() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;

    // ========================================================================
    // Serialization
    // ========================================================================

    [[nodiscard]] std::string Serialize() const override;
    bool Deserialize(const std::string &jsonStr) override;

    // ========================================================================
    // Wwise extensibility hook
    // ========================================================================

    /// @brief Get the owning GameObject ID (for Wwise listener registration)
    [[nodiscard]] uint64_t GetGameObjectId() const;
};

} // namespace infengine
