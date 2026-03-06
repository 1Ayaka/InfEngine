/**
 * @file InfEngine.cpp
 * @brief InfEngine — Core lifecycle, resources, renderer init, gizmos, material pipeline
 *
 * Editor camera control → InfEngineCamera.cpp
 * Scene picking / raycasting → ScenePicker.cpp
 */

#include "InfEngine.h"
// Explicit includes for types now only forward-declared in InfRenderer.h
#include <cmath>
#include <filesystem>
#include <fstream>
#include <function/audio/AudioEngine.h>
#include <function/renderer/EditorGizmos.h>
#include <function/renderer/GizmosDrawCallBuffer.h>
#include <function/renderer/SceneRenderGraph.h>
#include <function/renderer/ScriptableRenderContext.h>
#include <function/renderer/gui/InfGUIContext.h>
#include <function/renderer/gui/InfScreenUIRenderer.h>
#include <function/scene/MeshRenderer.h>
#include <function/scene/SceneRenderer.h>
#include <function/scene/physics/PhysicsWorld.h>
#include <imgui.h>
#include <imgui_internal.h>

#ifdef _WIN32
#include <ShlObj.h> // SHGetFolderPathW for Documents path
#endif

namespace infengine
{

// ----------------------------------
// Helper method for validation
// ----------------------------------

bool InfEngine::CheckEngineValid(const char *operation) const
{
    if (m_isCleanedUp) {
        INFLOG_ERROR("Cannot ", operation, ": Engine has been cleaned up.");
        return false;
    }
    if (m_isCleaningUp) {
        INFLOG_ERROR("Cannot ", operation, ": Engine is cleaning up.");
        return false;
    }
    return true;
}

// ----------------------------------
// Resources handling
// ----------------------------------

void InfEngine::ModifyResources(const std::string &filePath)
{
    if (!CheckEngineValid("modify resources") || !m_fileManager) {
        return;
    }
    m_fileManager->ModifyResource(filePath);
}

void InfEngine::DeleteResources(const std::string &filePath)
{
    if (!CheckEngineValid("delete resources") || !m_fileManager) {
        return;
    }
    m_fileManager->DeleteResource(filePath);
}

void InfEngine::MoveResources(const std::string &oldFilePath, const std::string &newFilePath)
{
    if (!CheckEngineValid("move resources") || !m_fileManager) {
        return;
    }
    m_fileManager->MoveResource(oldFilePath, newFilePath);
}

InfFileManager *InfEngine::GetFileManager() const
{
    return m_fileManager.get();
}

AssetDatabase *InfEngine::GetAssetDatabase() const
{
    return m_assetDatabase.get();
}

// ----------------------------------
// Lifecycle
// ----------------------------------

InfEngine::InfEngine(std::string dllPath) : m_isCleanedUp(false)
{
    INFLOG_DEBUG("Create InfEngine.");
    m_fileManager = std::make_unique<InfFileManager>();
    m_assetDatabase = std::make_unique<AssetDatabase>(m_fileManager.get());

    INFLOG_DEBUG("Create InfEngine Renderer.");
    m_renderer = std::make_unique<InfRenderer>(1, 1, 1, 1);
}

InfEngine::~InfEngine()
{
    INFLOG_DEBUG("InfEngine destructor called.");
    Cleanup();
}

void InfEngine::Run()
{
    if (!CheckEngineValid("run") || !m_renderer) {
        INFLOG_ERROR("Cannot run: Renderer is not initialized.");
        return;
    }

    INFLOG_DEBUG("Run InfEngine.");
    while (m_renderer && m_renderer->GetUserEvent()) {
        m_renderer->DrawFrame();

        // Periodically save layout when ImGui marks it dirty
        ImGuiIO &io = ImGui::GetIO();
        if (io.WantSaveIniSettings) {
            SaveImGuiLayout();
            io.WantSaveIniSettings = false;
        }
    }
    INFLOG_DEBUG("Main loop ended.");
    SaveImGuiLayout();
    // NOTE: Cleanup is no longer called here — Python controls the
    // shutdown order so it can stop background threads first.
    // ~InfEngine() still calls Cleanup() as a safety net.
}

void InfEngine::Exit()
{
    INFLOG_DEBUG("Exit requested.");
    // Set exit flag to make the main loop exit
    // The actual exit happens when GetUserEvent() returns false
}

void InfEngine::Cleanup()
{
    if (m_isCleanedUp) {
        INFLOG_DEBUG("Already cleaned up, skipping.");
        return;
    }

    m_isCleaningUp = true;

    SaveImGuiLayout();
    AudioEngine::Instance().Shutdown();
    PhysicsWorld::Instance().Shutdown();

    m_renderer.reset();
    m_assetDatabase.reset();
    m_fileManager.reset();
    m_extLoader.reset();

    m_isCleanedUp = true;
    m_isCleaningUp = false;
}

// ----------------------------------
// Renderer initialization
// ----------------------------------

void InfEngine::InitRenderer(int width, int height, const std::string &projectPath)
{
    if (!CheckEngineValid("initialize renderer") || !m_renderer) {
        INFLOG_ERROR("Cannot initialize renderer: Renderer is not available.");
        return;
    }

    m_renderer->Init(width, height, m_metadata);

    INFLOG_DEBUG("Load shaders.");
    const char *defaultShaderPath = JoinPath({projectPath, "Basics", "shaders"});
    const char *assetsPath = JoinPath({projectPath, "Assets"});
    if (m_fileManager) {
        m_fileManager->LoadDefaultShaders(defaultShaderPath, m_renderer.get());
        if (m_assetDatabase) {
            m_assetDatabase->Initialize(projectPath);
            m_assetDatabase->Refresh();
        } else {
            m_fileManager->LoadAllAssets(assetsPath);
        }
        // Load shaders from Assets directory to renderer
        m_fileManager->LoadAssetShaders(assetsPath, m_renderer.get());
    }

    INFLOG_DEBUG("Prepare pipeline.");
    m_renderer->PreparePipeline();

    // Set ImGui ini file path to user's Documents folder for per-project
    // layout persistence (keeps project directory clean / not in VCS).
    // We use std::filesystem::path throughout (wide-char on Windows) so
    // paths with non-ASCII characters (e.g. Chinese usernames) work.
    {
        std::filesystem::path layoutDir;
#ifdef _WIN32
        wchar_t docsPath[MAX_PATH] = {};
        if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docsPath) == S_OK) {
            std::filesystem::path projFs(projectPath);
            std::string projectName = projFs.filename().string();
            layoutDir = std::filesystem::path(docsPath) / "InfEngine" / projectName;
        }
#else
        const char *home = std::getenv("HOME");
        if (home) {
            std::filesystem::path projFs(projectPath);
            std::string projectName = projFs.filename().string();
            layoutDir = std::filesystem::path(home) / ".config" / "InfEngine" / projectName;
        }
#endif
        if (layoutDir.empty()) {
            layoutDir = std::filesystem::path(projectPath);
        }
        std::filesystem::create_directories(layoutDir);
        m_imguiIniPath = layoutDir / "imgui.ini";
    }
    // Disable ImGui auto-save (it uses fopen which can't handle Unicode
    // paths on Windows). We manually load/save with std::fstream instead.
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    LoadImGuiLayout();

    // Initialize physics world (Jolt)
    PhysicsWorld::Instance().Initialize();

    // Initialize audio engine (SDL3 audio)
    if (!AudioEngine::Instance().Initialize()) {
        INFLOG_WARN("Audio engine failed to initialize. Audio features will be unavailable.");
    }
}

