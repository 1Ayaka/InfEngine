#include "InfEngine.h"
// Explicit includes for types now only forward-declared in InfRenderer.h
#include <function/renderer/EditorTools.h>
#include <function/renderer/GizmosDrawCallBuffer.h>
#include <function/renderer/SceneRenderGraph.h>
#include <function/renderer/ScriptableRenderContext.h>
#include <function/renderer/gui/InfGUIContext.h>
#include <function/renderer/gui/InfScreenUIRenderer.h>
#include <function/scene/EditorCameraController.h>
#include <glm/glm.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

using namespace infengine;
namespace py = pybind11;

namespace infengine
{
void RegisterGUIBindings(py::module_ &m);
void RegisterVec2fBindings(py::module_ &m);
void RegisterVec3fBindings(py::module_ &m);
void RegisterVec4fBindings(py::module_ &m);
void RegisterResourceBindings(py::module_ &m);
void RegisterSceneBindings(py::module_ &m);
void RegisterAssetDatabaseBindings(py::module_ &m);
void RegisterRenderGraphBindings(py::module_ &m);
void RegisterRenderPipelineBindings(py::module_ &m);
void RegisterCommandBufferBindings(py::module_ &m);
void RegisterTagLayerBindings(py::module_ &m);
void RegisterInputBindings(py::module_ &m);
void RegisterPhysicsBindings(py::module_ &m);
void RegisterAudioBindings(py::module_ &m);
} // namespace infengine

