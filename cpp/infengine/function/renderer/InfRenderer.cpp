#include "InfRenderer.h"
// Explicit includes for all subsystem types that were forward-declared in
// InfRenderer.h. These are required here so that unique_ptr destructors,
// make_unique calls, and member-function calls all have complete types.
#include "EditorGizmos.h"
#include "EditorTools.h"
#include "GizmosDrawCallBuffer.h"
#include "InfVkCoreModular.h"
#include "OutlineRenderer.h"
#include "SceneRenderGraph.h"
#include "SceneRenderTarget.h"
#include "ScriptableRenderContext.h"
#include "TransientResourcePool.h"
#include "gui/InfGUI.h"
#include "gui/InfGUIContext.h"
#include "gui/InfScreenUIRenderer.h"
#include "vk/RenderGraph.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <function/scene/Camera.h>
#include <function/scene/Light.h>
#include <function/scene/LightingData.h>
#include <function/scene/MeshRenderer.h>
#include <function/scene/PrimitiveMeshes.h>
#include <function/scene/SceneManager.h>
#include <function/scene/SceneRenderer.h>
#include <platform/window/InfView.h>

namespace infengine
{
InfRenderer::InfRenderer(size_t graphicsThreads, size_t presentThreads, size_t transferThreads, size_t computeThreads)
{
    m_vkCore = std::make_unique<InfVkCoreModular>();
    m_view = std::make_unique<InfView>();
    m_vkCore->SetThreads(graphicsThreads, presentThreads, transferThreads, computeThreads);
}

InfRenderer::~InfRenderer()
{
    // ========================================================================
    // Shutdown order matters!  Every subsystem holds a raw VkCore/device
    // pointer, so they MUST be destroyed BEFORE m_vkCore.  We do ONE
    // vkDeviceWaitIdle up front, then tear down subsystems without further
    // GPU drains (each subsystem's individual WaitIdle is a no-op when the
    // device is already idle and the flag below is checked).
    // ========================================================================

    // 1. Single GPU drain — after this the device is idle, all subsequent
    //    vkDeviceWaitIdle calls inside subsystem destructors are redundant.
    if (m_vkCore) {
        m_vkCore->GetDeviceContext().WaitIdle();
        m_vkCore->SetShuttingDown(true);
    }

    // 2. Destroy all subsystems that hold raw VkCore pointers — order:
    //    render graphs → screen UI → render targets → auxiliary renderers → GUI.
    m_gameRenderGraph.reset();
    m_sceneRenderGraph.reset();

    m_screenUIRenderer.reset();

    m_gameRenderTarget.reset();
    m_sceneRenderTarget.reset();

    m_transientResourcePool.reset();

    m_outlineRenderer.reset();
    m_editorGizmos.reset();
    m_componentGizmos.reset();

    m_gui.reset();

    // 3. Now safe to destroy the Vulkan device itself.
    m_vkCore.reset();

    // 4. Tear down the platform window last.
    if (m_view) {
        m_view->Quit();
    }
    m_view.reset();
}

void InfRenderer::SetCameraPos(float x, float y, float z)
{
    m_cameraPos[0] = x;
    m_cameraPos[1] = y;
    m_cameraPos[2] = z;
}

void InfRenderer::SetCameraLookAt(float x, float y, float z)
{
    m_cameraLookAt[0] = x;
    m_cameraLookAt[1] = y;
    m_cameraLookAt[2] = z;
}

void InfRenderer::SetCameraUp(float x, float y, float z)
{
    m_cameraUp[0] = x;
    m_cameraUp[1] = y;
    m_cameraUp[2] = z;
}

float *InfRenderer::GetCameraPos()
{
    return &m_cameraPos[0];
}
float *InfRenderer::GetCameraLookAt()
{
    return &m_cameraLookAt[0];
}
float *InfRenderer::GetCameraUp()
{
    return &m_cameraUp[0];
}

void InfRenderer::TranslateCamera(float x, float y, float z)
{
    m_cameraPos[0] += x;
    m_cameraPos[1] += y;
    m_cameraPos[2] += z;
}

void InfRenderer::SetAppMetadata(InfAppMetadata appMetaData)
{
    m_appMetadata = appMetaData;
    m_view->SetAppMetadata(appMetaData);
}

InfAppMetadata InfRenderer::GetAppMetadata()
{
    return m_appMetadata;
}

InfAppMetadata InfRenderer::GetRendererMetadata()
{
    return m_rendererMetadata;
}

void InfRenderer::Init(int width, int height, InfAppMetadata appMetaData)
{
    SetAppMetadata(appMetaData);
    if (!m_vkCore) {
        INFLOG_ERROR("Failed to create InfVkCoreModular.");
        return;
    }

    m_vkCore->SetWindowSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    INFLOG_DEBUG("Init View.");
    m_view->Init(width, height);

    uint32_t extCount = 0;
    auto ext = m_view->GetVkExtensions(&extCount);
    if (!ext) {
        INFLOG_FATAL("Failed to get Vulkan extensions.");
    }

    INFLOG_DEBUG("Vulkan extensions count: ", extCount);
    for (uint32_t i = 0; i < extCount; ++i) {
        INFLOG_DEBUG("Vulkan extension: ", ext[i]);
    }

    m_vkCore->Init(m_appMetadata, m_rendererMetadata, extCount, const_cast<const char **>(ext));

    m_view->CreateSurface(&m_vkCore->m_instance, &m_vkCore->m_surface);

    INFLOG_DEBUG("Prepare surface.");
    m_vkCore->PrepareSurface();
}

void InfRenderer::PreparePipeline()
{
    if (m_vkCore) {
        m_vkCore->PreparePipeline();

        INFLOG_DEBUG("Init GUI.");
        m_gui = std::make_unique<InfGUI>(m_vkCore.get());
        m_gui->Init(m_view->m_window);

        // Initialize scene render target with default size
        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(m_vkCore.get());
        m_sceneRenderTarget->Initialize(800, 600);

        // Set initial scene render target size for aspect ratio calculation
        m_vkCore->SetSceneRenderTargetSize(800, 600);

        // Initialize default scene with gizmos
        InitializeDefaultScene();

        // Initialize RenderGraph pipeline (scene rendering in pre-render pass)
        if (!m_sceneRenderGraph) {
            m_sceneRenderGraph = std::make_unique<SceneRenderGraph>();
        }
        if (m_sceneRenderGraph) {
            m_sceneRenderGraph->Initialize(m_vkCore.get(), m_sceneRenderTarget.get());
        }

        // Hook RenderGraph execution into the pre-render callback
        m_vkCore->SetRenderGraphExecutor([this](VkCommandBuffer cmdBuf) {
            const bool sceneViewActive =
                (m_sceneRenderTarget && m_sceneRenderTarget->GetWidth() > 1 && m_sceneRenderTarget->GetHeight() > 1);

            // ---- Scene View: editor camera VP is already in UBO from UpdateUniformBuffer ----
            if (sceneViewActive && m_sceneRenderGraph) {
                // Swap in scene-specific draw calls (includes gizmos)
                if (m_sceneRenderGraph->HasCachedDrawCalls()) {
                    m_vkCore->SetDrawCalls(m_sceneRenderGraph->GetCachedDrawCalls());
                }
                // Set per-graph shadow descriptor (set 1) for multi-camera isolation
                m_vkCore->SetActiveShadowDescriptorSet(m_sceneRenderGraph->GetPerViewDescriptorSet());
                m_sceneRenderGraph->Execute(cmdBuf);
                m_sceneRenderGraph->ResolveSceneMsaa(cmdBuf);
            }

            // ---- Game View: use cached camera VP from SubmitCulling ----
            if (m_gameCameraEnabled && m_gameRenderGraph && m_gameRenderTarget && m_gameRenderTarget->IsReady()) {
                if (m_gameRenderGraph->HasCachedDrawCalls() && m_gameRenderGraph->HasCachedCameraVP()) {
                    // Swap in game-specific draw calls (no gizmos)
                    m_vkCore->SetDrawCalls(m_gameRenderGraph->GetCachedDrawCalls());

                    // Update UBO with game camera VP matrices cached during
                    // SetupCameraProperties() → SubmitCulling() in the SRP pipeline.
                    // This ensures the GPU sees the exact same VP used for culling.
                    m_vkCore->CmdUpdateUniformBuffer(cmdBuf, m_gameRenderGraph->GetCachedView(),
                                                     m_gameRenderGraph->GetCachedProj());

                    // Override lighting UBO camera position for the game view.
                    // Without this, specular/view-dependent lighting uses the
                    // editor camera position instead of the game camera position.
                    {
                        const glm::mat4 &gameView = m_gameRenderGraph->GetCachedView();
                        glm::mat4 invView = glm::inverse(gameView);
                        glm::vec3 gameCamPos(invView[3]);
                        m_vkCore->CmdUpdateLightingCameraPos(cmdBuf, gameCamPos);
                    }

                    // Set per-graph shadow descriptor (set 1) for game camera
                    m_vkCore->SetActiveShadowDescriptorSet(m_gameRenderGraph->GetPerViewDescriptorSet());
                    m_gameRenderGraph->Execute(cmdBuf);
                    m_gameRenderGraph->ResolveSceneMsaa(cmdBuf);

                    // Transition game color image (1x) from COLOR_ATTACHMENT_OPTIMAL
                    // to SHADER_READ_ONLY_OPTIMAL so that ImGui can safely sample it.
                    // Without this barrier the layout used by the ImGui descriptor set
                    // (SHADER_READ_ONLY) would not match the actual image layout,
                    // causing VK_ERROR_DEVICE_LOST on strict Vulkan drivers.
                    {
                        VkImageMemoryBarrier gameBarrier{};
                        gameBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        gameBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        gameBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        gameBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        gameBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        gameBarrier.image = m_gameRenderTarget->GetColorImage();
                        gameBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        gameBarrier.subresourceRange.baseMipLevel = 0;
                        gameBarrier.subresourceRange.levelCount = 1;
                        gameBarrier.subresourceRange.baseArrayLayer = 0;
                        gameBarrier.subresourceRange.layerCount = 1;
                        gameBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                        gameBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                        vkCmdPipelineBarrier(
                            cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &gameBarrier);
                    }

                    // Restore editor camera VP and lighting camera position for
                    // subsequent rendering (outlines, etc.)
                    if (m_sceneRenderGraph && m_sceneRenderGraph->HasCachedCameraVP()) {
                        m_vkCore->CmdUpdateUniformBuffer(cmdBuf, m_sceneRenderGraph->GetCachedView(),
                                                         m_sceneRenderGraph->GetCachedProj());
                        // Restore editor camera position in lighting UBO
                        const glm::mat4 &editorView = m_sceneRenderGraph->GetCachedView();
                        glm::mat4 invEditorView = glm::inverse(editorView);
                        glm::vec3 editorCamPos(invEditorView[3]);
                        m_vkCore->CmdUpdateLightingCameraPos(cmdBuf, editorCamPos);
                    }

                    // Restore scene draw calls
                    if (m_sceneRenderGraph && m_sceneRenderGraph->HasCachedDrawCalls()) {
                        m_vkCore->SetDrawCalls(m_sceneRenderGraph->GetCachedDrawCalls());
                    }

                    // Restore scene per-view descriptor for subsequent rendering
                    if (m_sceneRenderGraph) {
                        m_vkCore->SetActiveShadowDescriptorSet(m_sceneRenderGraph->GetPerViewDescriptorSet());
                    }
                }
            }
        });

        // GUI is rendered via RenderGraph pass (no more imperative BeginRenderPass/EndRenderPass)
        m_vkCore->SetGuiRenderCallback(
            [this](vk::RenderContext &ctx) { m_gui->RecordCommand(ctx.GetCommandBuffer()); });

        // Create TransientResourcePool for CommandBuffer temporary render targets
        m_transientResourcePool = std::make_unique<TransientResourcePool>();
        m_transientResourcePool->Initialize(&m_vkCore->GetDeviceContext(), &m_vkCore->GetResourceManager());

        // Create OutlineRenderer and wire post-scene-render callback
        m_outlineRenderer = std::make_unique<OutlineRenderer>();

        m_vkCore->SetPostSceneRenderCallback([this](VkCommandBuffer cmdBuf, const std::vector<DrawCall> &drawCalls) {
            // Post-processing effects are now integrated into the RenderGraph DAG
            // (transfer + graphics passes inside SceneRenderGraph::BuildRenderGraph).
            // No external PostProcessRenderer execution needed here.

            if (m_outlineRenderer && m_outlineRenderer->HasActiveOutline()) {
                m_outlineRenderer->RecordCommands(cmdBuf, drawCalls);
            } else if (m_outlineRenderer) {
                m_outlineRenderer->RecordNoOutlineBarrier(cmdBuf);
            }
        });
    }
}

void InfRenderer::DrawFrame()
{
    // Calculate delta time
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;

    // Clamp delta time to avoid huge jumps
    if (m_deltaTime > 0.1f)
        m_deltaTime = 0.1f;

    // ========================================================================
    // Frame Profiler — prints per-phase timings every 120 frames.
    // Controlled by INFENGINE_FRAME_PROFILE (define or set to 1 to enable).
    // ========================================================================
#ifndef INFENGINE_FRAME_PROFILE
#define INFENGINE_FRAME_PROFILE 0
#endif

    struct FrameProfiler
    {
        using Clock = std::chrono::high_resolution_clock;
        using TimePoint = Clock::time_point;
        TimePoint stamps[12];
        int idx = 0;
        void stamp()
        {
            if (idx < 12)
                stamps[idx++] = Clock::now();
        }
        float ms(int a, int b) const
        {
            return std::chrono::duration<float, std::milli>(stamps[b] - stamps[a]).count();
        }
    };
    [[maybe_unused]] FrameProfiler _fp;
    static int _fpCounter = 0;
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [0] frame start
    }