void InfEngine::SetGUIFont(const std::string &fontPath, float fontSize)
{
    if (!CheckEngineValid("set GUI font") || !m_renderer) {
        return;
    }
    m_renderer->SetGUIFont(fontPath.c_str(), fontSize);
}

void InfEngine::RegisterGUIRenderable(const std::string &name, std::shared_ptr<InfGUIRenderable> renderable)
{
    if (!CheckEngineValid("register GUI renderable") || !m_renderer) {
        return;
    }
    m_renderer->RegisterGUIRenderable(name.c_str(), renderable);
}

void InfEngine::UnregisterGUIRenderable(const std::string &name)
{
    if (!m_renderer || m_isCleanedUp) {
        // Silent return during cleanup
        return;
    }
    m_renderer->UnregisterGUIRenderable(name.c_str());
}

void InfEngine::ShowWindow()
{
    if (!CheckEngineValid("show window") || !m_renderer) {
        return;
    }
    m_renderer->ShowWindow();
}

void InfEngine::HideWindow()
{
    if (!CheckEngineValid("hide window") || !m_renderer) {
        return;
    }
    m_renderer->HideWindow();
}

void InfEngine::SetWindowIcon(const std::string &iconPath)
{
    if (!CheckEngineValid("set window icon") || !m_renderer) {
        return;
    }
    m_renderer->SetWindowIcon(iconPath);
}

