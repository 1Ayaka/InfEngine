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
#include <function/resources/AssetRegistry/AssetRegistry.h>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <function/scene/Camera.h>
#include <function/scene/Light.h>
#include <function/scene/LightingData.h>
#include <function/scene/MeshRenderer.h>
#include <function/scene/PrimitiveMeshes.h>
#include <function/scene/SceneManager.h>
#include <function/scene/SceneRenderer.h>
#include <iomanip>
#include <iostream>
#include <platform/window/InfView.h>
#include <sstream>

namespace infengine
{
InfRenderer::InfRenderer()
{
    m_vkCore = std::make_unique<InfVkCoreModular>();
    m_view = std::make_unique<InfView>();
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
                (m_sceneViewVisible && m_sceneRenderTarget && m_sceneRenderTarget->IsReady() &&
                 m_sceneRenderTarget->GetWidth() > 1 && m_sceneRenderTarget->GetHeight() > 1);

#if INFENGINE_FRAME_PROFILE
            using ExClock = std::chrono::high_resolution_clock;
            auto exT0 = ExClock::now();
#endif
            // ---- Scene View: editor camera VP is already in UBO from UpdateUniformBuffer ----
            if (sceneViewActive && m_sceneRenderGraph) {
                // Swap in scene-specific draw calls (includes gizmos)
                if (m_sceneRenderGraph->HasCachedDrawCalls()) {
                    m_vkCore->SetDrawCalls(&m_sceneRenderGraph->GetCachedDrawCalls());
                }
                // Set per-graph shadow descriptor (set 1) for multi-camera isolation
                m_vkCore->SetActiveShadowDescriptorSet(m_sceneRenderGraph->GetPerViewDescriptorSet());
                m_sceneRenderGraph->Execute(cmdBuf);
            }
#if INFENGINE_FRAME_PROFILE
            auto exT1 = ExClock::now();
#endif
            if (sceneViewActive && m_sceneRenderGraph) {
                m_sceneRenderGraph->ResolveSceneMsaa(cmdBuf);
            }
#if INFENGINE_FRAME_PROFILE
            auto exT2 = ExClock::now();
#endif

            // ---- Game View: use cached camera VP from SubmitCulling ----
            if (m_gameCameraEnabled && m_gameRenderGraph && m_gameRenderTarget && m_gameRenderTarget->IsReady()) {
                if (m_gameRenderGraph->HasCachedDrawCalls() && m_gameRenderGraph->HasCachedCameraVP()) {
                    // Swap in game-specific draw calls (no gizmos)
                    m_vkCore->SetDrawCalls(&m_gameRenderGraph->GetCachedDrawCalls());

                    // Update UBO with game camera VP matrices cached during
                    // SetupCameraProperties() → SubmitCulling() in the SRP pipeline.
                    m_vkCore->CmdUpdateUniformBuffer(cmdBuf, m_gameRenderGraph->GetCachedView(),
                                                     m_gameRenderGraph->GetCachedProj());

                    // Override lighting UBO camera position for the game view.
                    {
                        const glm::mat4 &gameView = m_gameRenderGraph->GetCachedView();
                        glm::mat4 invView = glm::inverse(gameView);
                        glm::vec3 gameCamPos(invView[3]);
                        m_vkCore->CmdUpdateLightingCameraPos(cmdBuf, gameCamPos);
                    }

                    // Patch shadow VP data for the game camera.
                    if (m_hasGameShadowData) {
                        m_vkCore->CmdUpdateShadowDataForCamera(cmdBuf, m_gameShadowVPs.data(), m_gameShadowCascadeCount,
                                                               m_gameShadowSplits.data(), m_gameShadowMapResolution);
                    }

                    // Set per-graph shadow descriptor (set 1) for game camera
                    m_vkCore->SetActiveShadowDescriptorSet(m_gameRenderGraph->GetPerViewDescriptorSet());
#if INFENGINE_FRAME_PROFILE
                    auto exTg0 = ExClock::now();
#endif
                    m_gameRenderGraph->Execute(cmdBuf);
#if INFENGINE_FRAME_PROFILE
                    auto exTg1 = ExClock::now();
#endif
                    m_gameRenderGraph->ResolveSceneMsaa(cmdBuf);
#if INFENGINE_FRAME_PROFILE
                    auto exTg2 = ExClock::now();
#endif

                    if (m_sceneRenderGraph && m_sceneRenderGraph->HasCachedCameraVP()) {
                        m_vkCore->CmdUpdateUniformBuffer(cmdBuf, m_sceneRenderGraph->GetCachedView(),
                                                         m_sceneRenderGraph->GetCachedProj());
                        const glm::mat4 &editorView = m_sceneRenderGraph->GetCachedView();
                        glm::mat4 invEditorView = glm::inverse(editorView);
                        glm::vec3 editorCamPos(invEditorView[3]);
                        m_vkCore->CmdUpdateLightingCameraPos(cmdBuf, editorCamPos);
                    }

                    if (m_sceneRenderGraph && m_sceneRenderGraph->HasCachedDrawCalls()) {
                        m_vkCore->SetDrawCalls(&m_sceneRenderGraph->GetCachedDrawCalls());
                    }

                    if (m_sceneRenderGraph) {
                        m_vkCore->SetActiveShadowDescriptorSet(m_sceneRenderGraph->GetPerViewDescriptorSet());
                    }
#if INFENGINE_FRAME_PROFILE
                    auto exTg3 = ExClock::now();

                    m_executorTiming.gameSetupMs += std::chrono::duration<double, std::milli>(exTg0 - exT2).count();
                    m_executorTiming.gameExecMs += std::chrono::duration<double, std::milli>(exTg1 - exTg0).count();
                    m_executorTiming.gameMsaaMs += std::chrono::duration<double, std::milli>(exTg2 - exTg1).count();
                    m_executorTiming.gameRestoreMs += std::chrono::duration<double, std::milli>(exTg3 - exTg2).count();
#endif
                }
            }

#if INFENGINE_FRAME_PROFILE
            m_executorTiming.sceneExecMs += std::chrono::duration<double, std::milli>(exT1 - exT0).count();
            m_executorTiming.sceneMsaaMs += std::chrono::duration<double, std::milli>(exT2 - exT1).count();
#endif
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

            if (m_outlineRenderer && m_outlineRenderer->HasActiveOutline()) {
                if (!m_outlineRenderer->RecordCommands(cmdBuf, drawCalls)) {
                    m_outlineRenderer->RecordNoOutlineBarrier(cmdBuf);
                }
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
    // Controlled by INFENGINE_FRAME_PROFILE in ProfileConfig.h (0 = off, 1 = on).
    // ========================================================================

#if INFENGINE_FRAME_PROFILE
    struct FrameProfiler
    {
        using Clock = std::chrono::high_resolution_clock;
        using TimePoint = Clock::time_point;
        TimePoint stamps[16];
        int idx = 0;
        void stamp()
        {
            if (idx < 16)
                stamps[idx++] = Clock::now();
        }
        float ms(int a, int b) const
        {
            return std::chrono::duration<float, std::milli>(stamps[b] - stamps[a]).count();
        }
    };
    FrameProfiler _fp;
    static int _fpCounter = 0;
    static double _fpAccum[12] = {};
    static SceneManager::FrameProfile _sceneAccum;
    static std::unordered_map<std::string, double> _guiAccum;
    _fp.stamp(); // [0] frame start
#endif

    // Window events
    m_view->ProcessEvent();
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [1] after input/event processing
#endif

    // Skip rendering while the window is minimized.
    // This avoids a deadlock in vkAcquireNextImageKHR when the
    // swapchain extent is zero.
    if (m_view->IsMinimized()) {
        SDL_Delay(16);
        return;
    }

    // Update scene system
    SceneManager::Instance().Update(m_deltaTime);

    // LateUpdate runs immediately after Update — before rendering — so that
    // camera-follow scripts see interpolated physics transforms and their
    // results are picked up by the same frame's render pass.  This matches
    // Unity's execution order: FixedUpdate → Update → LateUpdate → Render.
    SceneManager::Instance().LateUpdate(m_deltaTime);
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [2] after SceneManager::Update + LateUpdate
#endif

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
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [3] after WaitForCurrentFrame (GPU fence)
#endif

    // Run the pre-GUI callback (Python DeferredTaskRunner) BEFORE
    // BuildFrame so that scene-mutating deferred steps (e.g. scene
    // deserialize on play-mode Stop) complete before any ImGui panel
    // renders.  This prevents panels from accessing destroyed objects.
    if (m_preGuiCallback) {
        m_preGuiCallback();
    }

    m_gui->BuildFrame();
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [4] after GUI::BuildFrame (ImGui → Python panels)
#endif

    // Prepare scene rendering data (collect + cull + sort) AFTER GUI processing
    // so we always operate on the current scene state.
    SceneRenderBridge &bridge = SceneRenderBridge::Instance();
    bridge.PrepareFrame();
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [5] after PrepareFrame (CollectRenderables)
#endif

    // Update camera from scene system (uses PrepareFrame results)
    bridge.UpdateCameraData(m_cameraPos, m_cameraLookAt, m_cameraUp);

    if (m_sceneRenderGraph) {
        int requested = m_sceneRenderGraph->GetRequestedMsaaSamples();
        if (requested > 0 && requested != GetMsaaSamples()) {
            SetMsaaSamples(requested);
            return;
        }
    }

    // Render scene via Python SRP render pipeline
    const bool sceneViewActive =
        (m_sceneViewVisible && m_sceneRenderTarget && m_sceneRenderTarget->IsReady() &&
         m_sceneRenderTarget->GetWidth() > 1 && m_sceneRenderTarget->GetHeight() > 1);

    if (m_renderPipeline) {
        EditorGizmosContext gizmoCtx;
        if (m_editorGizmos) {
            gizmoCtx.gizmos = m_editorGizmos.get();
            gizmoCtx.editorTools = m_editorTools.get();
            gizmoCtx.componentGizmos = m_componentGizmos.get();
            auto &registry = AssetRegistry::Instance();
            gizmoCtx.gizmoMaterial = registry.GetBuiltinMaterial("GizmoMaterial");
            gizmoCtx.gridMaterial = registry.GetBuiltinMaterial("GridMaterial");
            gizmoCtx.editorToolsMaterial = registry.GetBuiltinMaterial("EditorToolsMaterial");
            gizmoCtx.componentGizmosMaterial = registry.GetBuiltinMaterial("ComponentGizmosMaterial");
            gizmoCtx.componentGizmoIconMaterial = registry.GetBuiltinMaterial("ComponentGizmoIconMaterial");
            gizmoCtx.cameraGizmoIconMaterial = registry.GetBuiltinMaterial("ComponentGizmoCameraIconMaterial");
            gizmoCtx.lightGizmoIconMaterial = registry.GetBuiltinMaterial("ComponentGizmoLightIconMaterial");
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
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [6] after RenderPipeline::Render (Python SRP)
#endif

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
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [7] after CleanupUnusedBuffers
#endif

    UpdateSceneLighting();
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [8] after UpdateSceneLighting
#endif

    // Lazy-init and update outline renderer
    if (sceneViewActive && m_outlineRenderer && m_sceneRenderTarget && m_vkCore) {
        m_outlineRenderer->Initialize(m_vkCore.get(), m_sceneRenderTarget.get());
        m_outlineRenderer->SetOutlineObjectId(m_selectedObjectId);
    } else if (m_outlineRenderer) {
        m_outlineRenderer->SetOutlineObjectId(0);
    }

    if (sceneViewActive && m_sceneRenderGraph) {
        m_sceneRenderGraph->EnsureGraphBuilt();
    }
    if (m_gameCameraEnabled && m_gameRenderGraph) {
        m_gameRenderGraph->EnsureGraphBuilt();
    }

    // ========================================================================
    // Stage engine globals UBO (time, screen, camera params) for this frame
    // ========================================================================
    {
        m_totalTime += m_deltaTime;
        m_smoothDeltaTime += (m_deltaTime - m_smoothDeltaTime) * 0.1f;
        ++m_frameCount;

        EngineGlobalsUBO globals{};

        // Time
        float t = m_totalTime;
        globals.time = glm::vec4(t, std::sin(t), std::cos(t), m_deltaTime);
        globals.sinTime = glm::vec4(std::sin(t / 20.0f), std::sin(t / 4.0f), std::sin(t * 2.0f), std::sin(t * 3.0f));
        globals.cosTime = glm::vec4(std::cos(t / 20.0f), std::cos(t / 4.0f), std::cos(t * 2.0f), std::cos(t * 3.0f));

        // Screen params (from scene render target or window)
        float w = 1.0f, h = 1.0f;
        if (m_sceneRenderTarget && m_sceneRenderTarget->GetWidth() > 0) {
            w = static_cast<float>(m_sceneRenderTarget->GetWidth());
            h = static_cast<float>(m_sceneRenderTarget->GetHeight());
        }
        globals.screenParams = glm::vec4(w, h, 1.0f / w, 1.0f / h);

        // Camera
        globals.worldSpaceCameraPos = glm::vec4(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2], 1.0f);

        float nearClip = 0.01f, farClip = 5000.0f;
        Camera *editorCam = SceneManager::Instance().GetEditorCameraController().GetCamera();
        if (editorCam) {
            nearClip = editorCam->GetNearClip();
            farClip = editorCam->GetFarClip();
        }
        globals.projectionParams = glm::vec4(nearClip, farClip, 1.0f / farClip, nearClip / farClip);

        // ZBuffer linearization helpers (reversed-Z compatible)
        float fn = farClip / nearClip;
        globals.zBufferParams = glm::vec4(1.0f - fn, fn, (1.0f - fn) / farClip, fn / farClip);

        // Frame
        float dt = m_deltaTime > 1e-6f ? m_deltaTime : 1e-6f;
        globals.frameParams = glm::vec4(static_cast<float>(m_frameCount), m_smoothDeltaTime, 1.0f / dt, 0.0f);

        m_vkCore->StageGlobals(globals);
    }
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [9] after Outline+GraphBuild+UBO staging
#endif

    // Render frame with scene camera
    m_vkCore->DrawFrame(m_cameraPos, m_cameraLookAt, m_cameraUp);
#if INFENGINE_FRAME_PROFILE
    _fp.stamp(); // [10] after VkCore::DrawFrame (GPU submit + present)
#endif

    SceneManager::Instance().EndFrame();

    // ========================================================================
    // Unified Frame Profiler output: print every 120 frames
    // ========================================================================
#if INFENGINE_FRAME_PROFILE
    {
        _fp.stamp();                    // [11] end of frame
        _fpAccum[0] += _fp.ms(0, 11);   // total frame
        _fpAccum[1] += _fp.ms(0, 1);    // Input
        _fpAccum[2] += _fp.ms(1, 2);    // Scene+Late
        _fpAccum[3] += _fp.ms(2, 3);    // GPUFence
        _fpAccum[4] += _fp.ms(3, 4);    // UI
        _fpAccum[5] += _fp.ms(4, 5);    // Prepare
        _fpAccum[6] += _fp.ms(5, 6);    // Render (Python SRP)
        _fpAccum[7] += _fp.ms(6, 7);    // CleanupBuffers
        _fpAccum[8] += _fp.ms(7, 8);    // Lighting
        _fpAccum[9] += _fp.ms(8, 9);    // Outline+Graph+UBO
        _fpAccum[10] += _fp.ms(9, 10);  // DrawFrame (submit+present)
        _fpAccum[11] += _fp.ms(10, 11); // End

        const auto &sceneProfile = SceneManager::Instance().GetLastFrameProfile();
        _sceneAccum.editorCameraMs += sceneProfile.editorCameraMs;
        _sceneAccum.editorUpdateMs += sceneProfile.editorUpdateMs;
        _sceneAccum.pendingStartsMs += sceneProfile.pendingStartsMs;
        _sceneAccum.syncExternalMovesMs += sceneProfile.syncExternalMovesMs;
        _sceneAccum.syncCollidersMs += sceneProfile.syncCollidersMs;
        _sceneAccum.fixedUpdateMs += sceneProfile.fixedUpdateMs;
        _sceneAccum.physicsStepMs += sceneProfile.physicsStepMs;
        _sceneAccum.physicsEventsMs += sceneProfile.physicsEventsMs;
        _sceneAccum.syncRigidbodiesMs += sceneProfile.syncRigidbodiesMs;
        _sceneAccum.interpolationMs += sceneProfile.interpolationMs;
        _sceneAccum.gameplayUpdateMs += sceneProfile.gameplayUpdateMs;
        _sceneAccum.lateUpdateMs += sceneProfile.lateUpdateMs;
        _sceneAccum.audioMs += sceneProfile.audioMs;
        _sceneAccum.endFrameMs += sceneProfile.endFrameMs;
        _sceneAccum.fixedSteps += sceneProfile.fixedSteps;

        const auto &panelTimes = m_gui->GetLastPanelTimesMs();
        for (const auto &name : m_gui->GetRenderableOrder()) {
            auto it = panelTimes.find(name);
            if (it != panelTimes.end()) {
                _guiAccum[name] += it->second;
            }
        }

        ++_fpCounter;
        if (_fpCounter % 120 == 0) {
            constexpr double kWindow = 120.0;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "[Profile] avg120 frame=" << (_fpAccum[0] / kWindow) << "ms"
                << " | Input=" << (_fpAccum[1] / kWindow) << "ms"
                << " | Scene+Late=" << (_fpAccum[2] / kWindow) << "ms"
                << " | GPUFence=" << (_fpAccum[3] / kWindow) << "ms"
                << " | UI=" << (_fpAccum[4] / kWindow) << "ms"
                << " | Prepare=" << (_fpAccum[5] / kWindow) << "ms"
                << " | Render=" << (_fpAccum[6] / kWindow) << "ms"
                << " | Cleanup=" << (_fpAccum[7] / kWindow) << "ms"
                << " | Lighting=" << (_fpAccum[8] / kWindow) << "ms"
                << " | Graph+UBO=" << (_fpAccum[9] / kWindow) << "ms"
                << " | Present=" << (_fpAccum[10] / kWindow) << "ms"
                << " | End=" << (_fpAccum[11] / kWindow) << "ms";

            // DrawFrame sub-timing breakdown
            if (m_vkCore) {
                double drawSub[infengine::InfVkCoreModular::kDrawSubSlots];
                int drawN = m_vkCore->GetDrawSubTimings(drawSub);
                if (drawN > 0) {
                    double n = static_cast<double>(drawN);
                    const auto rgProfile = infengine::vk::RenderGraph::GetExecuteProfileSnapshot();
                    const auto topPassProfiles = infengine::vk::RenderGraph::GetTopCallbackProfiles(6);
                    uint64_t filteredCalls = 0, filteredEligible = 0, filteredIssued = 0;
                    uint64_t shadowCalls = 0, shadowEligible = 0, shadowIssued = 0;
                    m_vkCore->GetDrawSubCounters(filteredCalls, filteredEligible, filteredIssued, shadowCalls,
                                                 shadowEligible, shadowIssued);
                    oss << "\n  DrawFrame: Acquire=" << (drawSub[0] / n) << "ms"
                        << " Record=" << (drawSub[1] / n) << "ms"
                        << " Submit=" << (drawSub[2] / n) << "ms"
                        << " Present=" << (drawSub[3] / n) << "ms"
                        << "\n    Record: UBO=" << (drawSub[4] / n) << "ms"
                        << " SceneGraph=" << (drawSub[5] / n) << "ms"
                        << " PostScene=" << (drawSub[6] / n) << "ms"
                        << " GUIGraph=" << (drawSub[7] / n) << "ms"
                        << "\n    Filtered: total=" << (drawSub[8] / n) << "ms"
                        << " filter=" << (drawSub[9] / n) << "ms"
                        << " sort=" << (drawSub[10] / n) << "ms"
                        << " draw=" << (drawSub[11] / n) << "ms"
                        << " calls/frame=" << (static_cast<double>(filteredCalls) / n) << " eligible/call="
                        << (filteredCalls ? static_cast<double>(filteredEligible) / static_cast<double>(filteredCalls)
                                          : 0.0)
                        << " issued/call="
                        << (filteredCalls ? static_cast<double>(filteredIssued) / static_cast<double>(filteredCalls)
                                          : 0.0)
                        << "\n    Shadow: total=" << (drawSub[12] / n) << "ms"
                        << " filter=" << (drawSub[13] / n) << "ms"
                        << " draw=" << (drawSub[14] / n) << "ms"
                        << " calls/frame=" << (static_cast<double>(shadowCalls) / n) << " eligible/call="
                        << (shadowCalls ? static_cast<double>(shadowEligible) / static_cast<double>(shadowCalls) : 0.0)
                        << " issued/call="
                        << (shadowCalls ? static_cast<double>(shadowIssued) / static_cast<double>(shadowCalls) : 0.0)
                        << "\n    RenderGraph: barrier="
                        << (rgProfile.executeCalls ? rgProfile.barrierMs / static_cast<double>(rgProfile.executeCalls)
                                                   : 0.0)
                        << "ms begin="
                        << (rgProfile.executeCalls ? rgProfile.beginPassMs / static_cast<double>(rgProfile.executeCalls)
                                                   : 0.0)
                        << "ms callback="
                        << (rgProfile.executeCalls ? rgProfile.callbackMs / static_cast<double>(rgProfile.executeCalls)
                                                   : 0.0)
                        << "ms end="
                        << (rgProfile.executeCalls ? rgProfile.endPassMs / static_cast<double>(rgProfile.executeCalls)
                                                   : 0.0)
                        << "ms passes/exec="
                        << (rgProfile.executeCalls
                                ? static_cast<double>(rgProfile.passCount) / static_cast<double>(rgProfile.executeCalls)
                                : 0.0)
                        << " gfx/exec="
                        << (rgProfile.executeCalls ? static_cast<double>(rgProfile.graphicsPassCount) /
                                                         static_cast<double>(rgProfile.executeCalls)
                                                   : 0.0)
                        << " barrierCalls/exec="
                        << (rgProfile.executeCalls ? static_cast<double>(rgProfile.barrierCallCount) /
                                                         static_cast<double>(rgProfile.executeCalls)
                                                   : 0.0);

                    if (!topPassProfiles.empty()) {
                        oss << "\n    CallbackTop:";
                        for (const auto &entry : topPassProfiles) {
                            oss << ' ' << entry.name << '=' << (drawN ? entry.totalMs / n : 0.0) << "ms" << '('
                                << entry.calls << ')';
                        }
                    }

                    // Executor sub-timing breakdown
                    oss << "\n    Executor: sceneExec=" << (m_executorTiming.sceneExecMs / n)
                        << "ms sceneMSAA=" << (m_executorTiming.sceneMsaaMs / n)
                        << "ms gameSetup=" << (m_executorTiming.gameSetupMs / n)
                        << "ms gameExec=" << (m_executorTiming.gameExecMs / n)
                        << "ms gameMSAA=" << (m_executorTiming.gameMsaaMs / n)
                        << "ms gameRestore=" << (m_executorTiming.gameRestoreMs / n) << "ms";
                }
            }

            oss << "\n  Scene: editorCam=" << (_sceneAccum.editorCameraMs / kWindow)
                << "ms editor=" << (_sceneAccum.editorUpdateMs / kWindow)
                << "ms pending=" << (_sceneAccum.pendingStartsMs / kWindow)
                << "ms syncExt=" << (_sceneAccum.syncExternalMovesMs / kWindow)
                << "ms syncCol=" << (_sceneAccum.syncCollidersMs / kWindow)
                << "ms fixed=" << (_sceneAccum.fixedUpdateMs / kWindow)
                << "ms physics=" << (_sceneAccum.physicsStepMs / kWindow)
                << "ms events=" << (_sceneAccum.physicsEventsMs / kWindow)
                << "ms syncRb=" << (_sceneAccum.syncRigidbodiesMs / kWindow)
                << "ms interp=" << (_sceneAccum.interpolationMs / kWindow)
                << "ms gameplay=" << (_sceneAccum.gameplayUpdateMs / kWindow)
                << "ms late=" << (_sceneAccum.lateUpdateMs / kWindow) << "ms audio=" << (_sceneAccum.audioMs / kWindow)
                << "ms end=" << (_sceneAccum.endFrameMs / kWindow)
                << "ms fixedSteps=" << (_sceneAccum.fixedSteps / kWindow);

            double guiTotal = 0.0;
            oss << "\n  UI:";
            for (const auto &name : m_gui->GetRenderableOrder()) {
                auto it = _guiAccum.find(name);
                if (it == _guiAccum.end()) {
                    continue;
                }
                const double avgMs = it->second / kWindow;
                guiTotal += avgMs;
                oss << ' ' << name << '=' << avgMs << "ms";
            }
            oss << " total=" << guiTotal << "ms";
            INFLOG_WARN(oss.str());
#if INFENGINE_FRAME_PROFILE_TERMINAL
            std::cerr << oss.str() << std::endl;
#endif

            for (double &value : _fpAccum) {
                value = 0.0;
            }
            _sceneAccum = {};
            _guiAccum.clear();
            if (m_vkCore) {
                m_vkCore->ResetDrawSubTimings();
            }
            infengine::vk::RenderGraph::ResetExecuteProfileSnapshot();
            m_executorTiming = {};
        }
    }
#endif
}

void InfRenderer::WaitForGpuIdle()
{
    if (!m_vkCore) {
        return;
    }

    if (m_sceneRenderGraph) {
        m_sceneRenderGraph->ClearCachedFrameState();
    }
    if (m_gameRenderGraph) {
        m_gameRenderGraph->ClearCachedFrameState();
    }

    m_hasGameShadowData = false;
    m_gameShadowCascadeCount = 0;
    m_gameShadowMapResolution = 0.0f;

    // Drop the currently staged draw list as well. If the upcoming frame skips
    // re-submitting one of the graphs (for example because no valid game camera
    // exists yet), VkCore must not keep drawing the previous scene's objects.
    m_vkCore->SetDrawCalls(nullptr);

    // Scene replacement destroys meshes/materials/textures immediately.
    // A full device drain avoids probabilistic crashes from old-scene resources
    // still being referenced by previously submitted command buffers.
    m_vkCore->GetDeviceContext().WaitIdle();
    m_vkCore->FlushDeletionQueue();
}

void InfRenderer::LoadShader(const char *name, const std::vector<char> &code, const char *type)
{
    m_vkCore->LoadShader(name, code, type);
}

void InfRenderer::StoreShaderRenderMeta(const std::string &shaderId, const std::string &cullMode,
                                        const std::string &depthWrite, const std::string &depthTest,
                                        const std::string &blend, int queue, const std::string &passTag,
                                        const std::string &stencil, const std::string &alphaClip)
{
    m_vkCore->StoreShaderRenderMeta(shaderId, cullMode, depthWrite, depthTest, blend, queue, passTag, stencil,
                                    alphaClip);
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

void InfRenderer::SetWindowFullscreen(bool fullscreen)
{
    if (m_view) {
        m_view->SetWindowFullscreen(fullscreen);
    }
}

void InfRenderer::SetWindowTitle(const std::string &title)
{
    if (m_view) {
        m_view->SetWindowTitle(title);
    }
}

void InfRenderer::SetWindowMaximized(bool maximized)
{
    if (m_view) {
        m_view->SetWindowMaximized(maximized);
    }
}

void InfRenderer::SetWindowResizable(bool resizable)
{
    if (m_view) {
        m_view->SetWindowResizable(resizable);
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

float InfRenderer::GetDisplayScale() const
{
    if (m_gui) {
        return m_gui->GetDisplayScale();
    }
    return 1.0f;
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
    // Use the editor camera for the scene view's shadow cascades.
    Camera *editorCam = SceneRenderBridge::Instance().GetEditorCamera();
    collector.ComputeShadowVP(activeScene, cameraPos, 4096.0f, editorCam);

    // Compute separate shadow VP for the game camera (if active).
    // This data is patched into the lighting UBO inline before the game
    // render graph executes, preventing shadow contamination from the
    // editor camera's frustum shape.
    m_hasGameShadowData = false;
    if (m_gameCameraEnabled) {
        Camera *gameCam = FindGameCamera();
        if (gameCam) {
            SceneLightCollector gameCollector;
            gameCollector.CollectLights(activeScene, cameraPos);
            gameCollector.ComputeShadowVP(activeScene, cameraPos, 4096.0f, gameCam);
            if (gameCollector.IsShadowEnabled()) {
                m_hasGameShadowData = true;
                m_gameShadowCascadeCount = gameCollector.GetShadowCascadeCount();
                m_gameShadowMapResolution = gameCollector.GetShadowMapResolution();
                const auto &splits = gameCollector.GetShadowCascadeSplits();
                for (uint32_t i = 0; i < 4; ++i) {
                    m_gameShadowVPs[i] = gameCollector.GetShadowLightVP(i);
                    m_gameShadowSplits[i] = splits[i];
                }
            }
        }
    }

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

    // Get shader names from material
    const std::string &vertName = material->GetVertShaderName();
    const std::string &fragName = material->GetFragShaderName();

    INFLOG_DEBUG("RefreshMaterialPipeline: vert='", vertName, "' frag='", fragName, "'");

    if (fragName.empty()) {
        INFLOG_ERROR("RefreshMaterialPipeline: fragment shader name is empty");
        return false;
    }

    // Load and compile shaders, then update pipeline
    return m_vkCore->RefreshMaterialPipeline(material, vertName, fragName);
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

void InfRenderer::InvalidateTextureCache(const std::string &texturePath)
{
    INFLOG_DEBUG("InvalidateTextureCache called: ", texturePath);

    if (!m_vkCore) {
        INFLOG_ERROR("InvalidateTextureCache: VkCore is null");
        return;
    }

    m_vkCore->InvalidateTextureCache(texturePath);
}

void InfRenderer::RemoveMaterialPipeline(const std::string &materialName)
{
    if (!m_vkCore) {
        INFLOG_ERROR("RemoveMaterialPipeline: VkCore is null");
        return;
    }
    m_vkCore->RemoveMaterialPipeline(materialName);
}

bool InfRenderer::RefreshMaterialsUsingShader(const std::string &shaderId)
{
    INFLOG_DEBUG("RefreshMaterialsUsingShader called: ", shaderId);

    if (!m_vkCore) {
        INFLOG_ERROR("RefreshMaterialsUsingShader: VkCore is null");
        return false;
    }

    // Get all materials from AssetRegistry
    auto &registry = AssetRegistry::Instance();
    auto materials = registry.GetAllMaterials();

    bool anyRefreshed = false;
    for (auto &material : materials) {
        if (!material)
            continue;

        // Check if this material uses the shader
        const std::string &vertName = material->GetVertShaderName();
        const std::string &fragName = material->GetFragShaderName();

        INFLOG_DEBUG("RefreshMaterialsUsingShader: checking material '", material->GetName(), "' with vert='", vertName,
                     "' frag='", fragName, "' against shaderId='", shaderId, "'");

        // Check for match: either vert or frag uses this shader
        auto matchesId = [&](const std::string &name) {
            if (name == shaderId)
                return true;
            if (name.empty())
                return false;
            size_t lastSlash = name.find_last_of("/\\");
            std::string fileName = (lastSlash != std::string::npos) ? name.substr(lastSlash + 1) : name;
            size_t dotPos = fileName.find_last_of('.');
            if (dotPos != std::string::npos)
                fileName = fileName.substr(0, dotPos);
            return fileName == shaderId;
        };
        bool matches = matchesId(vertName) || matchesId(fragName);

        if (matches) {
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
        // Match the current MSAA setting from the scene render target.
        // Without this, the game render target defaults to 4x MSAA even
        // when the engine has already switched to 1x, causing a layout
        // mismatch (COLOR_ATTACHMENT_OPTIMAL vs SHADER_READ_ONLY_OPTIMAL)
        // that triggers VK_ERROR_DEVICE_LOST.
        if (m_sceneRenderTarget) {
            m_gameRenderTarget->SetMsaaSampleCount(m_sceneRenderTarget->GetMsaaSampleCount());
        }
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

void InfRenderer::SetPresentMode(int mode)
{
    if (m_vkCore)
        m_vkCore->SetPresentMode(mode);
}

int InfRenderer::GetPresentMode() const
{
    if (m_vkCore)
        return m_vkCore->GetPresentMode();
    return 1; // MAILBOX default
}

} // namespace infengine