    // Window events
    m_view->ProcessEvent();

    // Skip rendering while the window is minimized.
    // This avoids a deadlock in vkAcquireNextImageKHR when the
    // swapchain extent is zero.
    if (m_view->IsMinimized()) {
        SDL_Delay(16);
        return;
    }

    // Update scene system
    SceneManager::Instance().Update(m_deltaTime);
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [1] after SceneManager::Update
    }

    // ========================================================================
    // Per-Frame Fence Synchronization (Fix 1: replaces vkDeviceWaitIdle)
    //
    // Wait on the current frame slot's fence so that GPU resources bound
    // to this slot are guaranteed idle.  Then flush deferred deletions
    // that have aged past maxFramesInFlight, ensuring no in-flight
    // command buffer references them.
    //
    // This replaces the blanket vkDeviceWaitIdle() that was serialising
    // 100% of CPU/GPU work.  Combined with vkCmdUpdateBuffer-based UBO
    // writes, this allows true double-buffered CPU/GPU parallelism.
    // ========================================================================
    if (m_vkCore) {
        m_vkCore->WaitForCurrentFrame();
        m_vkCore->TickDeletionQueue();
        // Tell the swapchain to skip the redundant fence wait in AcquireNextImage
        m_vkCore->GetSwapchain().MarkFenceAlreadyWaited();
    }
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [2] after WaitForCurrentFrame (GPU fence)
    }
    m_gui->BuildFrame();
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [3] after GUI::BuildFrame (ImGui → Python panels)
    }

    // Prepare scene rendering data (collect + cull + sort) AFTER GUI processing
    // so we always operate on the current scene state.
    SceneRenderBridge &bridge = SceneRenderBridge::Instance();
    bridge.PrepareFrame();
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [4] after PrepareFrame (CollectRenderables)
    }

    // Update camera from scene system (uses PrepareFrame results)
    bridge.UpdateCameraData(m_cameraPos, m_cameraLookAt, m_cameraUp);

    // ========================================================================
    // Deferred MSAA update: if the Python pipeline requested a different
    // MSAA sample count (via RenderGraphDescription.msaaSamples), apply it
    // before this frame's render.  This avoids changing MSAA inside the
    // render pass execution (which would invalidate in-flight resources).
    // ========================================================================
    if (m_sceneRenderGraph) {
        int requested = m_sceneRenderGraph->GetRequestedMsaaSamples();
        if (requested > 0 && requested != GetMsaaSamples()) {
            SetMsaaSamples(requested);
            // Skip the rest of this frame – all pipelines and render
            // graphs were destroyed/rebuilt.  Rendering with stale
            // resources would crash.  The next DrawFrame() will pick
            // up the fully rebuilt state.
            return;
        }
    }

    // Render scene via Python SRP render pipeline
    if (m_renderPipeline) {
        const bool sceneViewActive =
            (m_sceneRenderTarget && m_sceneRenderTarget->GetWidth() > 1 && m_sceneRenderTarget->GetHeight() > 1);

        EditorGizmosContext gizmoCtx;
        if (m_editorGizmos) {
            gizmoCtx.gizmos = m_editorGizmos.get();
            gizmoCtx.editorTools = m_editorTools.get();
            gizmoCtx.componentGizmos = m_componentGizmos.get();
            gizmoCtx.gizmoMaterial = MaterialManager::Instance().GetGizmoMaterial();
            gizmoCtx.gridMaterial = MaterialManager::Instance().GetGridMaterial();
            gizmoCtx.editorToolsMaterial = MaterialManager::Instance().GetEditorToolsMaterial();
            gizmoCtx.componentGizmosMaterial = MaterialManager::Instance().GetComponentGizmosMaterial();
            gizmoCtx.componentGizmoIconMaterial = MaterialManager::Instance().GetComponentGizmoIconMaterial();
            gizmoCtx.selectedObjectId = m_selectedObjectId;
            gizmoCtx.activeScene = SceneManager::Instance().GetActiveScene();
            gizmoCtx.cameraPos = glm::vec3(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);
        }

        // ---- Scene View: always use editor camera ----
        if (sceneViewActive) {
            ScriptableRenderContext ctx(m_vkCore.get(), m_sceneRenderGraph.get(), gizmoCtx);
            if (m_transientResourcePool) {
                ctx.SetTransientResourcePool(m_transientResourcePool.get());
            }
            Camera *editorCam = SceneManager::Instance().GetEditorCameraController().GetCamera();

            std::vector<Camera *> cameras;
            if (editorCam) {
                cameras.push_back(editorCam);
            }

            m_renderPipeline->Render(ctx, cameras);
        }

        // ---- Game View: use scene main camera (if enabled) ----
        if (m_gameCameraEnabled && m_gameRenderTarget && m_gameRenderTarget->IsReady() && m_gameRenderGraph) {
            Camera *gameCam = FindGameCamera();

            if (gameCam) {
                // Set aspect ratio BEFORE SetupCameraProperties() so the
                // projection matrix snapshot matches the render target size.
                if (m_gameRenderTarget->GetHeight() > 0) {
                    float aspect = static_cast<float>(m_gameRenderTarget->GetWidth()) /
                                   static_cast<float>(m_gameRenderTarget->GetHeight());
                    gameCam->SetAspectRatio(aspect);
                    gameCam->SetScreenDimensions(m_gameRenderTarget->GetWidth(), m_gameRenderTarget->GetHeight());
                }

                // Game camera: NO gizmos, NO grid, NO outline
                ScriptableRenderContext gameCtx(m_vkCore.get(), m_gameRenderGraph.get());
                if (m_transientResourcePool) {
                    gameCtx.SetTransientResourcePool(m_transientResourcePool.get());
                }

                std::vector<Camera *> gameCameras;
                gameCameras.push_back(gameCam);

                m_renderPipeline->Render(gameCtx, gameCameras);
            }
        }
    } else {
        INFLOG_ERROR("No render pipeline set — scene will not be rendered. "
                     "Call engine.set_render_pipeline(DefaultRenderPipelineAsset()) to activate rendering.");
    }
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [5] after RenderPipeline::Render (Python SRP)
    }

    // Cleanup unused per-object buffers using the union of all graphs' draw calls.
    // This runs once per frame AFTER all pipeline renders to prevent one graph's
    // cleanup from removing buffers needed by another graph.
    {
        std::vector<DrawCall> allDrawCalls;
        if (m_sceneRenderGraph && m_sceneRenderGraph->HasCachedDrawCalls()) {
            const auto &sceneDC = m_sceneRenderGraph->GetCachedDrawCalls();
            allDrawCalls.insert(allDrawCalls.end(), sceneDC.begin(), sceneDC.end());
        }
        if (m_gameCameraEnabled && m_gameRenderGraph && m_gameRenderGraph->HasCachedDrawCalls()) {
            const auto &gameDC = m_gameRenderGraph->GetCachedDrawCalls();
            allDrawCalls.insert(allDrawCalls.end(), gameDC.begin(), gameDC.end());
        }
        if (!allDrawCalls.empty()) {
            m_vkCore->CleanupUnusedBuffers(allDrawCalls);
        }
    }

    // Collect lights from scene and update lighting UBO staging data.
    // The actual GPU write happens inline in RecordCommandBuffer via
    // vkCmdUpdateBuffer (no CPU→GPU race).
    UpdateSceneLighting();

    // Lazy-init and update outline renderer
    if (m_outlineRenderer && m_sceneRenderTarget && m_vkCore) {
        m_outlineRenderer->Initialize(m_vkCore.get(), m_sceneRenderTarget.get());
        m_outlineRenderer->SetOutlineObjectId(m_selectedObjectId);
    }

    // Render frame with scene camera
    m_vkCore->DrawFrame(m_cameraPos, m_cameraLookAt, m_cameraUp);
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [6] after VkCore::DrawFrame (GPU submit + present)
    }

    // Post-frame updates
    SceneManager::Instance().LateUpdate(m_deltaTime);
    SceneManager::Instance().EndFrame();

    // ========================================================================
    // Frame Profiler output: print every 120 frames
    // ========================================================================
    if constexpr (INFENGINE_FRAME_PROFILE) {
        _fp.stamp(); // [7] end of frame
        ++_fpCounter;
        if (_fpCounter % 120 == 0) {
            INFLOG_WARN("[FrameProfile] total=", _fp.ms(0, 7),
                        "ms | "
                        "SceneUpdate=",
                        _fp.ms(0, 1),
                        "ms | "
                        "GPUFence=",
                        _fp.ms(1, 2),
                        "ms | "
                        "GUI(ImGui+Py)=",
                        _fp.ms(2, 3),
                        "ms | "
                        "PrepareFrame=",
                        _fp.ms(3, 4),
                        "ms | "
                        "RenderPipeline(Py)=",
                        _fp.ms(4, 5),
                        "ms | "
                        "VkDrawFrame=",
                        _fp.ms(5, 6),
                        "ms | "
                        "LateUpdate=",
                        _fp.ms(6, 7), "ms");
        }
    }
}

