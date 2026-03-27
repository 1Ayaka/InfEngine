/**
 * @file InfEngine.cpp
 * @brief InfEngine — Core lifecycle, resources, renderer init, gizmos, material pipeline
 *
 * Editor camera control → InfEngineCamera.cpp
 * Scene picking / raycasting → ScenePicker.cpp
 */

#include "InfEngine.h"
// Explicit includes for types now only forward-declared in InfRenderer.h
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <function/audio/AudioClipLoader.h>
#include <function/audio/AudioEngine.h>
#include <function/renderer/EditorGizmos.h>
#include <function/renderer/GizmosDrawCallBuffer.h>
#include <function/renderer/SceneRenderGraph.h>
#include <function/renderer/ScriptableRenderContext.h>
#include <function/renderer/gui/InfGUIContext.h>
#include <function/renderer/gui/InfScreenUIRenderer.h>
#include <function/resources/AssetDependencyGraph.h>
#include <function/resources/AssetRegistry/AssetRegistry.h>
#include <function/resources/InfFileLoader/InfDefaultLoader.hpp>
#include <function/resources/InfFileLoader/InfPythonScriptLoader.hpp>
#include <function/resources/InfFileLoader/InfShaderLoader.hpp>
#include <function/resources/InfMaterial/MaterialLoader.h>
#include <function/resources/InfMesh/MeshLoader.h>
#include <function/resources/InfTexture/InfTexture.h>
#include <function/resources/InfTexture/TextureLoader.h>
#include <function/resources/ShaderAsset/ShaderAsset.h>
#include <function/resources/ShaderAsset/ShaderLoader.h>
#include <function/scene/Component.h>
#include <function/scene/MeshRenderer.h>
#include <function/scene/SceneRenderer.h>
#include <function/scene/physics/PhysicsWorld.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unordered_map>
#include <unordered_set>

#include <core/config/InfPlatform.h>
#ifdef INF_PLATFORM_WINDOWS
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
    if (!CheckEngineValid("modify resources"))
        return;
    auto *adb = GetAssetDatabase();
    if (adb)
        adb->ModifyResource(filePath);
}

void InfEngine::DeleteResources(const std::string &filePath)
{
    if (!CheckEngineValid("delete resources"))
        return;
    auto *adb = GetAssetDatabase();
    if (adb)
        adb->DeleteResource(filePath);
}

void InfEngine::MoveResources(const std::string &oldFilePath, const std::string &newFilePath)
{
    if (!CheckEngineValid("move resources"))
        return;
    auto *adb = GetAssetDatabase();
    if (adb)
        adb->MoveResource(oldFilePath, newFilePath);
}

AssetDatabase *InfEngine::GetAssetDatabase() const
{
    // After InitRenderer, ownership is transferred to AssetRegistry
    auto *adb = AssetRegistry::Instance().GetAssetDatabase();
    return adb ? adb : m_assetDatabase.get();
}

// ----------------------------------
// Lifecycle
// ----------------------------------

InfEngine::InfEngine(std::string dllPath) : m_isCleanedUp(false)
{
    INFLOG_DEBUG("Create InfEngine.");
    m_assetDatabase = std::make_unique<AssetDatabase>();

    INFLOG_DEBUG("Create InfEngine Renderer.");
    m_renderer = std::make_unique<InfRenderer>();
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
        try {
            m_renderer->DrawFrame();
        } catch (const std::exception &ex) {
            INFLOG_ERROR("Exception in DrawFrame: {}", ex.what());
        } catch (...) {
            INFLOG_ERROR("Unknown exception in DrawFrame!");
        }

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

    // AssetRegistry owns all loaded assets + builtins.
    AssetRegistry::Instance().Shutdown();

    m_assetDatabase.reset();
    m_extLoader.reset();

    INFLOG_DEBUG("Cleanup completed.");
#if INFENGINE_FILE_LOGGING
    INFLOG_FLUSH_FILE();
#endif

    m_isCleanedUp = true;
    m_isCleaningUp = false;
}

// ----------------------------------
// Renderer initialization
// ----------------------------------