bool InfEngine::IsCloseRequested() const
{
    return m_renderer && m_renderer->IsCloseRequested();
}

void InfEngine::ConfirmClose()
{
    if (m_renderer) {
        m_renderer->ConfirmClose();
    }
}

void InfEngine::CancelClose()
{
    if (m_renderer) {
        m_renderer->CancelClose();
    }
}

// ----------------------------------
// ImGui texture management
// ----------------------------------

uint64_t InfEngine::UploadTextureForImGui(const std::string &name, const std::vector<unsigned char> &pixels, int width,
                                          int height)
{
    if (!CheckEngineValid("upload texture") || !m_renderer) {
        return 0;
    }
    return m_renderer->UploadTextureForImGui(name, pixels.data(), width, height);
}

void InfEngine::RemoveImGuiTexture(const std::string &name)
{
    if (!CheckEngineValid("remove texture") || !m_renderer) {
        return;
    }
    m_renderer->RemoveImGuiTexture(name);
}

bool InfEngine::HasImGuiTexture(const std::string &name) const
{
    if (!m_renderer || m_isCleanedUp) {
        return false;
    }
    return m_renderer->HasImGuiTexture(name);
}

uint64_t InfEngine::GetImGuiTextureId(const std::string &name) const
{
    if (!m_renderer || m_isCleanedUp) {
        return 0;
    }
    return m_renderer->GetImGuiTextureId(name);
}

ResourcePreviewManager *InfEngine::GetResourcePreviewManager()
{
    if (!m_renderer || m_isCleanedUp) {
        return nullptr;
    }
    return m_renderer->GetResourcePreviewManager();
}

// ----------------------------------
// Scene Render Target
// ----------------------------------

uint64_t InfEngine::GetSceneTextureId() const
{
    if (m_isCleanedUp || !m_renderer) {
        return 0;
    }
    return m_renderer->GetSceneTextureId();
}

void InfEngine::ResizeSceneRenderTarget(uint32_t width, uint32_t height)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_renderer->ResizeSceneRenderTarget(width, height);
}

// ----------------------------------
// Game Camera Render Target
// ----------------------------------

uint64_t InfEngine::GetGameTextureId() const
{
    if (m_isCleanedUp || !m_renderer) {
        return 0;
    }
    return m_renderer->GetGameTextureId();
}

void InfEngine::ResizeGameRenderTarget(uint32_t width, uint32_t height)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_renderer->ResizeGameRenderTarget(width, height);
}

void InfEngine::SetGameCameraEnabled(bool enabled)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_renderer->SetGameCameraEnabled(enabled);
}

bool InfEngine::IsGameCameraEnabled() const
{
    if (m_isCleanedUp || !m_renderer) {
        return false;
    }
    return m_renderer->IsGameCameraEnabled();
}

InfScreenUIRenderer *InfEngine::GetScreenUIRenderer()
{
    if (m_isCleanedUp || !m_renderer) {
        return nullptr;
    }
    return m_renderer->GetScreenUIRenderer();
}

// ----------------------------------
// MSAA Configuration
// ----------------------------------

void InfEngine::SetMsaaSamples(int samples)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_renderer->SetMsaaSamples(samples);
}

int InfEngine::GetMsaaSamples() const
{
    if (m_isCleanedUp || !m_renderer) {
        return 4;
    }
    return m_renderer->GetMsaaSamples();
}

// ----------------------------------
// Render Graph API
// ----------------------------------