void InfRenderer::SetThreads(size_t graphicsThreads, size_t presentThreads, size_t transferThreads,
                             size_t computeThreads)
{
    if (m_vkCore) {
        m_vkCore->SetThreads(graphicsThreads, presentThreads, transferThreads, computeThreads);
    }
}

void InfRenderer::LoadShader(const char *name, const std::vector<char> &code, const char *type)
{
    m_vkCore->LoadShader(name, code, type);
}

bool InfRenderer::HasShader(const std::string &name, const std::string &type) const
{
    if (!m_vkCore)
        return false;
    return m_vkCore->HasShader(name, type);
}

bool InfRenderer::GetUserEvent()
{
    return m_view->GetUserEvent();
}

void InfRenderer::ShowWindow()
{
    m_view->Show();
}

void InfRenderer::HideWindow()
{
    m_view->Hide();
}

void InfRenderer::SetWindowIcon(const std::string &iconPath)
{
    if (m_view) {
        m_view->SetWindowIcon(iconPath);
    }
}

bool InfRenderer::IsCloseRequested() const
{
    return m_view && m_view->IsCloseRequested();
}

void InfRenderer::ConfirmClose()
{
    if (m_view) {
        m_view->ConfirmClose();
    }
}

void InfRenderer::CancelClose()
{
    if (m_view) {
        m_view->CancelClose();
    }
}