void InfEngine::InitRenderer(int width, int height, const std::string &projectPath,
                             const std::string &builtinResourcePath)
{
    if (!CheckEngineValid("initialize renderer") || !m_renderer) {
        INFLOG_ERROR("Cannot initialize renderer: Renderer is not available.");
        return;
    }

    m_renderer->Init(width, height, m_metadata);

    // Redirect log output to Logs/engine.log.
    // Debug / RelWithDebInfo: truncate on startup and write through.
    // Release: retain only the last 100 lines and dump them on exit.
#if INFENGINE_FILE_LOGGING
    {
        auto logsDir = ToFsPath(JoinPath({projectPath, "Logs"}));
        std::filesystem::create_directories(logsDir);
        auto logFile = logsDir / "engine.log";
#if INFENGINE_DEFERRED_FILE_LOGGING
        INFLOG_SET_DEFERRED_FILE(FromFsPath(logFile), 100);
#else
        INFLOG_SET_FILE(FromFsPath(logFile));
#endif
    }
#endif

    INFLOG_DEBUG("Load shaders.");
    std::string defaultShaderPath = JoinPath({builtinResourcePath, "shaders"});
    std::string assetsPath = JoinPath({projectPath, "Assets"});
    if (m_assetDatabase) {
        // Register the builtin shader search path for @import resolution
        InfShaderLoader::AddShaderSearchPath(defaultShaderPath);

        m_assetDatabase->Initialize(projectPath);

        // ── Transfer AssetDatabase ownership to AssetRegistry ──────
        auto &registry = AssetRegistry::Instance();
        registry.Initialize(std::move(m_assetDatabase));

        // Register loader plug-ins for all asset types
        registry.RegisterLoader(ResourceType::Material, std::make_unique<MaterialLoader>());
        registry.RegisterLoader(ResourceType::Texture, std::make_unique<TextureLoader>());
        registry.RegisterLoader(ResourceType::Mesh, std::make_unique<MeshLoader>());
        registry.RegisterLoader(ResourceType::Audio, std::make_unique<AudioClipLoader>());
        registry.RegisterLoader(ResourceType::Shader, std::make_unique<ShaderLoader>());
        registry.RegisterLoader(ResourceType::Script, std::make_unique<InfPythonScriptLoader>());
        registry.RegisterLoader(ResourceType::DefaultText, std::make_unique<InfDefaultTextLoader>());
        registry.RegisterLoader(ResourceType::DefaultBinary, std::make_unique<InfDefaultBinaryLoader>());

        // Populate AssetDatabase's meta-loader table from registered loaders
        registry.PopulateAssetDatabaseLoaders();

        // Register the builtin resource directory as an extra scan root
        // so that Library/Resources assets (materials, etc.) get GUIDs.
        if (!builtinResourcePath.empty())
            registry.GetAssetDatabase()->AddScanRoot(builtinResourcePath);

        registry.GetAssetDatabase()->Refresh();

        // ── Load and register shaders via AssetRegistry ─────────────
        LoadAndRegisterShaders(defaultShaderPath, false);
        LoadAndRegisterShaders(assetsPath, true);

        // ── Register unified asset event callbacks ──────────────────
        auto &graph = AssetDependencyGraph::Instance();

        auto resolveMaterial = [](const std::string &matGuid) -> std::shared_ptr<InfMaterial> {
            auto mat = AssetRegistry::Instance().GetAsset<InfMaterial>(matGuid);
            if (mat)
                return mat;
            auto *adb = AssetRegistry::Instance().GetAssetDatabase();
            if (adb) {
                std::string matPath = adb->GetPathFromGuid(matGuid);
                if (!matPath.empty())
                    mat = AssetRegistry::Instance().LoadAssetByPath<InfMaterial>(matPath, ResourceType::Material);
            }
            return mat;
        };

        graph.RegisterCallback(ResourceType::Texture, [this, resolveMaterial](const std::string &dependentGuid,
                                                                              const std::string &texGuid,
                                                                              AssetEvent event) {
            auto mat = resolveMaterial(dependentGuid);
            if (!mat)
                return;

            if (event == AssetEvent::Deleted) {
                bool changed = false;
                for (const auto &[propName, prop] : mat->GetAllProperties()) {
                    if (prop.type != MaterialPropertyType::Texture2D)
                        continue;
                    const auto *val = std::get_if<std::string>(&prop.value);
                    if (!val || *val != texGuid)
                        continue;
                    mat->ClearTexture(propName);
                    changed = true;
                    INFLOG_INFO("AssetGraph: cleared texture '", propName, "' from material '", mat->GetName(), "'");
                }
                if (changed)
                    mat->SaveToFile();
            }

            if (event == AssetEvent::Deleted || event == AssetEvent::Modified) {
                if (m_renderer) {
                    std::string matName = mat->GetMaterialKey();
                    if (matName.empty())
                        matName = mat->GetName();
                    m_renderer->RemoveMaterialPipeline(matName);
                    mat->MarkPropertiesDirty();
                    INFLOG_INFO("AssetGraph: invalidated pipeline for material '", matName, "' (texture changed)");
                }
            }
        });

        graph.RegisterCallback(ResourceType::Material,
                               [](const std::string &dependentGuid, const std::string & /*matGuid*/, AssetEvent event) {
                                   if (event != AssetEvent::Deleted)
                                       return;
                                   auto *comp = Component::FindByInstanceGuid(dependentGuid);
                                   if (!comp)
                                       return;
                                   auto *mr = dynamic_cast<MeshRenderer *>(comp);
                                   if (!mr)
                                       return;
                                   auto fallback = AssetRegistry::Instance().GetBuiltinMaterial("ErrorMaterial");
                                   if (fallback)
                                       mr->SetMaterial(0, fallback);
                                   INFLOG_INFO("AssetGraph: reassigned MeshRenderer to error material");
                               });

        graph.RegisterCallback(ResourceType::Mesh, [](const std::string &dependentGuid,
                                                      const std::string & /*meshGuid*/, AssetEvent event) {
            auto *comp = Component::FindByInstanceGuid(dependentGuid);
            if (!comp)
                return;
            auto *mr = dynamic_cast<MeshRenderer *>(comp);
            if (!mr)
                return;
            mr->OnMeshAssetEvent(event);
            INFLOG_INFO("AssetGraph: refreshed MeshRenderer mesh state");
        });

        graph.RegisterCallback(ResourceType::Shader, [this, resolveMaterial](const std::string &dependentGuid,
                                                                             const std::string & /*shaderGuid*/,
                                                                             AssetEvent event) {
            if (event != AssetEvent::Modified && event != AssetEvent::Deleted)
                return;
            auto mat = resolveMaterial(dependentGuid);
            if (!mat)
                return;
            mat->MarkPipelineDirty();
            INFLOG_INFO("AssetGraph: marked material '", mat->GetName(), "' pipeline dirty (shader changed)");
        });
    }

    INFLOG_DEBUG("Prepare pipeline.");
    m_renderer->PreparePipeline();

    // Set ImGui ini file path to user's Documents folder for per-project
    // layout persistence (keeps project directory clean / not in VCS).
    // We use std::filesystem::path throughout (wide-char on Windows) so
    // paths with non-ASCII characters (e.g. Chinese usernames) work.
    {
        std::filesystem::path layoutDir;
#ifdef INF_PLATFORM_WINDOWS
        wchar_t docsPath[MAX_PATH] = {};
        if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docsPath) == S_OK) {
            std::filesystem::path projFs = ToFsPath(projectPath);
            std::filesystem::path projectNameFs = projFs.filename();
            layoutDir = std::filesystem::path(docsPath) / L"InfEngine" / projectNameFs;
        }