SceneRenderGraph *InfEngine::GetSceneRenderGraph()
{
    if (m_isCleanedUp || !m_renderer) {
        return nullptr;
    }
    return m_renderer->GetSceneRenderGraph();
}

GizmosDrawCallBuffer *InfEngine::GetGizmosDrawCallBuffer()
{
    if (m_isCleanedUp || !m_renderer) {
        return nullptr;
    }
    return m_renderer->GetGizmosDrawCallBuffer();
}

std::vector<std::string> InfEngine::GetReadablePassNames() const
{
    if (m_isCleanedUp || !m_renderer) {
        return {};
    }
    SceneRenderGraph *renderGraph = const_cast<InfRenderer *>(m_renderer.get())->GetSceneRenderGraph();
    if (!renderGraph) {
        return {};
    }
    return renderGraph->GetReadablePassNames();
}

bool InfEngine::ReadPassColorPixels(const std::string &passName, std::vector<uint8_t> &outData)
{
    if (m_isCleanedUp || !m_renderer) {
        return false;
    }
    SceneRenderGraph *renderGraph = m_renderer->GetSceneRenderGraph();
    if (!renderGraph) {
        return false;
    }
    return renderGraph->ReadPassColorPixels(passName, outData);
}

bool InfEngine::ReadPassDepthPixels(const std::string &passName, std::vector<float> &outData)
{
    if (m_isCleanedUp || !m_renderer) {
        return false;
    }
    SceneRenderGraph *renderGraph = m_renderer->GetSceneRenderGraph();
    if (!renderGraph) {
        return false;
    }
    return renderGraph->ReadPassDepthPixels(passName, outData);
}

bool InfEngine::GetPassOutputSize(const std::string &passName, uint32_t &outWidth, uint32_t &outHeight) const
{
    if (m_isCleanedUp || !m_renderer) {
        return false;
    }
    SceneRenderGraph *renderGraph = const_cast<InfRenderer *>(m_renderer.get())->GetSceneRenderGraph();
    if (!renderGraph) {
        return false;
    }
    return renderGraph->GetPassOutputSize(passName, outWidth, outHeight);
}

uint64_t InfEngine::GetPassTextureId(const std::string &passName) const
{
    if (m_isCleanedUp || !m_renderer) {
        return 0;
    }
    SceneRenderGraph *renderGraph = const_cast<InfRenderer *>(m_renderer.get())->GetSceneRenderGraph();
    if (!renderGraph) {
        return 0;
    }
    return renderGraph->GetPassTextureId(passName);
}

// ----------------------------------
// Editor Gizmos
// ----------------------------------

void InfEngine::SetShowGrid(bool show)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_renderer->SetShowGrid(show);
}

bool InfEngine::IsShowGrid() const
{
    if (m_isCleanedUp || !m_renderer) {
        return false;
    }
    return m_renderer->IsShowGrid();
}

void InfEngine::SetSelectionOutline(uint64_t objectId)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }

    // Store for frame-by-frame updates
    m_selectedObjectId = objectId;
    m_renderer->SetSelectedObjectId(objectId);

    auto &gizmos = m_renderer->GetEditorGizmos();

    if (objectId == 0) {
        gizmos.ClearSelectionOutline();
        return;
    }

    // Find the object and its mesh data
    Scene *scene = SceneManager::Instance().GetActiveScene();
    if (!scene) {
        gizmos.ClearSelectionOutline();
        return;
    }

    GameObject *obj = scene->FindByID(objectId);
    if (!obj || !obj->IsActiveInHierarchy()) {
        gizmos.ClearSelectionOutline();
        return;
    }

    MeshRenderer *renderer = obj->GetComponent<MeshRenderer>();
    if (!renderer || !renderer->IsEnabled()) {
        gizmos.ClearSelectionOutline();
        return;
    }

    // Get mesh data
    if (renderer->HasInlineMesh()) {
        const auto &inlineVerts = renderer->GetInlineVertices();
        const auto &inlineInds = renderer->GetInlineIndices();

        // Extract positions and normals from Vertex structures
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        positions.reserve(inlineVerts.size());
        normals.reserve(inlineVerts.size());
        for (const auto &v : inlineVerts) {
            positions.push_back(v.pos);
            normals.push_back(v.normal);
        }

        // Indices are already uint32_t, use directly
        const auto &indices = inlineInds;

        glm::mat4 worldMatrix = obj->GetTransform()->GetWorldMatrix();
        gizmos.SetSelectionOutline(positions, normals, indices, worldMatrix);
    } else {
        // For non-inline meshes, we can't easily get vertex data here
        // Fall back to no outline or AABB outline
        gizmos.ClearSelectionOutline();
    }
}