void InfRenderer::SetGUIFont(const char *fontPath, float fontSize)
{
    if (m_gui) {
        m_gui->SetGUIFont(fontPath, fontSize);
    } else {
        INFLOG_ERROR("InfGUI is not initialized.");
    }
}

void InfRenderer::RegisterGUIRenderable(const char *name, std::shared_ptr<InfGUIRenderable> renderable)
{
    if (m_gui) {
        m_gui->Register(name, renderable);
    } else {
        INFLOG_ERROR("InfGUI is not initialized.");
    }
}

void InfRenderer::UnregisterGUIRenderable(const char *name)
{
    if (m_gui) {
        m_gui->Unregister(name);
    } else {
        INFLOG_ERROR("InfGUI is not initialized.");
    }
}

uint64_t InfRenderer::UploadTextureForImGui(const std::string &name, const unsigned char *pixels, int width, int height)
{
    if (m_gui) {
        return m_gui->UploadTextureForImGui(name, pixels, width, height);
    } else {
        INFLOG_ERROR("InfGUI is not initialized.");
        return 0;
    }
}

void InfRenderer::RemoveImGuiTexture(const std::string &name)
{
    if (m_gui) {
        m_gui->RemoveImGuiTexture(name);
    } else {
        INFLOG_ERROR("InfGUI is not initialized.");
    }
}