#else
        const char *home = std::getenv("HOME");
        if (home) {
            std::filesystem::path projFs = ToFsPath(projectPath);
            std::filesystem::path projectNameFs = projFs.filename();
            layoutDir = std::filesystem::path(home) / ".config" / "InfEngine" / projectNameFs;
        }
#endif
        if (layoutDir.empty()) {
            layoutDir = ToFsPath(projectPath);
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

// ----------------------------------
// Mesh geometry extraction helper (shared by SetSelectionOutline / SetSelectionOutlines)
// ----------------------------------

static bool ExtractMeshGeometry(MeshRenderer *renderer, std::vector<glm::vec3> &positions,
                                std::vector<glm::vec3> &normals, std::vector<uint32_t> &indices)
{
    positions.clear();
    normals.clear();
    indices.clear();

    if (renderer->HasInlineMesh()) {
        const auto &verts = renderer->GetInlineVertices();
        positions.reserve(verts.size());
        normals.reserve(verts.size());
        for (const auto &v : verts) {
            positions.push_back(v.pos);
            normals.push_back(v.normal);
        }
        indices = renderer->GetInlineIndices();
    } else if (renderer->HasMeshAsset()) {
        auto mesh = renderer->GetMeshAssetRef().Get();
        if (!mesh || mesh->GetVertices().empty() || mesh->GetIndices().empty())
            return false;

        const auto &meshVertices = mesh->GetVertices();
        const auto &meshIndices = mesh->GetIndices();
        int32_t nodeGroup = renderer->GetNodeGroup();

        if (nodeGroup >= 0) {
            std::unordered_map<uint32_t, uint32_t> vertexRemap;
            for (const auto &sub : mesh->GetSubMeshes()) {
                if (static_cast<int32_t>(sub.nodeGroup) != nodeGroup)
                    continue;
                for (uint32_t i = 0; i < sub.indexCount; ++i) {
                    uint32_t origIdx = meshIndices[sub.indexStart + i];
                    auto it = vertexRemap.find(origIdx);
                    if (it == vertexRemap.end()) {
                        uint32_t newIdx = static_cast<uint32_t>(positions.size());
                        vertexRemap[origIdx] = newIdx;
                        positions.push_back(meshVertices[origIdx].pos);
                        normals.push_back(meshVertices[origIdx].normal);
                        indices.push_back(newIdx);
                    } else {
                        indices.push_back(it->second);
                    }
                }
            }
        } else {
            positions.reserve(meshVertices.size());
            normals.reserve(meshVertices.size());
            for (const auto &v : meshVertices) {
                positions.push_back(v.pos);
                normals.push_back(v.normal);
            }
            indices = meshIndices;
        }
    } else {
        return false;
    }

    return !positions.empty() && !indices.empty();
}

// ----------------------------------
// Editor Gizmos
// ----------------------------------

void InfEngine::SetSelectionOutline(uint64_t objectId)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }

    m_cachedOutlineIds.clear();
    m_selectedObjectId = objectId;
    m_renderer->SetSelectedObjectId(objectId);

    auto &gizmos = m_renderer->GetEditorGizmos();

    if (objectId == 0) {
        gizmos.ClearSelectionOutline();
        return;
    }

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

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<uint32_t> indices;

    if (!ExtractMeshGeometry(renderer, positions, normals, indices)) {
        gizmos.ClearSelectionOutline();
        return;
    }

    glm::mat4 worldMatrix = obj->GetTransform()->GetWorldMatrix();
    gizmos.SetSelectionOutline(positions, normals, indices, worldMatrix);
}