PYBIND11_MODULE(_InfEngine, m)
{
    m.doc() = "Python bindings for InfEngine";

    // ---- Editor gizmo axis IDs (exposed so Python can identify gizmo picks) ----
    m.attr("GIZMO_X_AXIS_ID") = EditorTools::X_AXIS_ID;
    m.attr("GIZMO_Y_AXIS_ID") = EditorTools::Y_AXIS_ID;
    m.attr("GIZMO_Z_AXIS_ID") = EditorTools::Z_AXIS_ID;

    py::enum_<LogLevel>(m, "LogLevel")
        .value("Debug", LogLevel::LOG_DEBUG)
        .value("Info", LogLevel::LOG_INFO)
        .value("Warn", LogLevel::LOG_WARN)
        .value("Error", LogLevel::LOG_ERROR)
        .value("Fatal", LogLevel::LOG_FATAL)
        .export_values();

    // ---- EditorCamera (property-based camera access) ----
    py::class_<EditorCameraController>(m, "EditorCamera",
                                       "Editor camera controller with property-based access.\n"
                                       "Access via engine.editor_camera.")
        .def_property(
            "fov",
            [](EditorCameraController &self) -> float {
                auto *cam = self.GetCamera();
                return cam ? cam->GetFieldOfView() : 60.0f;
            },
            [](EditorCameraController &self, float v) {
                auto *cam = self.GetCamera();
                if (cam)
                    cam->SetFieldOfView(v);
            },
            "Vertical field of view in degrees")
        .def_property(
            "near_clip",
            [](EditorCameraController &self) -> float {
                auto *cam = self.GetCamera();
                return cam ? cam->GetNearClip() : 0.01f;
            },
            [](EditorCameraController &self, float v) {
                auto *cam = self.GetCamera();
                if (cam)
                    cam->SetNearClip(v);
            },
            "Near clipping distance")
        .def_property(
            "far_clip",
            [](EditorCameraController &self) -> float {
                auto *cam = self.GetCamera();
                return cam ? cam->GetFarClip() : 1000.0f;
            },
            [](EditorCameraController &self, float v) {
                auto *cam = self.GetCamera();
                if (cam)
                    cam->SetFarClip(v);
            },
            "Far clipping distance")
        .def_property_readonly(
            "position",
            [](EditorCameraController &self) -> glm::vec3 {
                auto *cam = self.GetCamera();
                if (cam && cam->GetGameObject()) {
                    return cam->GetGameObject()->GetTransform()->GetPosition();
                }
                return glm::vec3(0.0f);
            },
            "Camera position as vec3f")
        .def_property_readonly(
            "rotation",
            [](EditorCameraController &self) -> py::tuple {
                // TODO: expose yaw/pitch from EditorCameraController internals
                return py::make_tuple(0.0f, 0.0f);
            },
            "Camera rotation as (yaw, pitch) tuple")
        .def("reset", &EditorCameraController::Reset, "Reset camera to default position and orientation")
        .def(
            "focus_on",
            [](EditorCameraController &self, float x, float y, float z, float distance) {
                self.FocusOn(glm::vec3(x, y, z), distance);
            },
            py::arg("x"), py::arg("y"), py::arg("z"), py::arg("distance") = 10.0f,
            "Focus camera on a world-space point")
        .def_readwrite("rotation_speed", &EditorCameraController::rotationSpeed, "Mouse rotation sensitivity")
        .def_readwrite("pan_speed", &EditorCameraController::panSpeed, "Middle-mouse pan sensitivity")
        .def_readwrite("zoom_speed", &EditorCameraController::zoomSpeed, "Scroll wheel zoom sensitivity")
        .def_readwrite("move_speed", &EditorCameraController::moveSpeed, "WASD movement speed")
        .def_readwrite("move_speed_boost", &EditorCameraController::moveSpeedBoost, "Shift speed multiplier");

    // ========================================================================
    // ScreenUIList enum and InfScreenUIRenderer bindings
    // ========================================================================
    py::enum_<ScreenUIList>(m, "ScreenUIList")
        .value("Camera", ScreenUIList::Camera)
        .value("Overlay", ScreenUIList::Overlay);

    py::class_<InfScreenUIRenderer>(m, "InfScreenUIRenderer")
        .def("begin_frame", &InfScreenUIRenderer::BeginFrame, py::arg("width"), py::arg("height"),
             "Reset draw lists for a new frame")
        .def("add_filled_rect", &InfScreenUIRenderer::AddFilledRect, py::arg("list"), py::arg("min_x"),
             py::arg("min_y"), py::arg("max_x"), py::arg("max_y"), py::arg("r") = 1.0f, py::arg("g") = 1.0f,
             py::arg("b") = 1.0f, py::arg("a") = 1.0f, py::arg("rounding") = 0.0f,
             "Add a filled rectangle to the specified draw list")
        .def("add_text", &InfScreenUIRenderer::AddText, py::arg("list"), py::arg("min_x"), py::arg("min_y"),
             py::arg("max_x"), py::arg("max_y"), py::arg("text"), py::arg("r") = 1.0f, py::arg("g") = 1.0f,
             py::arg("b") = 1.0f, py::arg("a") = 1.0f, py::arg("align_x") = 0.5f, py::arg("align_y") = 0.5f,
             py::arg("font_size") = 0.0f, "Add text within a bounding box to the specified draw list")
        .def("has_commands", &InfScreenUIRenderer::HasCommands, py::arg("list"),
             "Check if the specified draw list has any draw commands");

    py::class_<InfEngine>(m, "InfEngine")
        .def(py::init<std::string>(), py::arg("dll_path"))
        .def("init_renderer", &InfEngine::InitRenderer)
        .def("set_gui_font", &InfEngine::SetGUIFont)
        .def("run", &InfEngine::Run)
        .def("set_log_level", &InfEngine::SetLogLevel)
        .def("register_gui_renderable", &InfEngine::RegisterGUIRenderable, py::arg("name"), py::arg("renderable"))
        .def("unregister_gui_renderable", &InfEngine::UnregisterGUIRenderable, py::arg("name"))
        .def("exit", &InfEngine::Exit, "Exit the InfEngine application")
        .def("cleanup", &InfEngine::Cleanup, "Destroy renderer and release all GPU resources")
        .def("is_close_requested", &InfEngine::IsCloseRequested,
             "True when the user clicked the window close button but Python has not yet confirmed")
        .def("confirm_close", &InfEngine::ConfirmClose,
             "Actually close the engine (call after save dialogs are handled)")
        .def("cancel_close", &InfEngine::CancelClose,
             "Cancel a pending close request (user chose Cancel in save dialog)")
        .def("show", &InfEngine::ShowWindow, "Show the InfEngine window")
        .def("hide", &InfEngine::HideWindow, "Hide the InfEngine window")
        .def("set_window_icon", &InfEngine::SetWindowIcon, py::arg("icon_path"), "Set the window icon from a PNG file")
        .def("modify_resources", &InfEngine::ModifyResources, py::arg("file_path"))
        .def("delete_resources", &InfEngine::DeleteResources, py::arg("file_path"))
        .def("move_resources", &InfEngine::MoveResources, py::arg("old_file_path"), py::arg("new_file_path"))
        .def("reload_shader", &InfEngine::ReloadShader, py::arg("shader_path"),
             "Reload a shader file and refresh materials using it")
        .def("get_file_manager", &InfEngine::GetFileManager, py::return_value_policy::reference,
             "Get the file manager instance for direct resource operations")
        .def("get_asset_database", &InfEngine::GetAssetDatabase, py::return_value_policy::reference,
             "Get the asset database instance")
        .def("upload_texture_for_imgui", &InfEngine::UploadTextureForImGui, py::arg("name"), py::arg("pixels"),
             py::arg("width"), py::arg("height"), "Upload texture data for ImGui display, returns texture ID")
        .def("remove_imgui_texture", &InfEngine::RemoveImGuiTexture, py::arg("name"),
             "Remove a previously uploaded ImGui texture")
        .def("has_imgui_texture", &InfEngine::HasImGuiTexture, py::arg("name"),
             "Check if an ImGui texture with the given name exists")
        .def("get_imgui_texture_id", &InfEngine::GetImGuiTextureId, py::arg("name"),
             "Get texture ID for an already uploaded texture")
        .def("get_resource_preview_manager", &InfEngine::GetResourcePreviewManager, py::return_value_policy::reference,
             "Get the resource preview manager for file previews")
        // ========================================================================
        // Editor Camera (property-based object access — preferred API)
        // ========================================================================
        .def_property_readonly("editor_camera", &InfEngine::GetEditorCamera, py::return_value_policy::reference,
                               "Get the editor camera controller (EditorCamera object with property access)")
        // ========================================================================
        // Scene Camera Control API - for Scene View with Unity-style controls
        // ========================================================================
        .def("process_scene_view_input", &InfEngine::ProcessSceneViewInput, py::arg("delta_time"),
             py::arg("right_mouse_down"), py::arg("middle_mouse_down"), py::arg("mouse_delta_x"),
             py::arg("mouse_delta_y"), py::arg("scroll_delta"), py::arg("key_w"), py::arg("key_a"), py::arg("key_s"),
             py::arg("key_d"), py::arg("key_q"), py::arg("key_e"), py::arg("key_shift"),
             "Process scene view input for editor camera control")
        .def(
            "get_editor_camera_position",
            [](InfEngine &self) -> py::tuple {
                float x, y, z;
                self.GetEditorCameraPosition(&x, &y, &z);
                return py::make_tuple(py::float_(x), py::float_(y), py::float_(z));
            },
            "Get editor camera position as (x, y, z) tuple")
        .def(
            "get_editor_camera_rotation",
            [](InfEngine &self) -> py::tuple {
                float yaw, pitch;
                self.GetEditorCameraRotation(&yaw, &pitch);
                return py::make_tuple(py::float_(yaw), py::float_(pitch));
            },
            "Get editor camera rotation as (yaw, pitch) tuple")
        .def("reset_editor_camera", &InfEngine::ResetEditorCamera, "Reset editor camera to default position")
        .def("focus_editor_camera_on", &InfEngine::FocusEditorCameraOn, py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("distance") = 10.0f, "Focus editor camera on a point")
        .def(
            "get_editor_camera_focus_point",
            [](InfEngine &self) -> py::tuple {
                float x, y, z;
                self.GetEditorCameraFocusPoint(&x, &y, &z);
                return py::make_tuple(py::float_(x), py::float_(y), py::float_(z));
            },
            "Get editor camera focus point as (x, y, z) tuple")
        .def("get_editor_camera_focus_distance", &InfEngine::GetEditorCameraFocusDistance,
             "Get editor camera focus distance")
        .def("restore_editor_camera_state", &InfEngine::RestoreEditorCameraState, py::arg("pos_x"), py::arg("pos_y"),
             py::arg("pos_z"), py::arg("focus_x"), py::arg("focus_y"), py::arg("focus_z"), py::arg("focus_dist"),
             py::arg("yaw"), py::arg("pitch"), "Restore full editor camera state")
        // Camera settings
        .def("get_editor_camera_fov", &InfEngine::GetEditorCameraFov, "Get editor camera field of view (degrees)")
        .def("set_editor_camera_fov", &InfEngine::SetEditorCameraFov, py::arg("fov"),
             "Set editor camera field of view (degrees)")
        .def("get_editor_camera_near_clip", &InfEngine::GetEditorCameraNearClip, "Get editor camera near clip distance")
        .def("set_editor_camera_near_clip", &InfEngine::SetEditorCameraNearClip, py::arg("near_clip"),
             "Set editor camera near clip distance")
        .def("get_editor_camera_far_clip", &InfEngine::GetEditorCameraFarClip, "Get editor camera far clip distance")
        .def("set_editor_camera_far_clip", &InfEngine::SetEditorCameraFarClip, py::arg("far_clip"),
             "Set editor camera far clip distance")
        .def(
            "editor_world_to_screen_point",
            [](InfEngine &self, float x, float y, float z) -> glm::vec2 {
                auto *editorCamCtl = self.GetEditorCamera();
                if (!editorCamCtl)
                    return glm::vec2(0.0f);
                auto *camera = editorCamCtl->GetCamera();
                if (!camera)
                    return glm::vec2(0.0f);
                return camera->WorldToScreenPoint(glm::vec3(x, y, z));
            },
            py::arg("x"), py::arg("y"), py::arg("z"),
            "Project world position into current Scene View render target coordinates")
        // ========================================================================
        // Scene Render Target API - for offscreen scene rendering to ImGui
        // ========================================================================
        .def("get_scene_texture_id", &InfEngine::GetSceneTextureId,
             "Get scene render target texture ID for ImGui display")
        .def("resize_scene_render_target", &InfEngine::ResizeSceneRenderTarget, py::arg("width"), py::arg("height"),
             "Resize the scene render target to match viewport size")
        // ========================================================================
        // Game Camera Render Target API - for Game View panel
        // ========================================================================
        .def("get_game_texture_id", &InfEngine::GetGameTextureId, "Get game render target texture ID for ImGui display")
        .def("resize_game_render_target", &InfEngine::ResizeGameRenderTarget, py::arg("width"), py::arg("height"),
             "Resize the game render target (lazy-initializes on first call)")
        .def("set_game_camera_enabled", &InfEngine::SetGameCameraEnabled, py::arg("enabled"),
             "Enable/disable game camera rendering")
        .def("is_game_camera_enabled", &InfEngine::IsGameCameraEnabled, "Check if game camera rendering is enabled")
        .def("get_screen_ui_renderer", &InfEngine::GetScreenUIRenderer, py::return_value_policy::reference,
             "Get the screen UI renderer for GPU-based 2D screen-space UI (returns None before game RT init)")
        // ========================================================================
        // MSAA Configuration
        // ========================================================================
        .def("set_msaa_samples", &InfEngine::SetMsaaSamples, py::arg("samples"),
             "Set MSAA sample count (1=off, 2, 4, 8) for both scene and game render targets")
        .def("get_msaa_samples", &InfEngine::GetMsaaSamples, "Get current MSAA sample count (1=off)")
        // ========================================================================
        // Scene Picking API - for editor selection
        // ========================================================================
        .def("pick_scene_object_id", &InfEngine::PickSceneObjectId, py::arg("screen_x"), py::arg("screen_y"),
             py::arg("viewport_width"), py::arg("viewport_height"),
             "Pick a scene object or gizmo arrow by screen-space coordinates and return its ID (0 if none)")
        .def("pick_scene_object_ids", &InfEngine::PickSceneObjectIds, py::arg("screen_x"), py::arg("screen_y"),
             py::arg("viewport_width"), py::arg("viewport_height"),
             "Pick ordered scene object candidate IDs from screen coordinates")
        .def("set_editor_tool_highlight", &InfEngine::SetEditorToolHighlight, py::arg("axis"),
             "Set the highlighted gizmo axis. 0=None, 1=X, 2=Y, 3=Z.")
        .def("set_editor_tool_mode", &InfEngine::SetEditorToolMode, py::arg("mode"),
             "Set the active tool mode. 0=None, 1=Translate, 2=Rotate, 3=Scale.")
        .def("get_editor_tool_mode", &InfEngine::GetEditorToolMode,
             "Get the active tool mode. 0=None, 1=Translate, 2=Rotate, 3=Scale.")
        .def("set_editor_tool_local_mode", &InfEngine::SetEditorToolLocalMode, py::arg("local"),
             "Enable/disable local coordinate mode for editor tools (gizmo aligns to object rotation)")
        .def("screen_to_world_ray", &InfEngine::ScreenToWorldRay, py::arg("screen_x"), py::arg("screen_y"),
             py::arg("viewport_width"), py::arg("viewport_height"),
             "Build a world-space ray from screen coords. Returns (ox,oy,oz, dx,dy,dz).")
        // ========================================================================
        // Editor Gizmos API - for toggling visual aids in scene view
        // ========================================================================
        .def("set_show_grid", &InfEngine::SetShowGrid, py::arg("show"), "Set visibility of ground grid")
        .def("is_show_grid", &InfEngine::IsShowGrid, "Get visibility of ground grid")
        .def("set_selection_outline", &InfEngine::SetSelectionOutline, py::arg("object_id"),
             "Set selection outline for a game object (Unity-style orange wireframe). Pass 0 to clear.")
        .def("get_selected_object_id", &InfEngine::GetSelectedObjectId,
             "Get the currently selected object ID (0 if none).")
        .def("clear_selection_outline", &InfEngine::ClearSelectionOutline, "Clear selection outline")
        // ========================================================================
        // Component Gizmos API — upload per-component gizmo geometry from Python
        // ========================================================================
        .def(
            "upload_component_gizmos",
            [](InfEngine &self, py::buffer vertices, int64_t vertexCount, py::buffer indices, py::buffer descriptors,
               int64_t descriptorCount) {
                GizmosDrawCallBuffer *buf = self.GetGizmosDrawCallBuffer();
                if (!buf)
                    return;

                // vertices: flat float buffer, stride 6 (pos3 + color3) per vertex
                constexpr int64_t kVertStride = 6;
                py::buffer_info vInfo = vertices.request();
                const float *vPtr = static_cast<const float *>(vInfo.ptr);

                std::vector<Vertex> verts;
                verts.reserve(static_cast<size_t>(vertexCount));
                for (int64_t i = 0; i < vertexCount; ++i) {
                    const float *b = vPtr + i * kVertStride;
                    Vertex v;
                    v.pos = glm::vec3(b[0], b[1], b[2]);
                    v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    v.color = glm::vec3(b[3], b[4], b[5]);
                    v.texCoord = glm::vec2(0.0f);
                    verts.push_back(v);
                }

                // indices: flat uint32 buffer
                py::buffer_info iInfo = indices.request();
                const uint32_t *iPtr = static_cast<const uint32_t *>(iInfo.ptr);
                std::vector<uint32_t> idx(iPtr, iPtr + iInfo.size);

                // descriptors: flat float buffer, stride 18 (indexStart + indexCount + mat4x4)
                constexpr int64_t kDescStride = 18;
                py::buffer_info dInfo = descriptors.request();
                const float *dPtr = static_cast<const float *>(dInfo.ptr);

                std::vector<GizmosDrawCallBuffer::DrawDescriptor> descs;
                descs.reserve(static_cast<size_t>(descriptorCount));
                for (int64_t i = 0; i < descriptorCount; ++i) {
                    const float *b = dPtr + i * kDescStride;
                    GizmosDrawCallBuffer::DrawDescriptor d;
                    d.indexStart = static_cast<uint32_t>(b[0]);
                    d.indexCount = static_cast<uint32_t>(b[1]);
                    for (int j = 0; j < 16; ++j) {
                        d.worldMatrix[j] = b[2 + j];
                    }
                    descs.push_back(d);
                }

                buf->SetData(std::move(verts), std::move(idx), std::move(descs));
            },
            py::arg("vertices"), py::arg("vertex_count"), py::arg("indices"), py::arg("descriptors"),
            py::arg("descriptor_count"),
            "Upload per-component gizmo geometry via buffer protocol (no numpy). "
            "vertices: flat float32 (N*6), indices: flat uint32, descriptors: flat float32 (D*18)")
        .def(
            "clear_component_gizmos",
            [](InfEngine &self) {
                GizmosDrawCallBuffer *buf = self.GetGizmosDrawCallBuffer();
                if (buf)
                    buf->Clear();
            },
            "Clear all component gizmo geometry")
        .def(
            "upload_component_gizmo_icons",
            [](InfEngine &self, py::buffer positions, py::buffer objectIds, int64_t iconCount) {
                GizmosDrawCallBuffer *buf = self.GetGizmosDrawCallBuffer();
                if (!buf || iconCount <= 0)
                    return;

                // positions: flat float buffer, stride 6 (pos3 + color3) per icon
                constexpr int64_t kPosStride = 6;
                py::buffer_info posInfo = positions.request();
                const float *posPtr = static_cast<const float *>(posInfo.ptr);

                // objectIds: flat uint32 buffer, stride 2 (lo + hi) per icon
                py::buffer_info idInfo = objectIds.request();
                const uint32_t *idPtr = static_cast<const uint32_t *>(idInfo.ptr);

                std::vector<GizmosDrawCallBuffer::IconEntry> entries;
                entries.reserve(static_cast<size_t>(iconCount));
                for (int64_t i = 0; i < iconCount; ++i) {
                    const float *p = posPtr + i * kPosStride;
                    const uint32_t *id = idPtr + i * 2;

                    GizmosDrawCallBuffer::IconEntry entry;
                    entry.position = glm::vec3(p[0], p[1], p[2]);
                    entry.color = glm::vec3(p[3], p[4], p[5]);
                    entry.objectId = (static_cast<uint64_t>(id[1]) << 32) | static_cast<uint64_t>(id[0]);
                    entries.push_back(entry);
                }

                buf->SetIconData(std::move(entries));
            },
            py::arg("positions"), py::arg("object_ids"), py::arg("icon_count"),
            "Upload component gizmo icon entries via buffer protocol (no numpy). "
            "positions: flat float32 (N*6: x,y,z,r,g,b), object_ids: flat uint32 (N*2: lo,hi)")
        .def(
            "clear_component_gizmo_icons",
            [](InfEngine &self) {
                GizmosDrawCallBuffer *buf = self.GetGizmosDrawCallBuffer();
                if (buf)
                    buf->ClearIcons();
            },
            "Clear all component gizmo icon data")
        // ========================================================================
        // Material Pipeline API - for refreshing material shaders at runtime
        // ========================================================================
        .def("refresh_material_pipeline", &InfEngine::RefreshMaterialPipeline, py::arg("material"),
             "Refresh a material's rendering pipeline by reloading its shaders")
        // ========================================================================
        // Render Pipeline API - for custom Python render pipelines (SRP)
        // ========================================================================
        .def(
            "set_render_pipeline",
            [](InfEngine &self, py::object pipeline) {
                if (pipeline.is_none()) {
                    self.SetRenderPipeline(nullptr);
                } else {
                    self.SetRenderPipeline(pipeline.cast<std::shared_ptr<RenderPipelineCallback>>());
                }
            },
            py::arg("pipeline"),
            "Set a custom RenderPipelineCallback to control rendering from Python. Pass None to revert to default.")
        // ========================================================================
        // Render Graph API - for pass output access and ML integration
        // ========================================================================
        .def("get_scene_render_graph", &InfEngine::GetSceneRenderGraph, py::return_value_policy::reference,
             "Get the scene render graph for pass configuration and output access")
        .def("get_readable_pass_names", &InfEngine::GetReadablePassNames,
             "Get list of render pass names that have readback enabled")
        .def(
            "read_pass_color_pixels",
            [](InfEngine &self, const std::string &passName) -> py::object {
                std::vector<uint8_t> data;
                if (!self.ReadPassColorPixels(passName, data)) {
                    return py::none();
                }

                uint32_t width = 0, height = 0;
                if (!self.GetPassOutputSize(passName, width, height)) {
                    return py::none();
                }

                // Return as NumPy array with shape (height, width, 4) - RGBA
                return py::array_t<uint8_t>(
                    {static_cast<py::ssize_t>(height), static_cast<py::ssize_t>(width), static_cast<py::ssize_t>(4)},
                    data.data());
            },
            py::arg("pass_name"), "Read color pixels from a render pass as NumPy array (height, width, 4) RGBA uint8")
        .def(
            "read_pass_depth_pixels",
            [](InfEngine &self, const std::string &passName) -> py::object {
                std::vector<float> data;
                if (!self.ReadPassDepthPixels(passName, data)) {
                    return py::none();
                }

                uint32_t width = 0, height = 0;
                if (!self.GetPassOutputSize(passName, width, height)) {
                    return py::none();
                }

                // Return as NumPy array with shape (height, width) - float32
                return py::array_t<float>({static_cast<py::ssize_t>(height), static_cast<py::ssize_t>(width)},
                                          data.data());
            },
            py::arg("pass_name"), "Read depth pixels from a render pass as NumPy array (height, width) float32")
        .def(
            "get_pass_output_size",
            [](InfEngine &self, const std::string &passName) -> py::tuple {
                uint32_t width = 0, height = 0;
                self.GetPassOutputSize(passName, width, height);
                return py::make_tuple(py::int_(width), py::int_(height));
            },
            py::arg("pass_name"), "Get render pass output dimensions as (width, height) tuple")
        .def("get_pass_texture_id", &InfEngine::GetPassTextureId, py::arg("pass_name"),
             "Get render pass output texture ID for ImGui display");

    // Register all binding modules
    RegisterGUIBindings(m);
    RegisterVec2fBindings(m);
    RegisterVec3fBindings(m);
    RegisterVec4fBindings(m);
    RegisterResourceBindings(m);
    RegisterAssetDatabaseBindings(m);
    RegisterSceneBindings(m);
    RegisterTagLayerBindings(m);
    RegisterRenderGraphBindings(m);
    RegisterCommandBufferBindings(m); // Must come before RenderPipeline (provides VkFormat, RenderTargetHandle, etc.)
    RegisterRenderPipelineBindings(m);
    RegisterInputBindings(m);
    RegisterPhysicsBindings(m);
    RegisterAudioBindings(m);
}