bool InfRenderer::HasImGuiTexture(const std::string &name) const
{
    if (m_gui) {
        return m_gui->HasImGuiTexture(name);
    }
    return false;
}

uint64_t InfRenderer::GetImGuiTextureId(const std::string &name) const
{
    if (m_gui) {
        return m_gui->GetImGuiTextureId(name);
    }
    return 0;
}

ResourcePreviewManager *InfRenderer::GetResourcePreviewManager()
{
    if (m_gui) {
        return &m_gui->GetResourcePreviewManager();
    }
    return nullptr;
}

void InfRenderer::SetLogLevel(LogLevel level)
{
    INFLOG_SET_LEVEL(level);
}

void InfRenderer::InitializeDefaultScene()
{
    // Initialize editor gizmos
    m_editorGizmos = std::make_unique<EditorGizmos>();

    // Initialize editor tools (translate/rotate/scale handles)
    m_editorTools = std::make_unique<EditorTools>();

    // Initialize component gizmos buffer (Python-driven)
    m_componentGizmos = std::make_unique<GizmosDrawCallBuffer>();

    // Pass gizmos reference to VkCore for rendering
    if (m_vkCore) {
        m_vkCore->SetEditorGizmos(m_editorGizmos.get());
    }

    // Create a default scene
    Scene *scene = SceneManager::Instance().CreateScene("Default Scene");

    // Create a directional light (sun)
    GameObject *lightObj = scene->CreateGameObject("Directional Light");
    lightObj->GetTransform()->SetEulerAngles(glm::vec3(-50.0f, -30.0f, 0.0f)); // Angled downward (degrees)
    Light *light = lightObj->AddComponent<Light>();
    light->SetLightType(LightType::Directional);
    light->SetColor(glm::vec3(1.0f, 0.95f, 0.9f)); // Warm white sunlight
    light->SetIntensity(1.0f);
    light->SetShadows(LightShadows::Hard); // Enable hard shadows by default

    // Create a cube game object with MeshRenderer
    GameObject *cube = scene->CreateGameObject("Cube");
    cube->GetTransform()->SetPosition(glm::vec3(0.0f, 0.5f, 0.0f)); // Raise above ground plane

    // Add MeshRenderer with cube mesh data
    MeshRenderer *renderer = cube->AddComponent<MeshRenderer>();
    renderer->SetMesh(
        std::vector<Vertex>(PrimitiveMeshes::GetCubeVertices().begin(), PrimitiveMeshes::GetCubeVertices().end()),
        std::vector<uint32_t>(PrimitiveMeshes::GetCubeIndices().begin(), PrimitiveMeshes::GetCubeIndices().end()));

    // Create a ground plane to receive shadows
    GameObject *ground = scene->CreateGameObject("Ground");
    ground->GetTransform()->SetPosition(glm::vec3(0.0f, -0.5f, 0.0f));
    ground->GetTransform()->SetScale(glm::vec3(20.0f, 0.01f, 20.0f)); // Thin, wide plane
    MeshRenderer *groundRenderer = ground->AddComponent<MeshRenderer>();
    groundRenderer->SetMesh(
        std::vector<Vertex>(PrimitiveMeshes::GetCubeVertices().begin(), PrimitiveMeshes::GetCubeVertices().end()),
        std::vector<uint32_t>(PrimitiveMeshes::GetCubeIndices().begin(), PrimitiveMeshes::GetCubeIndices().end()));

    // Setup editor camera
    EditorCameraController &editorCam = SceneManager::Instance().GetEditorCameraController();
    editorCam.Reset();

    // Position camera to see the cube - use FocusOn to keep yaw/pitch in sync
    editorCam.FocusOn(glm::vec3(0.0f, 0.0f, 0.0f), 5.0f);

    // Initialize last frame time
    m_lastFrameTime = std::chrono::high_resolution_clock::now();

    INFLOG_INFO("Default scene initialized with cube, directional light, and gizmos");
}