void InfEngine::ClearSelectionOutline()
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }
    m_cachedOutlineIds.clear();
    m_selectedObjectId = 0;
    m_renderer->SetSelectedObjectId(0);
    m_renderer->GetEditorGizmos().ClearSelectionOutline();
}

void InfEngine::SetSelectionOutlines(const std::vector<uint64_t> &objectIds)
{
    if (m_isCleanedUp || !m_renderer) {
        return;
    }

    // Fast path: skip expensive mesh extraction when the ID set is unchanged.
    if (objectIds == m_cachedOutlineIds) {
        return;
    }
    m_cachedOutlineIds = objectIds;

    auto &gizmos = m_renderer->GetEditorGizmos();

    if (objectIds.empty()) {
        gizmos.ClearSelectionOutline();
        return;
    }

    if (objectIds.size() == 1) {
        SetSelectionOutline(objectIds[0]);
        return;
    }

    Scene *scene = SceneManager::Instance().GetActiveScene();
    if (!scene) {
        gizmos.ClearSelectionOutline();
        return;
    }

    std::vector<glm::vec3> mergedPositions;
    std::vector<glm::vec3> mergedNormals;
    std::vector<uint32_t> mergedIndices;

    for (uint64_t objId : objectIds) {
        GameObject *obj = scene->FindByID(objId);
        if (!obj || !obj->IsActiveInHierarchy())
            continue;

        MeshRenderer *renderer = obj->GetComponent<MeshRenderer>();
        if (!renderer || !renderer->IsEnabled())
            continue;

        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<uint32_t> indices;

        if (!ExtractMeshGeometry(renderer, positions, normals, indices))
            continue;

        glm::mat4 worldMatrix = obj->GetTransform()->GetWorldMatrix();
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldMatrix)));

        uint32_t baseIndex = static_cast<uint32_t>(mergedPositions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            glm::vec4 wp = worldMatrix * glm::vec4(positions[i], 1.0f);
            mergedPositions.push_back(glm::vec3(wp));
            glm::vec3 wn = glm::normalize(normalMatrix * normals[i]);
            mergedNormals.push_back(wn);
        }
        for (uint32_t idx : indices) {
            mergedIndices.push_back(idx + baseIndex);
        }
    }

    if (mergedPositions.empty() || mergedIndices.empty()) {
        gizmos.ClearSelectionOutline();
        return;
    }

    gizmos.SetSelectionOutline(mergedPositions, mergedNormals, mergedIndices, glm::mat4(1.0f));

    // Store the first valid ID for frame-by-frame matrix updates
    m_selectedObjectId = objectIds[0];
    m_renderer->SetSelectedObjectId(objectIds[0]);
}

