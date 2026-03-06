#pragma once
#include <core/types/InfFwdType.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <function/renderer/InfRenderer.h>
#include <function/renderer/gui/InfGUIRenderable.h>
#include <function/renderer/gui/InfResourcePreviewer.h>
#include <function/scene/EditorCameraController.h>
#include <function/scene/SceneManager.h>

#include <core/error/InfError.h>
#include <core/log/InfLog.h>
#include <platform/filesystem/InfExtLoad.h>
#include <platform/filesystem/InfPath.h>
#include <platform/input/InputManager.h>

#include <function/resources/AssetDatabase/AssetDatabase.h>
#include <function/resources/InfFileManager.h>

namespace infengine
{
class RenderPipelineCallback; // forward declaration for SRP
class GizmosDrawCallBuffer;   // forward declaration for component gizmos
class InfGUIContext;          // forward declaration for GUI draw-list rendering
class InfScreenUIRenderer;    // forward declaration for GPU screen UI

class InfEngine
{
  public:
    InfEngine(std::string dllPath);
    ~InfEngine();

    // Prevent copying
    InfEngine(const InfEngine &) = delete;
    InfEngine &operator=(const InfEngine &) = delete;
    InfEngine(InfEngine &&) = delete;
    InfEngine &operator=(InfEngine &&) = delete;

    void Run();
    void Exit();
    void Cleanup();

    // renderer
    void InitRenderer(int width, int height, const std::string &projectPath);
    void SetGUIFont(const std::string &fontPath, float fontSize);
    void RegisterGUIRenderable(const std::string &name, std::shared_ptr<InfGUIRenderable> renderable);
    void UnregisterGUIRenderable(const std::string &name);
    void ShowWindow();
    void HideWindow();
    void SetWindowIcon(const std::string &iconPath);

    // Close-request interception — Python checks each frame
    bool IsCloseRequested() const;
    void ConfirmClose();
    void CancelClose();

    // ImGui texture management
    uint64_t UploadTextureForImGui(const std::string &name, const std::vector<unsigned char> &pixels, int width,
                                   int height);
    void RemoveImGuiTexture(const std::string &name);
    bool HasImGuiTexture(const std::string &name) const;
    uint64_t GetImGuiTextureId(const std::string &name) const;

    // Resource preview manager
    ResourcePreviewManager *GetResourcePreviewManager();

    // resources manager
    void ModifyResources(const std::string &filePath);
    void DeleteResources(const std::string &filePath);
    void MoveResources(const std::string &oldFilePath, const std::string &newFilePath);

    /// @brief Get the file manager instance for direct resource operations
    /// @return Pointer to InfFileManager, or nullptr if not initialized
    InfFileManager *GetFileManager() const;

    /// @brief Get the asset database instance
    /// @return Pointer to AssetDatabase, or nullptr if not initialized
    AssetDatabase *GetAssetDatabase() const;

    // ========================================================================
    // Scene Camera Control API - for Scene View with Unity-style controls
    // ========================================================================

    /// @brief Get the editor camera controller (property-based access).
    /// @return Pointer to EditorCameraController, or nullptr if not valid
    EditorCameraController *GetEditorCamera();

    /// @brief Process scene view input (call from Python when scene view is hovered/focused)
    /// @param deltaTime Time since last frame
    /// @param rightMouseDown Is right mouse button held
    /// @param middleMouseDown Is middle mouse button held
    /// @param mouseDeltaX Mouse movement X
    /// @param mouseDeltaY Mouse movement Y
    /// @param scrollDelta Mouse wheel scroll
    /// @param keyW W key held
    /// @param keyA A key held
    /// @param keyS S key held
    /// @param keyD D key held
    /// @param keyQ Q key held
    /// @param keyE E key held
    /// @param keyShift Shift key held
    void ProcessSceneViewInput(float deltaTime, bool rightMouseDown, bool middleMouseDown, float mouseDeltaX,
                               float mouseDeltaY, float scrollDelta, bool keyW, bool keyA, bool keyS, bool keyD,
                               bool keyQ, bool keyE, bool keyShift);

    /// @brief Get editor camera position for display
    void GetEditorCameraPosition(float *outX, float *outY, float *outZ);

    /// @brief Get editor camera rotation (yaw, pitch) for display
    void GetEditorCameraRotation(float *outYaw, float *outPitch);

    /// @brief Get editor camera field of view (vertical FOV in degrees)
    float GetEditorCameraFov();

    /// @brief Set editor camera field of view (vertical FOV in degrees)
    void SetEditorCameraFov(float fov);

    /// @brief Get editor camera near clip distance
    float GetEditorCameraNearClip();

    /// @brief Set editor camera near clip distance
    void SetEditorCameraNearClip(float nearClip);

    /// @brief Get editor camera far clip distance
    float GetEditorCameraFarClip();