void InfRenderer::UpdateSceneLighting()
{
    if (!m_vkCore)
        return;

    Scene *activeScene = SceneManager::Instance().GetActiveScene();
    if (!activeScene)
        return;

    // Get camera position for light sorting
    glm::vec3 cameraPos(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);

    // Collect lights from scene into the light collector
    SceneLightCollector &collector = m_vkCore->GetLightCollector();
    collector.CollectLights(activeScene, cameraPos);

    // Compute shadow VP from the first shadow-casting directional light.
    // Must happen BEFORE UpdateLightingUBO which uploads lightVP to GPU.
    collector.ComputeShadowVP(activeScene, cameraPos, 4096.0f);

    // Build shader-compatible UBO and upload to GPU
    m_vkCore->UpdateLightingUBO(cameraPos);
}

uint64_t InfRenderer::GetSceneTextureId() const
{
    if (m_sceneRenderTarget && m_sceneRenderTarget->IsReady()) {
        return m_sceneRenderTarget->GetImGuiTextureId();
    }
    return 0;
}

void InfRenderer::ResizeSceneRenderTarget(uint32_t width, uint32_t height)
{
    if (m_sceneRenderTarget && width > 0 && height > 0) {
        m_sceneRenderTarget->Resize(width, height);
        // Update aspect ratio for projection matrix calculation
        if (m_vkCore) {
            m_vkCore->SetSceneRenderTargetSize(width, height);
        }
        // Recreate outline framebuffers to match new dimensions
        if (m_outlineRenderer) {
            m_outlineRenderer->OnResize(width, height);
        }

        // Update scene camera aspect ratio
        SceneRenderBridge::Instance().OnWindowResize(width, height);
    }
}

void InfRenderer::SetShowGrid(bool show)
{
    if (m_editorGizmos) {
        m_editorGizmos->SetShowGrid(show);
        // Grid visibility change takes effect on next frame via SRP Submit()
    }
}

bool InfRenderer::IsShowGrid() const
{
    return m_editorGizmos ? m_editorGizmos->IsShowGrid() : false;
}

EditorGizmos &InfRenderer::GetEditorGizmos()
{
    if (!m_editorGizmos) {
        m_editorGizmos = std::make_unique<EditorGizmos>();
    }
    return *m_editorGizmos;
}

EditorTools *InfRenderer::GetEditorTools()
{
    return m_editorTools.get();
}

GizmosDrawCallBuffer *InfRenderer::GetGizmosDrawCallBuffer()
{
    return m_componentGizmos.get();
}

bool InfRenderer::RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material)
{
    INFLOG_DEBUG("RefreshMaterialPipeline called");

    if (!material) {
        INFLOG_ERROR("RefreshMaterialPipeline: material is null");
        return false;
    }

    INFLOG_DEBUG("RefreshMaterialPipeline: material name = ", material->GetName());

    if (!m_vkCore) {
        INFLOG_ERROR("RefreshMaterialPipeline: VkCore is null");
        return false;
    }

    // Get shader paths from material
    const std::string &vertPath = material->GetVertexShaderPath();
    const std::string &fragPath = material->GetFragmentShaderPath();

    INFLOG_DEBUG("RefreshMaterialPipeline: vertPath='", vertPath, "', fragPath='", fragPath, "'");

    if (vertPath.empty() || fragPath.empty()) {
        INFLOG_ERROR("RefreshMaterialPipeline: shader paths are empty");
        return false;
    }

    // Load and compile shaders, then update pipeline
    return m_vkCore->RefreshMaterialPipeline(material, vertPath, fragPath);
}

void InfRenderer::InvalidateShaderCache(const std::string &shaderId)
{
    INFLOG_DEBUG("InvalidateShaderCache called: ", shaderId);

    if (!m_vkCore) {
        INFLOG_ERROR("InvalidateShaderCache: VkCore is null");
        return;
    }

    m_vkCore->InvalidateShaderCache(shaderId);
}

