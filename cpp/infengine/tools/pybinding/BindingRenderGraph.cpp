/**
 * @file BindingRenderGraph.cpp
 * @brief Python bindings for RenderGraph and pass output access
 *
 * Provides Python-side control over the render graph, allowing:
 * - Configuration of render passes
 * - Reading pass output pixels to NumPy arrays
 * - Getting pass texture IDs for ImGui display
 * - Phase 2: Python-driven RenderGraph topology definition
 */

#include "InfEngine.h"
#include "function/renderer/RenderGraphDescription.h"
#include "function/renderer/SceneRenderGraph.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace infengine
{

void RegisterRenderGraphBindings(py::module_ &m)
{
    // NOTE: ScenePassType and ScenePassConfig are intentionally not exposed to
    // Python. They are internal C++ implementation details of SceneRenderGraph.
    // Python uses GraphPassDesc / RenderGraphDescription (via the RenderGraph
    // builder in InfEngine.rendergraph) as its exclusive pass-definition API.

    // ========================================================================
    // Phase 2: Python-Driven RenderGraph Topology Types
    // ========================================================================

    // GraphPassActionType enum
    py::enum_<GraphPassActionType>(m, "GraphPassActionType", "The rendering action a graph pass should perform")
        .value("NONE", GraphPassActionType::None, "No rendering (resource-only pass)")
        .value("DRAW_RENDERERS", GraphPassActionType::DrawRenderers, "Draw scene renderers filtered by queue range")
        .value("DRAW_SKYBOX", GraphPassActionType::DrawSkybox, "Draw the procedural skybox")
        .value("COMPUTE", GraphPassActionType::Compute, "Dispatch a compute shader (no render pass)")
        .value("CUSTOM", GraphPassActionType::Custom, "Reserved for future Python callback support")
        .value("DRAW_SHADOW_CASTERS", GraphPassActionType::DrawShadowCasters,
               "Draw shadow casters into a depth-only shadow map")
        .value("DRAW_SCREEN_UI", GraphPassActionType::DrawScreenUI, "Draw screen-space UI (Camera or Overlay list)")
        .value("FULLSCREEN_QUAD", GraphPassActionType::FullscreenQuad,
               "Draw a fullscreen triangle with a named shader (post-process)")
        .export_values();

    // GraphTextureDesc struct
    py::class_<GraphTextureDesc>(m, "GraphTextureDesc", "Description of a texture resource in the Python-defined graph")
        .def(py::init<>())
        .def_readwrite("name", &GraphTextureDesc::name, "Unique resource name")
        .def_readwrite("format", &GraphTextureDesc::format, "Vulkan format")
        .def_readwrite("is_backbuffer", &GraphTextureDesc::isBackbuffer,
                       "If true, refers to the scene's main color target")
        .def_readwrite("is_depth", &GraphTextureDesc::isDepth, "If true, this is a depth/stencil texture")
        .def_readwrite("width", &GraphTextureDesc::width, "Custom width (0 = use scene target size)")
        .def_readwrite("height", &GraphTextureDesc::height, "Custom height (0 = use scene target size)")
        .def_readwrite("size_divisor", &GraphTextureDesc::sizeDivisor,
                       "Size divisor relative to scene target (>0: actual = scene / divisor)");

    // GraphPassDesc struct
    py::class_<GraphPassDesc>(m, "GraphPassDesc", "Description of a single render pass in the Python-defined graph")
        .def(py::init<>())
        .def_readwrite("name", &GraphPassDesc::name, "Pass name (must be unique)")
        .def_readwrite("read_textures", &GraphPassDesc::readTextures, "Names of textures this pass reads")
        .def_readwrite("write_colors", &GraphPassDesc::writeColors,
                       "MRT color outputs: list of (slot, texture_name) pairs")
        .def_readwrite("write_depth", &GraphPassDesc::writeDepth, "Name of depth output texture")
        .def_readwrite("clear_color", &GraphPassDesc::clearColor, "Whether to clear color buffer")
        .def_readwrite("clear_depth", &GraphPassDesc::clearDepth, "Whether to clear depth buffer")
        .def_readwrite("clear_color_r", &GraphPassDesc::clearColorR, "Clear color red")
        .def_readwrite("clear_color_g", &GraphPassDesc::clearColorG, "Clear color green")
        .def_readwrite("clear_color_b", &GraphPassDesc::clearColorB, "Clear color blue")
        .def_readwrite("clear_color_a", &GraphPassDesc::clearColorA, "Clear color alpha")
        .def_readwrite("clear_depth_value", &GraphPassDesc::clearDepthValue, "Depth clear value")
        .def_readwrite("action", &GraphPassDesc::action, "Render action type")
        .def_readwrite("queue_min", &GraphPassDesc::queueMin, "Minimum render queue (inclusive)")
        .def_readwrite("queue_max", &GraphPassDesc::queueMax, "Maximum render queue (inclusive)")
        .def_readwrite("sort_mode", &GraphPassDesc::sortMode, "Sort mode: 'front_to_back', 'back_to_front', 'none'")
        .def_readwrite("input_bindings", &GraphPassDesc::inputBindings,
                       "Shader input bindings: list of (sampler_name, texture_name) pairs")
        .def_readwrite("compute_shader_name", &GraphPassDesc::computeShaderName, "Compute shader name")
        .def_readwrite("dispatch_x", &GraphPassDesc::dispatchX, "Compute dispatch group count X")
        .def_readwrite("dispatch_y", &GraphPassDesc::dispatchY, "Compute dispatch group count Y")
        .def_readwrite("dispatch_z", &GraphPassDesc::dispatchZ, "Compute dispatch group count Z")
        .def_readwrite("light_index", &GraphPassDesc::lightIndex, "Shadow-casting light index (0 = first directional)")
        .def_readwrite("shadow_type", &GraphPassDesc::shadowType, "Shadow quality: 'hard', 'soft'")
        .def_readwrite("screen_ui_list", &GraphPassDesc::screenUIList,
                       "Screen UI list: 0 = Camera (before post-process), 1 = Overlay (after post-process)")
        .def_readwrite("shader_name", &GraphPassDesc::shaderName, "Shader id for FullscreenQuad action")
        .def_readwrite("push_constants", &GraphPassDesc::pushConstants,
                       "Push-constant values: list of (name, float) pairs");

    // RenderGraphDescription struct
    py::class_<RenderGraphDescription>(m, "RenderGraphDescription", "Complete render graph topology defined by Python")
        .def(py::init<>())
        .def_readwrite("name", &RenderGraphDescription::name, "Graph name for debugging")
        .def_readwrite("textures", &RenderGraphDescription::textures, "All texture resources")
        .def_readwrite("passes", &RenderGraphDescription::passes, "All passes in declaration order")
        .def_readwrite("output_texture", &RenderGraphDescription::outputTexture, "Name of the final output texture")
        .def_readwrite("msaa_samples", &RenderGraphDescription::msaaSamples,
                       "MSAA sample count (0=no change, 1=off, 2, 4, 8)");

    // SceneRenderGraph class
    py::class_<SceneRenderGraph>(m, "SceneRenderGraph")
        // Pass configuration
        .def("mark_dirty", &SceneRenderGraph::MarkDirty, "Force rebuild of the render graph on next frame")
        .def("mark_dirty", &SceneRenderGraph::MarkDirty, "Force rebuild of the render graph on next frame")
        // Phase 2: Python-driven render graph topology
        .def("apply_python_graph", &SceneRenderGraph::ApplyPythonGraph, py::arg("description"),
             "Apply a Python-defined render graph topology (Phase 2)")
        .def("has_python_graph", &SceneRenderGraph::HasPythonGraph, "Check if a Python graph topology has been applied")
        // Pass output access
        .def("get_readable_pass_names", &SceneRenderGraph::GetReadablePassNames,
             "Get list of pass names that have readback enabled")
        .def("get_pass_count", &SceneRenderGraph::GetPassCount, "Get number of configured passes")
        .def("get_debug_string", &SceneRenderGraph::GetDebugString, "Get debug visualization of the render graph")
        .def(
            "get_pass_output_size",
            [](SceneRenderGraph &self, const std::string &passName) -> py::tuple {
                uint32_t width = 0, height = 0;
                if (self.GetPassOutputSize(passName, width, height)) {
                    return py::make_tuple(py::int_(width), py::int_(height));
                }
                return py::make_tuple(py::int_(0), py::int_(0));
            },
            py::arg("pass_name"), "Get pass output dimensions as (width, height) tuple")
        .def("get_pass_texture_id", &SceneRenderGraph::GetPassTextureId, py::arg("pass_name"),
             "Get pass output texture ID for ImGui display")
        // Pixel readback with NumPy support
        .def(
            "read_pass_color_pixels",
            [](SceneRenderGraph &self, const std::string &passName) -> py::object {
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
            py::arg("pass_name"), "Read color pixels from a pass output as NumPy array (height, width, 4) RGBA uint8")
        .def(
            "read_pass_depth_pixels",
            [](SceneRenderGraph &self, const std::string &passName) -> py::object {
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
            py::arg("pass_name"), "Read depth pixels from a pass output as NumPy array (height, width) float32");

    // Add RenderGraph access methods to InfEngine
    // These are defined in BindingInfEngine.cpp but we document them here for clarity:
    // - get_scene_render_graph() -> SceneRenderGraph
    // - get_readable_pass_names() -> list[str]
    // - read_pass_color_pixels(pass_name) -> numpy.ndarray or None
    // - read_pass_depth_pixels(pass_name) -> numpy.ndarray or None
    // - get_pass_output_size(pass_name) -> tuple(width, height)
    // - get_pass_texture_id(pass_name) -> int
}

} // namespace infengine