    /// @brief Set editor camera far clip distance
    void SetEditorCameraFarClip(float farClip);

    /// @brief Reset editor camera to default position
    void ResetEditorCamera();

    /// @brief Focus editor camera on a point
    void FocusEditorCameraOn(float x, float y, float z, float distance = 10.0f);

    /// @brief Get editor camera focus point
    void GetEditorCameraFocusPoint(float *outX, float *outY, float *outZ);

    /// @brief Get editor camera focus distance
    float GetEditorCameraFocusDistance();

    /// @brief Restore full editor camera state (position, focus point, distance, yaw, pitch)
    void RestoreEditorCameraState(float posX, float posY, float posZ, float focusX, float focusY, float focusZ,
                                  float focusDist, float yaw, float pitch);

    // ========================================================================
    // Scene Render Target API - for offscreen scene rendering
    // ========================================================================

    /// @brief Get the texture ID for the scene render target (for ImGui display)
    /// @return Texture ID (VkDescriptorSet cast to uint64_t), or 0 if not ready
    uint64_t GetSceneTextureId() const;

    /// @brief Resize the scene render target
    /// @param width New width in pixels
    /// @param height New height in pixels
    void ResizeSceneRenderTarget(uint32_t width, uint32_t height);

    // ========================================================================
    // Game Camera Render Target API - for Game View panel
    // ========================================================================

    /// @brief Get the texture ID for the game render target (for ImGui display)
    /// @return Texture ID or 0 if not ready / no game camera
    uint64_t GetGameTextureId() const;

    /// @brief Resize the game render target (lazy-initializes on first call)
    /// @param width New width in pixels
    /// @param height New height in pixels
    void ResizeGameRenderTarget(uint32_t width, uint32_t height);

    /// @brief Enable/disable game camera rendering
    void SetGameCameraEnabled(bool enabled);

    /// @brief Check if game camera rendering is enabled
    [[nodiscard]] bool IsGameCameraEnabled() const;

    /// @brief Get the screen UI renderer for GPU-based 2D screen-space UI
    /// @return Pointer to InfScreenUIRenderer, or nullptr if not initialized
    InfScreenUIRenderer *GetScreenUIRenderer();

    // ========================================================================
    // MSAA Configuration
    // ========================================================================

    /// @brief Set MSAA sample count for both scene and game render targets.
    /// Valid values: 1 (off), 2, 4, 8.
    void SetMsaaSamples(int samples);

    /// @brief Get current MSAA sample count (1 = off).
    int GetMsaaSamples() const;

    // ========================================================================
    // Scene Picking API - for editor selection (screen-space to object)
    // ========================================================================

    /// @brief Pick a scene object by screen-space coordinates in the scene view
    /// @param screenX Mouse X in pixels relative to the scene viewport
    /// @param screenY Mouse Y in pixels relative to the scene viewport
    /// @param viewportWidth Scene viewport width in pixels
    /// @param viewportHeight Scene viewport height in pixels
    /// @return Picked GameObject ID, or 0 if none
    uint64_t PickSceneObjectId(float screenX, float screenY, float viewportWidth, float viewportHeight);

    /// @brief Pick all feasible scene objects under screen-space coordinates, nearest first.
    /// @param screenX Mouse X in pixels relative to the scene viewport
    /// @param screenY Mouse Y in pixels relative to the scene viewport
    /// @param viewportWidth Scene viewport width in pixels
    /// @param viewportHeight Scene viewport height in pixels
    /// @return Ordered candidate GameObject IDs (nearest first), deduplicated.
    std::vector<uint64_t> PickSceneObjectIds(float screenX, float screenY, float viewportWidth, float viewportHeight);

    // ========================================================================
    // Editor Tools API — highlight + ray + mode for Python-side gizmo interaction
    // ========================================================================

    /// @brief Set the highlighted (hovered) gizmo axis. 0=None, 1=X, 2=Y, 3=Z.
    void SetEditorToolHighlight(int axis);

    /// @brief Set the active tool mode. 0=None, 1=Translate, 2=Rotate, 3=Scale.
    void SetEditorToolMode(int mode);

    /// @brief Get the active tool mode. 0=None, 1=Translate, 2=Rotate, 3=Scale.
    int GetEditorToolMode() const;

    /// @brief Set local coordinate mode for editor tools (gizmo aligns to object rotation).
    void SetEditorToolLocalMode(bool local);

    /// @brief Build a world-space ray from screen coordinates (same math as picking).
    /// @return (originX, originY, originZ, dirX, dirY, dirZ)
    std::tuple<float, float, float, float, float, float> ScreenToWorldRay(float screenX, float screenY,
                                                                          float viewportWidth, float viewportHeight);

    // ========================================================================
    // Component Gizmos API — Python-driven per-component gizmo rendering
    // ========================================================================