bool InfRenderer::RefreshMaterialsUsingShader(const std::string &shaderId)
{
    INFLOG_DEBUG("RefreshMaterialsUsingShader called: ", shaderId);

    if (!m_vkCore) {
        INFLOG_ERROR("RefreshMaterialsUsingShader: VkCore is null");
        return false;
    }

    // Get all materials from MaterialManager
    auto &materialManager = MaterialManager::Instance();
    auto materials = materialManager.GetAllMaterials();

    bool anyRefreshed = false;
    for (auto &material : materials) {
        if (!material)
            continue;

        // Check if this material uses the shader
        const std::string &vertPath = material->GetVertexShaderPath();
        const std::string &fragPath = material->GetFragmentShaderPath();

        INFLOG_DEBUG("RefreshMaterialsUsingShader: checking material '", material->GetName(), "' with vertPath='",
                     vertPath, "' fragPath='", fragPath, "' against shaderId='", shaderId, "'");

        // Check for match: exact match OR shader name matches
        bool matchesVert = vertPath == shaderId;
        bool matchesFrag = fragPath == shaderId;

        // Also check if shaderId is a filename that matches the end of the path
        if (!matchesVert && !vertPath.empty()) {
            size_t lastSlash = vertPath.find_last_of("/\\");
            std::string vertFileName = (lastSlash != std::string::npos) ? vertPath.substr(lastSlash + 1) : vertPath;
            // Remove extension for comparison
            size_t dotPos = vertFileName.find_last_of('.');
            if (dotPos != std::string::npos) {
                vertFileName = vertFileName.substr(0, dotPos);
            }
            matchesVert = vertFileName == shaderId;
        }

        if (!matchesFrag && !fragPath.empty()) {
            size_t lastSlash = fragPath.find_last_of("/\\");
            std::string fragFileName = (lastSlash != std::string::npos) ? fragPath.substr(lastSlash + 1) : fragPath;
            // Remove extension for comparison
            size_t dotPos = fragFileName.find_last_of('.');
            if (dotPos != std::string::npos) {
                fragFileName = fragFileName.substr(0, dotPos);
            }
            matchesFrag = fragFileName == shaderId;
        }

        if (matchesVert || matchesFrag) {
            INFLOG_DEBUG("RefreshMaterialsUsingShader: refreshing material '", material->GetName(), "'");
            if (RefreshMaterialPipeline(material)) {
                anyRefreshed = true;
            }
        }
    }

    INFLOG_INFO("RefreshMaterialsUsingShader: refreshed materials using shader '", shaderId, "'");
    return anyRefreshed;
}

SceneRenderGraph *InfRenderer::GetSceneRenderGraph()
{
    return m_sceneRenderGraph.get();
}

std::shared_ptr<InfMaterial> InfRenderer::GetFirstMeshRendererMaterial()
{
    Scene *activeScene = SceneManager::Instance().GetActiveScene();
    if (!activeScene) {
        return nullptr;
    }

    std::vector<GameObject *> allObjects = activeScene->GetAllObjects();
    for (GameObject *obj : allObjects) {
        if (!obj->IsActiveInHierarchy()) {
            continue;
        }

        MeshRenderer *renderer = obj->GetComponent<MeshRenderer>();
        if (!renderer || !renderer->IsEnabled()) {
            continue;
        }

        // Return the first valid material found
        auto material = renderer->GetEffectiveMaterial();
        if (material) {
            return material;
        }
    }

    return nullptr;
}

void InfRenderer::SetRenderPipeline(std::shared_ptr<RenderPipelineCallback> pipeline)
{
    // Dispose the old pipeline if replacing
    if (m_renderPipeline) {
        m_renderPipeline->Dispose();
    }
    m_renderPipeline = std::move(pipeline);

    if (m_renderPipeline) {
        INFLOG_INFO("Render pipeline set (Python SRP path active)");
    } else {
        INFLOG_INFO("Render pipeline cleared (C++ default path active)");
    }
}

// ============================================================================
// Game Camera Render Target
// ============================================================================

Camera *InfRenderer::FindGameCamera()
{
    Scene *activeScene = SceneManager::Instance().GetActiveScene();
    if (!activeScene)
        return nullptr;

    Camera *editorCam = SceneManager::Instance().GetEditorCameraController().GetCamera();
    return activeScene->FindGameCamera(editorCam);
}

uint64_t InfRenderer::GetGameTextureId() const
{
    if (m_gameRenderTarget && m_gameRenderTarget->IsReady()) {
        Scene *activeScene = SceneManager::Instance().GetActiveScene();
        if (!activeScene)
            return 0;

        // Fast path: cached main camera is already known
        if (activeScene->GetMainCamera())
            return m_gameRenderTarget->GetImGuiTextureId();

        // Slow path: main camera hasn't been cached yet (first frame after
        // a Camera component was added).  Do a lightweight const-safe scan
        // for any active, enabled Camera that is NOT the editor camera to
        // avoid a one-frame "No Camera" flicker.
        Camera *editorCam = SceneManager::Instance().GetEditorCameraController().GetCamera();
        auto cameraObjects = activeScene->FindObjectsWithComponent<Camera>();
        for (auto *obj : cameraObjects) {
            if (!obj->IsActiveInHierarchy())
                continue;
            Camera *c = obj->GetComponent<Camera>();
            if (c && c->IsEnabled() && c != editorCam)
                return m_gameRenderTarget->GetImGuiTextureId();
        }
        return 0;
    }
    return 0;
}