// ----------------------------------
// Material Pipeline
// ----------------------------------

void InfEngine::RegisterShaderToRenderer(const ShaderAsset &asset)
{
    if (!m_renderer || asset.spirvForward.empty())
        return;

    m_renderer->LoadShader(asset.shaderId.c_str(), asset.spirvForward,
                           asset.shaderType == "vertex" ? "vertex" : "fragment");

    // Shadow vertex variant
    if (asset.shaderType == "vertex" && !asset.spirvShadowVertex.empty()) {
        std::string shadowId = asset.shaderId + "/shadow";
        m_renderer->LoadShader(shadowId.c_str(), asset.spirvShadowVertex, "vertex");
        INFLOG_INFO("Registered shadow vertex variant '", shadowId, "'");
    }

    // Shadow fragment variant
    if (asset.shaderType == "fragment" && !asset.spirvShadow.empty()) {
        std::string shadowId = asset.shaderId + "/shadow";
        m_renderer->LoadShader(shadowId.c_str(), asset.spirvShadow, "fragment");
        INFLOG_INFO("Registered shadow fragment variant '", shadowId, "'");
    }

    // GBuffer fragment variant
    if (asset.shaderType == "fragment" && !asset.spirvGBuffer.empty()) {
        std::string gbufferId = asset.shaderId + "/gbuffer";
        m_renderer->LoadShader(gbufferId.c_str(), asset.spirvGBuffer, "fragment");
        INFLOG_INFO("Registered GBuffer variant '", gbufferId, "'");
    }

    // Render-state metadata (fragment shaders only)
    if (asset.shaderType == "fragment") {
        const auto &rm = asset.renderMeta;
        m_renderer->StoreShaderRenderMeta(asset.shaderId, rm.cullMode, rm.depthWrite, rm.depthTest, rm.blend, rm.queue,
                                          rm.passTag, rm.stencil, rm.alphaClip);
    }
}