void InfEngine::ClearSelectionOutline()
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_selectedObjectId = 0;
    m_renderer->SetSelectedObjectId(0);
    m_renderer->GetEditorGizmos().ClearSelectionOutline();
}

// ----------------------------------
// Material Pipeline
// ----------------------------------

bool InfEngine::EnsureShaderLoaded(const std::string &shaderId, const std::string &shaderType)
{
    // Check if shader is already loaded
    if (m_renderer->HasShader(shaderId, shaderType)) {
        return true;
    }

    INFLOG_DEBUG("InfEngine::EnsureShaderLoaded: shader '", shaderId, "' (", shaderType,
                 ") not loaded, trying to find and load it");

    // Try to find shader file path by shader_id
    std::string shaderPath = m_fileManager->FindShaderPathById(shaderId, shaderType);
    if (shaderPath.empty()) {
        INFLOG_WARN("InfEngine::EnsureShaderLoaded: could not find shader file for '", shaderId, "' (", shaderType,
                    ")");
        return false;
    }

    INFLOG_DEBUG("InfEngine::EnsureShaderLoaded: found shader at '", shaderPath, "', loading...");

    // Load the shader
    return ReloadShader(shaderPath);
}

bool InfEngine::RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material)
{
    INFLOG_DEBUG("InfEngine::RefreshMaterialPipeline called");
    if (!CheckEngineValid("refresh material pipeline") || !m_renderer || !m_fileManager) {
        INFLOG_ERROR("InfEngine::RefreshMaterialPipeline: engine or renderer invalid");
        return false;
    }

    if (!material) {
        INFLOG_ERROR("InfEngine::RefreshMaterialPipeline: material is null");
        return false;
    }

    // Get shader IDs from material
    const std::string &vertShaderId = material->GetVertexShaderPath();
    const std::string &fragShaderId = material->GetFragmentShaderPath();

    // Ensure shaders are loaded before refreshing pipeline
    if (!vertShaderId.empty()) {
        EnsureShaderLoaded(vertShaderId, "vertex");
    }
    if (!fragShaderId.empty()) {
        EnsureShaderLoaded(fragShaderId, "fragment");
    }

    INFLOG_DEBUG("InfEngine::RefreshMaterialPipeline: calling renderer");
    return m_renderer->RefreshMaterialPipeline(material);
}