void InfRenderer::ResizeGameRenderTarget(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return;
    }

    // Lazy-initialize game render target on first resize request
    if (!m_gameRenderTarget) {
        INFLOG_INFO("Game render target: lazy-initializing (", width, "x", height, ")");
        m_gameRenderTarget = std::make_unique<SceneRenderTarget>(m_vkCore.get());
        m_gameRenderTarget->Initialize(width, height);

        // Create a dedicated SceneRenderGraph for game camera
        m_gameRenderGraph = std::make_unique<SceneRenderGraph>();
        m_gameRenderGraph->Initialize(m_vkCore.get(), m_gameRenderTarget.get());

        // Create the screen UI renderer for GPU-based 2D UI in the game render graph
        if (!m_screenUIRenderer) {
            m_screenUIRenderer = std::make_unique<InfScreenUIRenderer>();
            m_screenUIRenderer->Initialize(m_vkCore->GetDevice(), m_vkCore->GetDeviceContext().GetVmaAllocator(),
                                           m_gameRenderTarget->GetColorFormat(),
                                           m_gameRenderTarget->GetMsaaSampleCount());
        }
        m_gameRenderGraph->SetScreenUIRenderer(m_screenUIRenderer.get());

        // Pre-discover game camera so GetGameTextureId() returns a valid
        // texture on the very first frame (avoids "No Camera" flicker).
        FindGameCamera();

        return;
    }

    if (m_gameRenderTarget && (width != m_gameRenderTarget->GetWidth() || height != m_gameRenderTarget->GetHeight())) {
        m_gameRenderTarget->Resize(width, height);
        if (m_gameRenderGraph) {
            m_gameRenderGraph->OnResize(width, height);
        }
    }
}

void InfRenderer::SetGameCameraEnabled(bool enabled)
{
    m_gameCameraEnabled = enabled;
    if (enabled) {
        INFLOG_DEBUG("Game camera rendering enabled");
    } else {
        INFLOG_DEBUG("Game camera rendering disabled");
    }
}

InfScreenUIRenderer *InfRenderer::GetScreenUIRenderer()
{
    return m_screenUIRenderer.get();
}

// ============================================================================
// MSAA Configuration
// ============================================================================

static VkSampleCountFlagBits IntToSampleCount(int samples)
{
    switch (samples) {
    case 1:
        return VK_SAMPLE_COUNT_1_BIT;
    case 2:
        return VK_SAMPLE_COUNT_2_BIT;
    case 4:
        return VK_SAMPLE_COUNT_4_BIT;
    case 8:
        return VK_SAMPLE_COUNT_8_BIT;
    default:
        INFLOG_WARN("Invalid MSAA sample count ", samples, ", clamping to 4");
        return VK_SAMPLE_COUNT_4_BIT;
    }
}

void InfRenderer::SetMsaaSamples(int samples)
{
    VkSampleCountFlagBits vkSamples = IntToSampleCount(samples);

    // Check if already set
    if (m_sceneRenderTarget && m_sceneRenderTarget->GetMsaaSampleCount() == vkSamples) {
        return; // No change
    }

    INFLOG_INFO("SetMsaaSamples: changing to ", samples, "x");

    // Must drain GPU before destroying Vulkan resources
    if (m_vkCore) {
        m_vkCore->GetDeviceContext().WaitIdle();
    }

    // 1. Recreate scene render target with new sample count
    if (m_sceneRenderTarget) {
        uint32_t w = m_sceneRenderTarget->GetWidth();
        uint32_t h = m_sceneRenderTarget->GetHeight();
        m_sceneRenderTarget->SetMsaaSampleCount(vkSamples);
        m_sceneRenderTarget->Cleanup();
        m_sceneRenderTarget->Initialize(w, h);

        // Force render graph rebuild to pick up new MSAA resources
        // (OnResize is a no-op when dimensions haven't changed)
        if (m_sceneRenderGraph) {
            m_sceneRenderGraph->MarkDirty();
        }
    }

    // 2. Recreate game render target with new sample count
    if (m_gameRenderTarget) {
        uint32_t w = m_gameRenderTarget->GetWidth();
        uint32_t h = m_gameRenderTarget->GetHeight();
        m_gameRenderTarget->SetMsaaSampleCount(vkSamples);
        m_gameRenderTarget->Cleanup();
        m_gameRenderTarget->Initialize(w, h);

        // Force render graph rebuild to pick up new MSAA resources
        if (m_gameRenderGraph) {
            m_gameRenderGraph->MarkDirty();
        }

        // Reinitialize ScreenUIRenderer with new sample count
        if (m_screenUIRenderer) {
            m_screenUIRenderer = std::make_unique<InfScreenUIRenderer>();
            m_screenUIRenderer->Initialize(m_vkCore->GetDevice(), m_vkCore->GetDeviceContext().GetVmaAllocator(),
                                           m_gameRenderTarget->GetColorFormat(),
                                           m_gameRenderTarget->GetMsaaSampleCount());
            if (m_gameRenderGraph) {
                m_gameRenderGraph->SetScreenUIRenderer(m_screenUIRenderer.get());
            }
        }
    }

    // 3. Reinitialize material pipeline manager with new MSAA setting
    if (m_vkCore) {
        m_vkCore->ReinitializeMaterialPipelines(vkSamples);
    }

    // 4. Reset outline renderer — it caches framebuffers and image views
    //    from the old SceneRenderTarget.  Without cleanup it would use
    //    destroyed Vulkan handles on the next frame → crash.
    if (m_outlineRenderer) {
        m_outlineRenderer->Cleanup();
    }

    INFLOG_INFO("SetMsaaSamples: complete (", samples, "x)");
}

int InfRenderer::GetMsaaSamples() const
{
    if (m_sceneRenderTarget) {
        return static_cast<int>(m_sceneRenderTarget->GetMsaaSampleCount());
    }
    return 4; // default
}

} // namespace infengine