void InfEngine::LoadAndRegisterShaders(const std::string &dir, bool recursive)
{
    namespace fs = std::filesystem;
    auto &registry = AssetRegistry::Instance();
    auto *adb = registry.GetAssetDatabase();
    if (!adb || !m_renderer)
        return;

    fs::path dirPath = ToFsPath(dir);
    if (!fs::exists(dirPath))
        return;

    std::unordered_set<std::string> loadedShaderKeys;
    std::vector<char> unlitVertCode;
    std::vector<char> unlitFragCode;

    auto processEntry = [&](const fs::directory_entry &entry) {
        if (!entry.is_regular_file())
            return;
        fs::path file = entry.path();
        std::string ext = file.extension().string();

        if (ext != ".vert" && ext != ".frag")
            return;

        std::string filePath = FromFsPath(file);

        // Always register to ensure meta + GUID↔path mappings are cached
        std::string guid = adb->RegisterResource(filePath, ResourceType::Shader);
        if (guid.empty())
            return;

        // Get shader_id from meta
        std::string shaderId;
        const InfResourceMeta *meta = adb->GetMetaByGuid(guid);
        if (meta && meta->HasKey("shader_id")) {
            shaderId = meta->GetDataAs<std::string>("shader_id");
        }
        if (shaderId.empty()) {
            shaderId = FromFsPath(file.stem());
        }

        std::string shaderKey = shaderId + "_" + ext;

        // Skip duplicates
        if (loadedShaderKeys.count(shaderKey))
            return;

        // For recursive (asset) shaders, skip if already loaded in renderer
        if (recursive && m_renderer->HasShader(shaderId, ext == ".vert" ? "vertex" : "fragment")) {
            loadedShaderKeys.insert(shaderKey);
            return;
        }

        loadedShaderKeys.insert(shaderKey);

        // Load via AssetRegistry (compiles the shader)
        auto shaderAsset = registry.LoadAsset<ShaderAsset>(guid, ResourceType::Shader);
        if (!shaderAsset || shaderAsset->spirvForward.empty())
            return;

        // Register all variants with the renderer
        RegisterShaderToRenderer(*shaderAsset);

        INFLOG_DEBUG("Loaded shader '", shaderId, "' (", ext, ") from ", filePath);

        // Track unlit for default registration
        if (!recursive && shaderId == "unlit") {
            if (ext == ".vert")
                unlitVertCode = shaderAsset->spirvForward;
            else if (ext == ".frag")
                unlitFragCode = shaderAsset->spirvForward;
        }
    };

    if (recursive) {
        for (const auto &entry : fs::recursive_directory_iterator(dirPath))
            processEntry(entry);
    } else {
        for (const auto &entry : fs::directory_iterator(dirPath))
            processEntry(entry);
    }

    // Register 'unlit' as the default shader (non-recursive = builtin shaders only)
    if (!recursive) {
        if (!unlitVertCode.empty()) {
            m_renderer->LoadShader("default", unlitVertCode, "vertex");
            INFLOG_INFO("Registered 'unlit' as default vertex shader");
        }
        if (!unlitFragCode.empty()) {
            m_renderer->LoadShader("default", unlitFragCode, "fragment");
            INFLOG_INFO("Registered 'unlit' as default fragment shader");
        }
    }
}

bool InfEngine::EnsureShaderLoaded(const std::string &shaderId, const std::string &shaderType)
{
    if (m_renderer->HasShader(shaderId, shaderType)) {
        return true;
    }

    INFLOG_DEBUG("InfEngine::EnsureShaderLoaded: shader '", shaderId, "' (", shaderType,
                 ") not loaded, trying to find and load it");

    auto *adb = GetAssetDatabase();
    if (!adb) {
        INFLOG_WARN("InfEngine::EnsureShaderLoaded: no AssetDatabase available");
        return false;
    }
    std::string shaderPath = adb->FindShaderPathById(shaderId, shaderType);
    if (shaderPath.empty()) {
        INFLOG_WARN("InfEngine::EnsureShaderLoaded: could not find shader file for '", shaderId, "' (", shaderType,
                    ")");
        return false;
    }

    INFLOG_DEBUG("InfEngine::EnsureShaderLoaded: found shader at '", shaderPath, "', loading...");

    return ReloadShader(shaderPath).empty();
}

bool InfEngine::RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material)
{
    INFLOG_DEBUG("InfEngine::RefreshMaterialPipeline called");
    if (!CheckEngineValid("refresh material pipeline") || !m_renderer) {
        INFLOG_ERROR("InfEngine::RefreshMaterialPipeline: engine or renderer invalid");
        return false;
    }

    auto *adb = GetAssetDatabase();
    if (!adb) {
        INFLOG_ERROR("InfEngine::RefreshMaterialPipeline: no AssetDatabase available");
        return false;
    }

    if (!material) {
        INFLOG_ERROR("InfEngine::RefreshMaterialPipeline: material is null");
        return false;
    }

    // Get shader names from material
    const std::string &vertName = material->GetVertShaderName();
    const std::string &fragName = material->GetFragShaderName();

    // Ensure shaders are loaded before refreshing pipeline
    if (!vertName.empty()) {
        EnsureShaderLoaded(vertName, "vertex");
    }
    if (!fragName.empty()) {
        EnsureShaderLoaded(fragName, "fragment");
    }

    INFLOG_DEBUG("InfEngine::RefreshMaterialPipeline: calling renderer");
    return m_renderer->RefreshMaterialPipeline(material);
}