    /// @brief Get the component gizmos draw call buffer for Python upload
    /// @return Pointer to GizmosDrawCallBuffer, or nullptr if not initialized
    GizmosDrawCallBuffer *GetGizmosDrawCallBuffer();

    // ========================================================================
    // Render Graph API - for Python/ML integration
    // ========================================================================

    /// @brief Get the scene render graph for pass configuration
    /// @return Pointer to SceneRenderGraph, or nullptr if not initialized
    SceneRenderGraph *GetSceneRenderGraph();

    /// @brief Get list of render pass names that have readback enabled
    /// @return Vector of pass names
    std::vector<std::string> GetReadablePassNames() const;

    /// @brief Read color pixels from a render pass output
    /// @param passName Name of the pass to read from
    /// @param outData Output vector for RGBA8 pixel data
    /// @return true if successful
    bool ReadPassColorPixels(const std::string &passName, std::vector<uint8_t> &outData);

    /// @brief Read depth pixels from a render pass output
    /// @param passName Name of the pass to read from
    /// @param outData Output vector for float32 depth data
    /// @return true if successful
    bool ReadPassDepthPixels(const std::string &passName, std::vector<float> &outData);

    /// @brief Get render pass output dimensions
    /// @param passName Name of the pass
    /// @param outWidth Output width
    /// @param outHeight Output height
    /// @return true if pass exists
    bool GetPassOutputSize(const std::string &passName, uint32_t &outWidth, uint32_t &outHeight) const;

    /// @brief Get render pass output texture ID for ImGui display
    /// @param passName Name of the pass
    /// @return Texture ID or 0 if not available
    uint64_t GetPassTextureId(const std::string &passName) const;

    // ========================================================================
    // Editor Gizmos API
    // ========================================================================

    /// @brief Set grid visibility
    void SetShowGrid(bool show);

    /// @brief Get grid visibility
    bool IsShowGrid() const;

    /// @brief Set selection outline for a game object (Unity-style orange wireframe)
    /// @param objectId The ID of the object to show selection for, or 0 to clear
    void SetSelectionOutline(uint64_t objectId);

    /// @brief Get the currently selected object ID (0 if none)
    [[nodiscard]] uint64_t GetSelectedObjectId() const
    {
        return m_selectedObjectId;
    }

    /// @brief Clear selection outline
    void ClearSelectionOutline();

    // ========================================================================
    // Material Pipeline API
    // ========================================================================

    /// @brief Refresh a material's pipeline by reloading shaders
    /// @param material The material to refresh
    /// @return true if successful, false otherwise
    bool RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material);

    /// @brief Reload a shader from file (hot-reload support)
    /// @param shaderPath The path to the shader file (.vert or .frag)
    /// @return true if successful, false otherwise
    bool ReloadShader(const std::string &shaderPath);

    // ========================================================================
    // Render Pipeline API (SRP)
    // ========================================================================

    /// @brief Set a custom render pipeline (Python-driven rendering).
    /// Pass nullptr to revert to the default C++ rendering path.
    void SetRenderPipeline(std::shared_ptr<RenderPipelineCallback> pipeline);

    // debug
    void SetLogLevel(LogLevel engineLevel);

    // State check
    [[nodiscard]] bool IsCleanedUp() const
    {
        return m_isCleanedUp;
    }
    [[nodiscard]] bool IsCleaningUp() const
    {
        return m_isCleaningUp;
    }
    [[nodiscard]] bool IsInitialized() const
    {
        return m_renderer != nullptr && !m_isCleanedUp;
    }

  private:
    /// @brief Check if engine is valid for operations
    [[nodiscard]] bool CheckEngineValid(const char *operation) const;

    /// @brief Ensure a shader is loaded in the renderer
    /// @param shaderId The shader_id to check/load
    /// @param shaderType "vertex" or "fragment"
    /// @return true if shader is loaded, false otherwise
    bool EnsureShaderLoaded(const std::string &shaderId, const std::string &shaderType);

    InfAppMetadata m_metadata{"InfEngine", 0, 1, 0, "com.infrenderer.InfEngine"};

    std::unique_ptr<InfExtLoad> m_extLoader;
    std::unique_ptr<InfFileManager> m_fileManager;
    std::unique_ptr<AssetDatabase> m_assetDatabase;
    std::unique_ptr<InfRenderer> m_renderer;

    LogLevel m_logLevel = LogLevel::LOG_INFO;
    bool m_isCleanedUp = false;
    bool m_isCleaningUp = false;

    // Selection tracking for outline updates
    uint64_t m_selectedObjectId = 0;

    // ImGui ini file path — stored as std::filesystem::path so that
    // wide-char paths (e.g. Chinese usernames) work correctly on Windows.
    std::filesystem::path m_imguiIniPath;

    void LoadImGuiLayout();
    void SaveImGuiLayout();
};
} // namespace infengine