bool InfEngine::ReloadShader(const std::string &shaderPath)
{
    INFLOG_INFO("InfEngine::ReloadShader called: ", shaderPath);
    if (!CheckEngineValid("reload shader") || !m_renderer || !m_fileManager) {
        INFLOG_ERROR("InfEngine::ReloadShader: engine, renderer or file manager invalid");
        return false;
    }

    // Determine shader type from extension
    std::filesystem::path path(shaderPath);
    std::string ext = path.extension().string();

    if (ext != ".vert" && ext != ".frag") {
        INFLOG_ERROR("InfEngine::ReloadShader: unsupported shader extension: ", ext);
        return false;
    }

    // Get shader_id from filename (before reloading)
    std::string shaderId = path.stem().string();

    // Check if resource already exists and unload it first
    const InfResourceMeta *existingMeta = m_fileManager->GetMetaByPath(shaderPath);
    if (existingMeta) {
        // Get shader_id from existing meta
        if (existingMeta->HasKey("shader_id")) {
            shaderId = existingMeta->GetDataAs<std::string>("shader_id");
        }
        std::string existingUid = existingMeta->GetGuid();
        m_fileManager->UnloadResource(existingUid);
        INFLOG_DEBUG("InfEngine::ReloadShader: unloaded old resource: ", existingUid);
    }

    // Re-register and reload the resource (compiles shader to SPIR-V)
    ResourceType type = ResourceType::Shader;
    std::string uid = m_fileManager->RegisterResource(shaderPath, type);

    if (uid.empty()) {
        INFLOG_ERROR("InfEngine::ReloadShader: failed to register resource: ", shaderPath);
        return false;
    }

    m_fileManager->LoadResource(uid);

    // Get compiled SPIR-V data from resource
    const InfResource *resource = m_fileManager->GetResource(uid);
    if (!resource) {
        INFLOG_ERROR("InfEngine::ReloadShader: no resource for: ", shaderPath);
        return false;
    }

    auto compiledDataPtr = std::static_pointer_cast<std::vector<char>>(resource->GetCompiledData());
    if (!compiledDataPtr || compiledDataPtr->empty()) {
        INFLOG_ERROR("InfEngine::ReloadShader: no compiled data for: ", shaderPath);
        return false;
    }

    // Update shader_id from newly loaded meta if available
    const InfResourceMeta *meta = m_fileManager->GetMetaByGuid(uid);
    if (meta && meta->HasKey("shader_id")) {
        shaderId = meta->GetDataAs<std::string>("shader_id");
    }

    // CRITICAL: Invalidate shader cache BEFORE loading new shader code
    // This clears ShaderProgramCache and MaterialPipelineManager caches
    // to force pipeline recreation with updated SPIR-V
    m_renderer->InvalidateShaderCache(shaderId);

    // Reload shader in renderer
    std::string shaderType = (ext == ".vert") ? "vertex" : "fragment";
    m_renderer->LoadShader(shaderId.c_str(), *compiledDataPtr, shaderType.c_str());

    INFLOG_INFO("InfEngine::ReloadShader: reloaded shader '", shaderId, "' from ", shaderPath);

    // Refresh all materials using this shader
    return m_renderer->RefreshMaterialsUsingShader(shaderId);
}

// ----------------------------------
// Render Pipeline (SRP)
// ----------------------------------

void InfEngine::SetRenderPipeline(std::shared_ptr<RenderPipelineCallback> pipeline)
{
    if (!m_renderer) {
        INFLOG_WARN("InfEngine::SetRenderPipeline: renderer not initialized");
        return;
    }
    m_renderer->SetRenderPipeline(std::move(pipeline));
}

// ----------------------------------
// Debug
// ----------------------------------

void InfEngine::SetLogLevel(LogLevel engineLevel)
{
    INFLOG_SET_LEVEL(engineLevel);
    m_logLevel = engineLevel;
}

// ----------------------------------
// ImGui layout save / load (Unicode-safe)
// ----------------------------------

void InfEngine::ResetImGuiLayout()
{
    // Clear ImGui's in-memory ini state (windows, docking, tables)
    ImGui::ClearIniSettings();
    // Delete the persisted ini file so the reset survives a restart
    if (!m_imguiIniPath.empty() && std::filesystem::exists(m_imguiIniPath)) {
        std::filesystem::remove(m_imguiIniPath);
    }
}

void InfEngine::LoadImGuiLayout()
{
    if (!std::filesystem::exists(m_imguiIniPath))
        return;
    // std::ifstream(std::filesystem::path) uses wchar_t on Windows,
    // so paths with Chinese / non-ASCII characters are handled properly.
    std::ifstream ifs(m_imguiIniPath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
        return;
    auto size = ifs.tellg();
    if (size <= 0)
        return;
    ifs.seekg(0);
    std::string data(static_cast<size_t>(size), '\0');
    ifs.read(data.data(), size);
    ImGui::LoadIniSettingsFromMemory(data.c_str(), data.size());
}

void InfEngine::SaveImGuiLayout()
{
    if (m_imguiIniPath.empty())
        return;
    size_t dataSize = 0;
    const char *data = ImGui::SaveIniSettingsToMemory(&dataSize);
    if (!data || dataSize == 0)
        return;
    std::filesystem::create_directories(m_imguiIniPath.parent_path());
    std::ofstream ofs(m_imguiIniPath, std::ios::binary);
    if (ofs.is_open())
        ofs.write(data, static_cast<std::streamsize>(dataSize));
}

} // namespace infengine