std::string InfEngine::ReloadShader(const std::string &shaderPath)
{
    INFLOG_INFO("InfEngine::ReloadShader called: ", shaderPath);
    if (!CheckEngineValid("reload shader") || !m_renderer) {
        INFLOG_ERROR("InfEngine::ReloadShader: engine or renderer invalid");
        return "Engine or renderer invalid";
    }

    auto &registry = AssetRegistry::Instance();
    auto *adb = registry.GetAssetDatabase();
    if (!adb) {
        INFLOG_ERROR("InfEngine::ReloadShader: no AssetDatabase available");
        return "No AssetDatabase available";
    }

    std::filesystem::path path(shaderPath);
    std::string ext = path.extension().string();

    if (ext != ".vert" && ext != ".frag") {
        INFLOG_ERROR("InfEngine::ReloadShader: unsupported shader extension: ", ext);
        return "Unsupported shader extension: " + ext;
    }

    // Get or create GUID for this shader
    std::string guid = adb->GetGuidFromPath(shaderPath);
    std::string oldShaderId;

    if (!guid.empty()) {
        // Get old shader_id before recompilation
        const InfResourceMeta *existingMeta = adb->GetMetaByGuid(guid);
        if (existingMeta && existingMeta->HasKey("shader_id")) {
            oldShaderId = existingMeta->GetDataAs<std::string>("shader_id");
        }
    }

    // Re-register meta (updates shader_id annotations from source)
    adb->ModifyResource(shaderPath);
    if (guid.empty()) {
        guid = adb->RegisterResource(shaderPath, ResourceType::Shader);
    } else {
        // Refresh the guid from the updated meta
        guid = adb->GetGuidFromPath(shaderPath);
    }

    if (guid.empty()) {
        INFLOG_ERROR("InfEngine::ReloadShader: failed to register resource: ", shaderPath);
        return "Failed to register resource: " + shaderPath;
    }

    // Invalidate the AssetRegistry cache so the shader gets recompiled
    registry.InvalidateAsset(guid);

    // Reload via AssetRegistry → ShaderLoader
    auto shaderAsset = registry.LoadAsset<ShaderAsset>(guid, ResourceType::Shader);
    if (!shaderAsset || shaderAsset->spirvForward.empty()) {
        INFLOG_ERROR("InfEngine::ReloadShader: compilation failed for: ", shaderPath);
        std::string compileErr = InfShaderLoader::s_lastCompileError;
        if (!compileErr.empty())
            return compileErr;
        return "Shader compilation failed (no compiled data)";
    }

    // Invalidate renderer caches BEFORE loading new shader code
    m_renderer->InvalidateShaderCache(shaderAsset->shaderId);
    if (!oldShaderId.empty() && oldShaderId != shaderAsset->shaderId) {
        m_renderer->InvalidateShaderCache(oldShaderId);
        INFLOG_INFO("InfEngine::ReloadShader: shader_id changed from '", oldShaderId, "' to '", shaderAsset->shaderId,
                    "'");
    }

    // Also invalidate variant caches
    m_renderer->InvalidateShaderCache(shaderAsset->shaderId + "/shadow");
    m_renderer->InvalidateShaderCache(shaderAsset->shaderId + "/gbuffer");

    // Register all SPIR-V variants with the renderer
    RegisterShaderToRenderer(*shaderAsset);

    INFLOG_INFO("InfEngine::ReloadShader: reloaded shader '", shaderAsset->shaderId, "' from ", shaderPath);

    // Refresh all materials using this shader
    m_renderer->RefreshMaterialsUsingShader(shaderAsset->shaderId);

    // If shader_id changed, update materials that referenced the old name
    if (!oldShaderId.empty() && oldShaderId != shaderAsset->shaderId) {
        auto materials = registry.GetAllMaterials();
        for (auto &material : materials) {
            if (!material)
                continue;
            if (material->GetFragShaderName() == oldShaderId) {
                material->SetFragShader(shaderAsset->shaderId);
                INFLOG_INFO("InfEngine::ReloadShader: updated material '", material->GetName(), "' frag shader from '",
                            oldShaderId, "' to '", shaderAsset->shaderId, "'");
            }
            if (material->GetVertShaderName() == oldShaderId) {
                material->SetVertShader(shaderAsset->shaderId);
                INFLOG_INFO("InfEngine::ReloadShader: updated material '", material->GetName(), "' vert shader from '",
                            oldShaderId, "' to '", shaderAsset->shaderId, "'");
            }
        }
        m_renderer->RefreshMaterialsUsingShader(shaderAsset->shaderId);
    }

    return ""; // success
}

