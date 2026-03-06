#pragma once

// Minimal includes required for public API value types and POD members
#include "InfRenderStruct.h"
#include <chrono>
#include <core/log/InfLog.h>           // LogLevel enum (used in SetLogLevel)
#include <core/types/InfApplication.h> // InfAppMetadata value type (public methods)
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace infengine
{
// ============================================================================
// Forward declarations for private subsystem types.
// Full definitions are in InfRenderer.cpp. Keeping them out of this header
// eliminates the transitive include fan-out that made every InfRenderer.h
// consumer recompile when any subsystem changed.
// ============================================================================
class EditorGizmos;
class EditorTools;
class GizmosDrawCallBuffer;
class InfGUI;
class InfGUIRenderable;
class InfMaterial;
class InfVkCoreModular;
class InfView;
class OutlineRenderer;
class RenderPipelineCallback;
class ResourcePreviewManager;
class SceneRenderGraph;
class SceneRenderTarget;
class TransientResourcePool;
class InfGUIContext;
class InfScreenUIRenderer;

class InfRenderer
{
  public:
    InfRenderer(size_t graphicsThreads = 1, size_t presentThreads = 1, size_t transferThreads = 1,
                size_t computeThreads = 1);
    ~InfRenderer();

    InfRenderer(const InfRenderer &) = delete;
    InfRenderer &operator=(const InfRenderer &) = delete;
    InfRenderer &operator=(InfRenderer &&) = delete;
    InfRenderer(InfRenderer &&) = delete;

    void SetCameraPos(float x, float y, float z);
    void SetCameraLookAt(float x, float y, float z);
    void SetCameraUp(float x, float y, float z);

    float *GetCameraPos();
    float *GetCameraLookAt();
    float *GetCameraUp();

    void TranslateCamera(float x, float y, float z);

    void SetAppMetadata(InfAppMetadata appMetaData);
    InfAppMetadata GetAppMetadata();
    InfAppMetadata GetRendererMetadata();

    void Init(int width, int height, InfAppMetadata appMetaData);
    void PreparePipeline();
    void DrawFrame();

    void SetThreads(size_t graphicsThreads, size_t presentThreads, size_t transferThreads, size_t computeThreads);
    void LoadShader(const char *name, const std::vector<char> &code, const char *type);
    bool HasShader(const std::string &name, const std::string &type) const;
    bool GetUserEvent();
    void ShowWindow();
    void HideWindow();
    void SetWindowIcon(const std::string &iconPath);

    // Close-request interception (delegates to InfView)
    bool IsCloseRequested() const;
    void ConfirmClose();
    void CancelClose();

    void SetGUIFont(const char *fontPath, float fontSize);
    void RegisterGUIRenderable(const char *name, std::shared_ptr<InfGUIRenderable> renderable);
    void UnregisterGUIRenderable(const char *name);

    // ImGui texture management
    uint64_t UploadTextureForImGui(const std::string &name, const unsigned char *pixels, int width, int height);
    void RemoveImGuiTexture(const std::string &name);
    bool HasImGuiTexture(const std::string &name) const;
    uint64_t GetImGuiTextureId(const std::string &name) const;

    // Resource preview manager
    ResourcePreviewManager *GetResourcePreviewManager();

    void SetLogLevel(LogLevel level);

    // Scene system integration
    void InitializeDefaultScene();
    void UpdateSceneLighting();

    // Get the material from the first MeshRenderer in the scene
    std::shared_ptr<InfMaterial> GetFirstMeshRendererMaterial();

    // Scene render target for offscreen rendering
    uint64_t GetSceneTextureId() const;
    void ResizeSceneRenderTarget(uint32_t width, uint32_t height);

    // Editor gizmos
    void SetShowGrid(bool show);
    bool IsShowGrid() const;
    EditorGizmos &GetEditorGizmos();

    /// @brief Access editor tools (translate/rotate/scale gizmo)
    EditorTools *GetEditorTools();

    /// @brief Access the component gizmos draw call buffer (Python-driven)
    GizmosDrawCallBuffer *GetGizmosDrawCallBuffer();

    /// @brief Set the selected object ID for outline tracking
    void SetSelectedObjectId(uint64_t objectId)
    {
        m_selectedObjectId = objectId;
    }
    [[nodiscard]] uint64_t GetSelectedObjectId() const
    {
        return m_selectedObjectId;
    }

    // Material pipeline refresh - call this after modifying material shader paths
    bool RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material);

    // Refresh all materials using a specific shader
    bool RefreshMaterialsUsingShader(const std::string &shaderId);

    // Invalidate shader cache for hot-reload (must call before loading new shader code)
    void InvalidateShaderCache(const std::string &shaderId);

    // ========================================================================
    // Render Graph Access (for Python/ML integration)
    // ========================================================================

    /// @brief Get the scene render graph for pass configuration and output access
    /// @return Pointer to SceneRenderGraph, or nullptr if not initialized
    SceneRenderGraph *GetSceneRenderGraph();

    /// @brief Set a custom render pipeline (Python-driven rendering via SRC).
    /// Pass nullptr to revert to the default C++ rendering path.
    void SetRenderPipeline(std::shared_ptr<RenderPipelineCallback> pipeline);

    // ========================================================================
    // Game Camera Render Target (for Game View panel)
    // ========================================================================

    /// @brief Get Game View texture ID for ImGui display
    uint64_t GetGameTextureId() const;

    /// @brief Resize the game render target to match Game View panel size
    void ResizeGameRenderTarget(uint32_t width, uint32_t height);

    /// @brief Enable/disable game camera rendering
    void SetGameCameraEnabled(bool enabled);

    /// @brief Check if game camera rendering is enabled
    [[nodiscard]] bool IsGameCameraEnabled() const
    {
        return m_gameCameraEnabled;
    }

    /// @brief Get the screen UI renderer for GPU-based 2D screen-space UI
    /// @return Pointer to InfScreenUIRenderer, or nullptr if not initialized
    InfScreenUIRenderer *GetScreenUIRenderer();

    // ========================================================================
    // MSAA Configuration
    // ========================================================================

    /// @brief Set MSAA sample count for both scene and game render targets.
    /// Valid values: 1 (off), 2, 4, 8.  Triggers Vulkan resource recreation.
    void SetMsaaSamples(int samples);

    /// @brief Get current MSAA sample count (1 = off).
    int GetMsaaSamples() const;

  private:
    InfAppMetadata m_appMetadata;
    InfAppMetadata m_rendererMetadata;

    float m_cameraPos[3] = {2.0f, 2.0f, 2.0f};
    float m_cameraLookAt[3] = {0.0f, 0.0f, 0.0f};
    float m_cameraUp[3] = {0.0f, 1.0f, 0.0f};

    // Delta time tracking
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_deltaTime = 0.016f;

    std::unique_ptr<InfVkCoreModular> m_vkCore;
    std::unique_ptr<InfGUI> m_gui;
    std::unique_ptr<InfView> m_view;
    std::unique_ptr<SceneRenderTarget> m_sceneRenderTarget;
    std::unique_ptr<SceneRenderGraph> m_sceneRenderGraph;
    std::unique_ptr<EditorGizmos> m_editorGizmos;
    std::unique_ptr<EditorTools> m_editorTools;
    std::unique_ptr<GizmosDrawCallBuffer> m_componentGizmos;
    std::unique_ptr<OutlineRenderer> m_outlineRenderer;
    std::unique_ptr<TransientResourcePool> m_transientResourcePool;

    // Game Camera: separate render target + graph for Game View
    std::unique_ptr<SceneRenderTarget> m_gameRenderTarget;
    std::unique_ptr<SceneRenderGraph> m_gameRenderGraph;
    std::unique_ptr<InfScreenUIRenderer> m_screenUIRenderer;
    bool m_gameCameraEnabled = false;

    // SRP: Python-driven render pipeline (nullptr = default C++ path)
    std::shared_ptr<RenderPipelineCallback> m_renderPipeline;

    // Selection tracking for auto-update of outline transforms
    uint64_t m_selectedObjectId = 0;

    /// @brief Find effective game camera via Scene::FindGameCamera().
    /// Returns the highest-priority active Camera (by depth), excluding the editor camera.
    /// Caches result in scene main camera for fast-path lookup.
    class Camera *FindGameCamera();
};
} // namespace infengine