void InfEngine::ReloadTexture(const std::string &texturePath)
{
    INFLOG_INFO("InfEngine::ReloadTexture called: ", texturePath);

    if (!CheckEngineValid("reload texture") || !m_renderer) {
        INFLOG_ERROR("InfEngine::ReloadTexture: engine or renderer invalid");
        return;
    }

    // Resolve path → GUID so that InvalidateTextureCache can match GUID-based cache keys
    auto &registry = AssetRegistry::Instance();
    auto *adb = registry.GetAssetDatabase();
    std::string guid;
    if (adb)
        guid = adb->GetGuidFromPath(texturePath);

    if (guid.empty()) {
        INFLOG_WARN("InfEngine::ReloadTexture: could not resolve GUID for '", texturePath, "'");
    }

    // Reload InfTexture metadata in AssetRegistry (import settings may have changed)
    if (!guid.empty() && registry.IsLoaded(guid)) {
        registry.ReloadAsset(guid);

        // Log the reloaded import settings for diagnostics
        auto infTex = registry.LoadAsset<InfTexture>(guid, ResourceType::Texture);
        if (infTex) {
            INFLOG_INFO(
                "InfEngine::ReloadTexture: InfTexture reloaded — IsLinear=", infTex->IsLinear() ? "true" : "false",
                ", GenerateMipmaps=", infTex->GenerateMipmaps() ? "true" : "false", ", GUID=", guid);
        }
    }

    // InvalidateTextureCache accepts both GUID and path — prefer GUID
    m_renderer->InvalidateTextureCache(!guid.empty() ? guid : texturePath);

    // Fire graph notification so dependent materials get their pipelines
    // invalidated via the Texture Modified callback.
    if (!guid.empty()) {
        auto dependents = AssetDependencyGraph::Instance().GetDependents(guid);
        INFLOG_INFO("InfEngine::ReloadTexture: NotifyEvent guid=", guid, " dependents=", dependents.size());
        AssetDependencyGraph::Instance().NotifyEvent(guid, ResourceType::Texture, AssetEvent::Modified);
    }

    INFLOG_INFO("InfEngine::ReloadTexture: done for '", texturePath, "'");
}

void InfEngine::ReloadMesh(const std::string &meshPath)
{
    INFLOG_INFO("InfEngine::ReloadMesh called: ", meshPath);

    if (!CheckEngineValid("reload mesh")) {
        INFLOG_ERROR("InfEngine::ReloadMesh: engine invalid");
        return;
    }

    auto &registry = AssetRegistry::Instance();
    auto *adb = registry.GetAssetDatabase();
    std::string guid;
    if (adb)
        guid = adb->GetGuidFromPath(meshPath);

    if (guid.empty()) {
        INFLOG_WARN("InfEngine::ReloadMesh: could not resolve GUID for '", meshPath, "'");
        return;
    }

    if (registry.IsLoaded(guid))
        registry.ReloadAsset(guid);

    auto dependents = AssetDependencyGraph::Instance().GetDependents(guid);
    INFLOG_INFO("InfEngine::ReloadMesh: NotifyEvent guid=", guid, " dependents=", dependents.size());
    AssetDependencyGraph::Instance().NotifyEvent(guid, ResourceType::Mesh, AssetEvent::Modified);

    INFLOG_INFO("InfEngine::ReloadMesh: done for '", meshPath, "'");
}

void InfEngine::ReloadAudio(const std::string &audioPath)
{
    INFLOG_INFO("InfEngine::ReloadAudio called: ", audioPath);

    if (!CheckEngineValid("reload audio")) {
        INFLOG_ERROR("InfEngine::ReloadAudio: engine invalid");
        return;
    }

    auto &registry = AssetRegistry::Instance();
    auto *adb = registry.GetAssetDatabase();
    std::string guid;
    if (adb)
        guid = adb->GetGuidFromPath(audioPath);

    if (guid.empty()) {
        INFLOG_WARN("InfEngine::ReloadAudio: could not resolve GUID for '", audioPath, "'");
        return;
    }

    if (registry.IsLoaded(guid))
        registry.ReloadAsset(guid);

    AssetDependencyGraph::Instance().NotifyEvent(guid, ResourceType::Audio, AssetEvent::Modified);

    INFLOG_INFO("InfEngine::ReloadAudio: done for '", audioPath, "'");
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