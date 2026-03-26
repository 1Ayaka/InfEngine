/**
 * @file InfScreenUIRenderer.cpp
 * @brief Implementation of GPU-based 2D screen-space UI renderer
 *
 * Uses ImGui's ImDrawList for command accumulation (provides text rendering
 * via font atlas for free) and renders with a standalone Vulkan pipeline
 * inside the scene render graph's MSAA render passes.
 *
 * The pipeline is a direct replica of ImGui's internal 2D pipeline:
 *   - Vertex format: ImDrawVert (pos, uv, col)
 *   - Push constants: vec2 scale + vec2 translate (orthographic projection)
 *   - Descriptor: single combined_image_sampler (font atlas)
 *   - Alpha blending, no depth test, no cull
 *
 * SPIR-V bytecode is identical to Dear ImGui's (MIT licensed).
 */

#include "InfScreenUIRenderer.h"
#include "InfTextLayout.h"
#include <cmath>
#include <core/log/InfLog.h>
#include <imgui_internal.h> // for ImGui::GetDrawListSharedData()

namespace infengine
{

namespace
{
float ResolveFontSize(float fontSize)
{
    return textlayout::ResolveFontSize(fontSize);
}

float ExtractHDRScale(float &r, float &g, float &b)
{
    const float maxRGB = std::max(r, std::max(g, b));
    if (maxRGB <= 1.0f) {
        return 1.0f;
    }

    r /= maxRGB;
    g /= maxRGB;
    b /= maxRGB;
    return maxRGB;
}

ImTextureID ToImTextureID(uint64_t textureId)
{
    if constexpr (std::is_pointer_v<ImTextureID>) {
        return (ImTextureID)(static_cast<uintptr_t>(textureId));
    }
    return static_cast<ImTextureID>(textureId);
}
} // namespace

// ============================================================================
// ImGui SPIR-V shader bytecode (from imgui_impl_vulkan.cpp, MIT license)
// ============================================================================

// clang-format off

// backends/vulkan/glsl_shader.vert — compiled with glslangValidator
static const uint32_t s_vertSpv[] = {
    0x07230203,0x00010000,0x00080001,0x0000002e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x000a000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x0000000f,0x00000015,
    0x0000001b,0x0000001c,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00030005,0x00000009,0x00000000,0x00050006,0x00000009,0x00000000,0x6f6c6f43,
    0x00000072,0x00040006,0x00000009,0x00000001,0x00005655,0x00030005,0x0000000b,0x0074754f,
    0x00040005,0x0000000f,0x6c6f4361,0x0000726f,0x00030005,0x00000015,0x00565561,0x00060005,
    0x00000019,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x00000019,0x00000000,
    0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000001b,0x00000000,0x00040005,0x0000001c,
    0x736f5061,0x00000000,0x00060005,0x0000001e,0x73755075,0x6e6f4368,0x6e617473,0x00000074,
    0x00050006,0x0000001e,0x00000000,0x61635375,0x0000656c,0x00060006,0x0000001e,0x00000001,
    0x61725475,0x616c736e,0x00006574,0x00030005,0x00000020,0x00006370,0x00040047,0x0000000b,
    0x0000001e,0x00000000,0x00040047,0x0000000f,0x0000001e,0x00000002,0x00040047,0x00000015,
    0x0000001e,0x00000001,0x00050048,0x00000019,0x00000000,0x0000000b,0x00000000,0x00030047,
    0x00000019,0x00000002,0x00040047,0x0000001c,0x0000001e,0x00000000,0x00050048,0x0000001e,
    0x00000000,0x00000023,0x00000000,0x00050048,0x0000001e,0x00000001,0x00000023,0x00000008,
    0x00030047,0x0000001e,0x00000002,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
    0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040017,
    0x00000008,0x00000006,0x00000002,0x0004001e,0x00000009,0x00000007,0x00000008,0x00040020,
    0x0000000a,0x00000003,0x00000009,0x0004003b,0x0000000a,0x0000000b,0x00000003,0x00040015,
    0x0000000c,0x00000020,0x00000001,0x0004002b,0x0000000c,0x0000000d,0x00000000,0x00040020,
    0x0000000e,0x00000001,0x00000007,0x0004003b,0x0000000e,0x0000000f,0x00000001,0x00040020,
    0x00000011,0x00000003,0x00000007,0x0004002b,0x0000000c,0x00000013,0x00000001,0x00040020,
    0x00000014,0x00000001,0x00000008,0x0004003b,0x00000014,0x00000015,0x00000001,0x00040020,
    0x00000017,0x00000003,0x00000008,0x0003001e,0x00000019,0x00000007,0x00040020,0x0000001a,
    0x00000003,0x00000019,0x0004003b,0x0000001a,0x0000001b,0x00000003,0x0004003b,0x00000014,
    0x0000001c,0x00000001,0x0004001e,0x0000001e,0x00000008,0x00000008,0x00040020,0x0000001f,
    0x00000009,0x0000001e,0x0004003b,0x0000001f,0x00000020,0x00000009,0x00040020,0x00000021,
    0x00000009,0x00000008,0x0004002b,0x00000006,0x00000028,0x00000000,0x0004002b,0x00000006,
    0x00000029,0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,
    0x00000005,0x0004003d,0x00000007,0x00000010,0x0000000f,0x00050041,0x00000011,0x00000012,
    0x0000000b,0x0000000d,0x0003003e,0x00000012,0x00000010,0x0004003d,0x00000008,0x00000016,
    0x00000015,0x00050041,0x00000017,0x00000018,0x0000000b,0x00000013,0x0003003e,0x00000018,
    0x00000016,0x0004003d,0x00000008,0x0000001d,0x0000001c,0x00050041,0x00000021,0x00000022,
    0x00000020,0x0000000d,0x0004003d,0x00000008,0x00000023,0x00000022,0x00050085,0x00000008,
    0x00000024,0x0000001d,0x00000023,0x00050041,0x00000021,0x00000025,0x00000020,0x00000013,
    0x0004003d,0x00000008,0x00000026,0x00000025,0x00050081,0x00000008,0x00000027,0x00000024,
    0x00000026,0x00050051,0x00000006,0x0000002a,0x00000027,0x00000000,0x00050051,0x00000006,
    0x0000002b,0x00000027,0x00000001,0x00070050,0x00000007,0x0000002c,0x0000002a,0x0000002b,
    0x00000028,0x00000029,0x00050041,0x00000011,0x0000002d,0x0000001b,0x0000000d,0x0003003e,
    0x0000002d,0x0000002c,0x000100fd,0x00010038
};

// backends/vulkan/glsl_shader.frag — compiled with glslangValidator
static const uint32_t s_fragSpv[] = {
    0x07230203,0x00010000,0x00080001,0x0000001e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000d,0x00030010,
    0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00040005,0x00000009,0x6c6f4366,0x0000726f,0x00030005,0x0000000b,0x00000000,
    0x00050006,0x0000000b,0x00000000,0x6f6c6f43,0x00000072,0x00040006,0x0000000b,0x00000001,
    0x00005655,0x00030005,0x0000000d,0x00006e49,0x00050005,0x00000016,0x78655473,0x65727574,
    0x00000000,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,
    0x00000000,0x00040047,0x00000016,0x00000022,0x00000000,0x00040047,0x00000016,0x00000021,
    0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
    0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,
    0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,0x0000000a,0x00000006,
    0x00000002,0x0004001e,0x0000000b,0x00000007,0x0000000a,0x00040020,0x0000000c,0x00000001,
    0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,0x00040015,0x0000000e,0x00000020,
    0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040020,0x00000010,0x00000001,
    0x00000007,0x00090019,0x00000013,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,
    0x00000001,0x00000000,0x0003001b,0x00000014,0x00000013,0x00040020,0x00000015,0x00000000,
    0x00000014,0x0004003b,0x00000015,0x00000016,0x00000000,0x0004002b,0x0000000e,0x00000018,
    0x00000001,0x00040020,0x00000019,0x00000001,0x0000000a,0x00050036,0x00000002,0x00000004,
    0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000010,0x00000011,0x0000000d,
    0x0000000f,0x0004003d,0x00000007,0x00000012,0x00000011,0x0004003d,0x00000014,0x00000017,
    0x00000016,0x00050041,0x00000019,0x0000001a,0x0000000d,0x00000018,0x0004003d,0x0000000a,
    0x0000001b,0x0000001a,0x00050057,0x00000007,0x0000001c,0x00000017,0x0000001b,0x00050085,
    0x00000007,0x0000001d,0x00000012,0x0000001c,0x0003003e,0x00000009,0x0000001d,0x000100fd,
    0x00010038
};

// clang-format on

// ============================================================================
// Constructor / Destructor
// ============================================================================

InfScreenUIRenderer::InfScreenUIRenderer() = default;

InfScreenUIRenderer::~InfScreenUIRenderer()
{
    Destroy();
}

// ============================================================================
// Initialization
// ============================================================================

bool InfScreenUIRenderer::Initialize(VkDevice device, VmaAllocator allocator, VkFormat colorFormat,
                                     VkSampleCountFlagBits msaaSamples)
{
    if (m_initialized)
        return true;

    m_device = device;
    m_allocator = allocator;
    m_colorFormat = colorFormat;
    m_msaaSamples = msaaSamples;

    if (!CreateCompatibleRenderPass()) {
        INFLOG_ERROR("InfScreenUIRenderer: Failed to create compatible render pass");
        return false;
    }

    if (!CreatePipeline()) {
        INFLOG_ERROR("InfScreenUIRenderer: Failed to create pipeline");
        return false;
    }

    // Create standalone ImDrawList instances (not attached to any ImGui window)
    ImDrawListSharedData *sharedData = ImGui::GetDrawListSharedData();
    m_cameraDrawList = IM_NEW(ImDrawList)(sharedData);
    m_overlayDrawList = IM_NEW(ImDrawList)(sharedData);

    m_initialized = true;
    INFLOG_INFO("InfScreenUIRenderer initialized (format=", static_cast<int>(colorFormat),
                ", MSAA=", static_cast<int>(msaaSamples), ")");
    return true;
}

void InfScreenUIRenderer::Destroy()
{
    if (m_cameraDrawList) {
        IM_DELETE(m_cameraDrawList);
        m_cameraDrawList = nullptr;
    }
    if (m_overlayDrawList) {
        IM_DELETE(m_overlayDrawList);
        m_overlayDrawList = nullptr;
    }

    if (m_device != VK_NULL_HANDLE) {
        if (m_vertexBuffer)
            vmaDestroyBuffer(m_allocator, m_vertexBuffer, m_vertexAlloc);
        if (m_indexBuffer)
            vmaDestroyBuffer(m_allocator, m_indexBuffer, m_indexAlloc);
        if (m_pipeline)
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_pipelineLayout)
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_descriptorSetLayout)
            vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        if (m_fontSampler)
            vkDestroySampler(m_device, m_fontSampler, nullptr);
        if (m_descriptorPool)
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_renderPass)
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        if (m_vertShader)
            vkDestroyShaderModule(m_device, m_vertShader, nullptr);
        if (m_fragShader)
            vkDestroyShaderModule(m_device, m_fragShader, nullptr);
    }

    m_vertexBuffer = VK_NULL_HANDLE;
    m_indexBuffer = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_descriptorSetLayout = VK_NULL_HANDLE;
    m_fontSampler = VK_NULL_HANDLE;
    m_fontDescriptorSet = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
    m_vertShader = VK_NULL_HANDLE;
    m_fragShader = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_initialized = false;
}

// ============================================================================
// Per-Frame Command Accumulation
// ============================================================================

void InfScreenUIRenderer::BeginFrame(uint32_t width, uint32_t height)
{
    if (!m_initialized)
        return;

    m_frameWidth = width;
    m_frameHeight = height;
    m_cameraHDRRanges.clear();
    m_overlayHDRRanges.clear();

    // Reset draw lists for new frame
    m_cameraDrawList->_ResetForNewFrame();
    m_cameraDrawList->PushTextureID(ImGui::GetIO().Fonts->TexID);
    m_cameraDrawList->PushClipRect(ImVec2(0, 0), ImVec2(static_cast<float>(width), static_cast<float>(height)));

    m_overlayDrawList->_ResetForNewFrame();
    m_overlayDrawList->PushTextureID(ImGui::GetIO().Fonts->TexID);
    m_overlayDrawList->PushClipRect(ImVec2(0, 0), ImVec2(static_cast<float>(width), static_cast<float>(height)));
}

void InfScreenUIRenderer::AddFilledRect(ScreenUIList list, float minX, float minY, float maxX, float maxY, float r,
                                        float g, float b, float a, float rounding)
{
    ImDrawList *dl = GetDrawList(list);
    if (!dl)
        return;
    const int vtxStart = dl->VtxBuffer.Size;
    const float hdrScale = ExtractHDRScale(r, g, b);
    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    dl->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY), col, rounding);
    TrackHDRColorRange(list, vtxStart, dl->VtxBuffer.Size, hdrScale);
}

void InfScreenUIRenderer::AddImage(ScreenUIList list, uint64_t textureId, float minX, float minY, float maxX,
                                   float maxY, float uv0X, float uv0Y, float uv1X, float uv1Y, float r, float g,
                                   float b, float a, float rotation, bool mirrorH, bool mirrorV, float rounding)
{
    ImDrawList *dl = GetDrawList(list);
    if (!dl || textureId == 0)
        return;
    const int vtxStart = dl->VtxBuffer.Size;
    const float hdrScale = ExtractHDRScale(r, g, b);
    ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    if (rounding > 0.5f)
        dl->AddImageRounded(ToImTextureID(textureId), ImVec2(minX, minY), ImVec2(maxX, maxY), ImVec2(uv0X, uv0Y),
                            ImVec2(uv1X, uv1Y), tint, rounding);
    else
        dl->AddImage(ToImTextureID(textureId), ImVec2(minX, minY), ImVec2(maxX, maxY), ImVec2(uv0X, uv0Y),
                     ImVec2(uv1X, uv1Y), tint);

    rotation = std::fmod(rotation, 360.0f);
    if (rotation < 0.0f)
        rotation += 360.0f;
    if ((std::fabs(rotation) < 0.001f) && !mirrorH && !mirrorV)
    {
        TrackHDRColorRange(list, vtxStart, dl->VtxBuffer.Size, hdrScale);
        return;
    }

    const float radians = rotation * 3.14159265358979f / 180.0f;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);
    const ImVec2 pivot((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    for (int i = vtxStart; i < dl->VtxBuffer.Size; ++i) {
        ImVec2 local(dl->VtxBuffer[i].pos.x - pivot.x, dl->VtxBuffer[i].pos.y - pivot.y);
        if (mirrorH)
            local.x = -local.x;
        if (mirrorV)
            local.y = -local.y;
        const float rx = local.x * cosA - local.y * sinA;
        const float ry = local.x * sinA + local.y * cosA;
        dl->VtxBuffer[i].pos = ImVec2(pivot.x + rx, pivot.y + ry);
    }
    TrackHDRColorRange(list, vtxStart, dl->VtxBuffer.Size, hdrScale);
}

void InfScreenUIRenderer::AddText(ScreenUIList list, float minX, float minY, float maxX, float maxY,
                                  const std::string &text, float r, float g, float b, float a, float alignX,
                                  float alignY, float fontSize, float wrapWidth, float rotation, bool mirrorH,
                                  bool mirrorV, const std::string &fontPath, float lineHeight, float letterSpacing)
{
    ImDrawList *dl = GetDrawList(list);
    if (!dl || text.empty())
        return;

    const textlayout::TextLayoutResult layout =
        textlayout::LayoutText({text, fontPath, ResolveFontSize(fontSize), wrapWidth, lineHeight, letterSpacing});
    ImVec2 textSize(layout.totalWidth, layout.totalHeight);

    float boxW = maxX - minX;
    float boxH = maxY - minY;

    const float hdrScale = ExtractHDRScale(r, g, b);
    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    const int vtxStart = dl->VtxBuffer.Size;
    dl->PushTextureID(ImGui::GetIO().Fonts->TexRef);
    textlayout::RenderTextBox(dl, minX, minY, maxX, maxY, layout, col, alignX, alignY, letterSpacing);
    dl->PopTextureID();

    rotation = std::fmod(rotation, 360.0f);
    if (rotation < 0.0f)
        rotation += 360.0f;
    if ((std::fabs(rotation) < 0.001f) && !mirrorH && !mirrorV)
    {
        TrackHDRColorRange(list, vtxStart, dl->VtxBuffer.Size, hdrScale);
        return;
    }

    const float radians = rotation * 3.14159265358979f / 180.0f;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);
    const ImVec2 pivot((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    for (int i = vtxStart; i < dl->VtxBuffer.Size; ++i) {
        ImVec2 local(dl->VtxBuffer[i].pos.x - pivot.x, dl->VtxBuffer[i].pos.y - pivot.y);
        if (mirrorH)
            local.x = -local.x;
        if (mirrorV)
            local.y = -local.y;
        const float rx = local.x * cosA - local.y * sinA;
        const float ry = local.x * sinA + local.y * cosA;
        dl->VtxBuffer[i].pos = ImVec2(pivot.x + rx, pivot.y + ry);
    }
    TrackHDRColorRange(list, vtxStart, dl->VtxBuffer.Size, hdrScale);
}

std::pair<float, float> InfScreenUIRenderer::MeasureText(const std::string &text, float fontSize, float wrapWidth,
                                                         const std::string &fontPath, float lineHeight,
                                                         float letterSpacing) const
{
    const textlayout::TextLayoutResult layout =
        textlayout::LayoutText({text, fontPath, ResolveFontSize(fontSize), wrapWidth, lineHeight, letterSpacing});
    return {layout.totalWidth, layout.totalHeight};
}

bool InfScreenUIRenderer::HasCommands(ScreenUIList list) const
{
    const ImDrawList *dl = GetDrawList(list);
    return dl && dl->CmdBuffer.Size > 0 && dl->VtxBuffer.Size > 0;
}

void InfScreenUIRenderer::TrackHDRColorRange(ScreenUIList list, int vertexStart, int vertexEnd, float rgbScale)
{
    if (rgbScale <= 1.0f || vertexEnd <= vertexStart) {
        return;
    }

    auto &ranges = GetHDRRanges(list);
    ranges.push_back({vertexStart, vertexEnd, rgbScale});
}

std::vector<InfScreenUIRenderer::HDRColorRange> &InfScreenUIRenderer::GetHDRRanges(ScreenUIList list)
{
    return (list == ScreenUIList::Camera) ? m_cameraHDRRanges : m_overlayHDRRanges;
}

const std::vector<InfScreenUIRenderer::HDRColorRange> &InfScreenUIRenderer::GetHDRRanges(ScreenUIList list) const
{
    return (list == ScreenUIList::Camera) ? m_cameraHDRRanges : m_overlayHDRRanges;
}

// ============================================================================
// Rendering
// ============================================================================

void InfScreenUIRenderer::Render(VkCommandBuffer cmdBuf, ScreenUIList list, uint32_t width, uint32_t height)
{
    if (!m_initialized || !m_pipeline || width == 0 || height == 0 || !m_enabled)
        return;

    ImDrawList *dl = GetDrawList(list);
    if (!dl || dl->VtxBuffer.Size == 0 || dl->IdxBuffer.Size == 0)
        return;

    // ---- Refresh font atlas descriptor set ----
    // With ImGui 1.92+ dynamic font atlas, the texture may be recreated
    // at any time (new glyphs loaded, atlas resized). We must always use
    // the latest descriptor set from the current atlas texture.
    {
        ImTextureID texId = ImGui::GetIO().Fonts->TexRef.GetTexID();
        if (texId == 0)
            return; // Font atlas not yet uploaded
        m_fontDescriptorSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(texId));
    }

    // ---- Upload vertex/index data ----
    std::vector<GPUVertex> gpuVertices;
    gpuVertices.resize(static_cast<size_t>(dl->VtxBuffer.Size));

    const auto &hdrRanges = GetHDRRanges(list);
    size_t rangeIndex = 0;
    for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
        while (rangeIndex < hdrRanges.size() && i >= hdrRanges[rangeIndex].vertexEnd) {
            ++rangeIndex;
        }

        float rgbScale = 1.0f;
        if (rangeIndex < hdrRanges.size()) {
            const HDRColorRange &range = hdrRanges[rangeIndex];
            if (i >= range.vertexStart && i < range.vertexEnd) {
                rgbScale = range.rgbScale;
            }
        }

        const ImDrawVert &src = dl->VtxBuffer[i];
        GPUVertex &dst = gpuVertices[static_cast<size_t>(i)];
        dst.pos = src.pos;
        dst.uv = src.uv;

        const ImVec4 unpacked = ImGui::ColorConvertU32ToFloat4(src.col);
        dst.color[0] = unpacked.x * rgbScale;
        dst.color[1] = unpacked.y * rgbScale;
        dst.color[2] = unpacked.z * rgbScale;
        dst.color[3] = unpacked.w;
    }

    VkDeviceSize vtxSize = gpuVertices.size() * sizeof(GPUVertex);
    VkDeviceSize idxSize = dl->IdxBuffer.Size * sizeof(ImDrawIdx);
    EnsureBuffers(vtxSize, idxSize);

    // Map and copy vertex data
    void *vtxDst = nullptr;
    vmaMapMemory(m_allocator, m_vertexAlloc, &vtxDst);
    memcpy(vtxDst, gpuVertices.data(), vtxSize);
    vmaUnmapMemory(m_allocator, m_vertexAlloc);

    void *idxDst = nullptr;
    vmaMapMemory(m_allocator, m_indexAlloc, &idxDst);
    memcpy(idxDst, dl->IdxBuffer.Data, idxSize);
    vmaUnmapMemory(m_allocator, m_indexAlloc);

    // ---- Bind pipeline ----
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // ---- Bind vertex/index buffers ----
    VkBuffer vertBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_indexBuffer, 0,
                         sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

    // ---- Set viewport ----
    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    // ---- Push constants: ortho projection (scale + translate) ----
    // Maps [0, width] x [0, height] → [-1, 1] x [-1, 1]
    float pushConstants[4];
    pushConstants[0] = 2.0f / static_cast<float>(width);  // scaleX
    pushConstants[1] = 2.0f / static_cast<float>(height); // scaleY
    pushConstants[2] = -1.0f;                             // translateX
    pushConstants[3] = -1.0f;                             // translateY
    vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), pushConstants);

    // ---- Bind font atlas descriptor set ----
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_fontDescriptorSet, 0,
                            nullptr);
    VkDescriptorSet lastBoundDescSet = m_fontDescriptorSet;

    // ---- Issue draw commands ----
    uint32_t globalVtxOffset = 0;
    uint32_t globalIdxOffset = 0;

    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    for (int cmdI = 0; cmdI < dl->CmdBuffer.Size; cmdI++) {
        const ImDrawCmd &cmd = dl->CmdBuffer[cmdI];

        if (cmd.UserCallback != nullptr) {
            // User callbacks are not supported in scene render passes
            continue;
        }

        // Per-command texture (usually font atlas)
        VkDescriptorSet texDescSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(cmd.GetTexID()));
        if (texDescSet != lastBoundDescSet) {
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &texDescSet, 0,
                                    nullptr);
            lastBoundDescSet = texDescSet;
        }

        // Scissor rect from ImDrawCmd clip rect — clamped to render area
        // to prevent Vulkan validation errors and potential DEVICE_LOST.
        float clipMinX = cmd.ClipRect.x < 0.0f ? 0.0f : cmd.ClipRect.x;
        float clipMinY = cmd.ClipRect.y < 0.0f ? 0.0f : cmd.ClipRect.y;
        float clipMaxX = cmd.ClipRect.z > fw ? fw : cmd.ClipRect.z;
        float clipMaxY = cmd.ClipRect.w > fh ? fh : cmd.ClipRect.w;
        if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            continue; // Degenerate scissor — skip draw

        VkRect2D scissor{};
        scissor.offset.x = static_cast<int32_t>(clipMinX);
        scissor.offset.y = static_cast<int32_t>(clipMinY);
        scissor.extent.width = static_cast<uint32_t>(clipMaxX - clipMinX);
        scissor.extent.height = static_cast<uint32_t>(clipMaxY - clipMinY);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        vkCmdDrawIndexed(cmdBuf, cmd.ElemCount, 1, cmd.IdxOffset + globalIdxOffset,
                         static_cast<int32_t>(cmd.VtxOffset + globalVtxOffset), 0);
    }
}

// ============================================================================
// Pipeline Creation
// ============================================================================

bool InfScreenUIRenderer::CreateCompatibleRenderPass()
{
    // Create a render pass compatible with the scene MSAA backbuffer.
    // This is only used for pipeline creation — the actual render pass
    // is created by the RenderGraph and must be compatible.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_colorFormat;
    colorAttachment.samples = m_msaaSamples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Subpass dependency must match VkPipelineManager::CreateRenderPass so that
    // pipelines compiled against this render pass are compatible with the render
    // graph's actual render passes.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    return vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass) == VK_SUCCESS;
}

bool InfScreenUIRenderer::CreatePipeline()
{
    VkResult err;

    // ---- Shader modules ----
    {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = sizeof(s_vertSpv);
        ci.pCode = s_vertSpv;
        err = vkCreateShaderModule(m_device, &ci, nullptr, &m_vertShader);
        if (err != VK_SUCCESS)
            return false;
    }
    {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = sizeof(s_fragSpv);
        ci.pCode = s_fragSpv;
        err = vkCreateShaderModule(m_device, &ci, nullptr, &m_fragShader);
        if (err != VK_SUCCESS)
            return false;
    }

    // ---- Sampler for font atlas ----
    {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.minLod = -1000.0f;
        si.maxLod = 1000.0f;
        si.maxAnisotropy = 1.0f;
        err = vkCreateSampler(m_device, &si, nullptr, &m_fontSampler);
        if (err != VK_SUCCESS)
            return false;
    }

    // ---- Descriptor set layout (identical to ImGui's) ----
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings = &binding;
        err = vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_descriptorSetLayout);
        if (err != VK_SUCCESS)
            return false;
    }

    // ---- Pipeline layout (identical to ImGui's: 4 floats push constant) ----
    {
        VkPushConstantRange pushConstRange{};
        pushConstRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstRange.offset = 0;
        pushConstRange.size = sizeof(float) * 4;

        VkPipelineLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &m_descriptorSetLayout;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pushConstRange;
        err = vkCreatePipelineLayout(m_device, &ci, nullptr, &m_pipelineLayout);
        if (err != VK_SUCCESS)
            return false;
    }

    // ---- Graphics pipeline (replicates ImGui's pipeline for scene render target) ----
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vertShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_fragShader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.stride = sizeof(GPUVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDesc[3]{};
    attrDesc[0].location = 0;
    attrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[0].offset = offsetof(GPUVertex, pos);
    attrDesc[1].location = 1;
    attrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[1].offset = offsetof(GPUVertex, uv);
    attrDesc[2].location = 2;
    attrDesc[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrDesc[2].offset = offsetof(GPUVertex, color);

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount = 1;
    vertInput.pVertexBindingDescriptions = &bindingDesc;
    vertInput.vertexAttributeDescriptionCount = 3;
    vertInput.pVertexAttributeDescriptions = attrDesc;

    VkPipelineInputAssemblyStateCreateInfo iaInfo{};
    iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpInfo{};
    vpInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpInfo.viewportCount = 1;
    vpInfo.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsInfo{};
    rsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rsInfo.cullMode = VK_CULL_MODE_NONE;
    rsInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msInfo{};
    msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msInfo.rasterizationSamples = m_msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.blendEnable = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttach.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbInfo{};
    cbInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbInfo.attachmentCount = 1;
    cbInfo.pAttachments = &blendAttach;

    VkPipelineDepthStencilStateCreateInfo dsInfo{};
    dsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // No depth test for 2D UI

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertInput;
    pipeInfo.pInputAssemblyState = &iaInfo;
    pipeInfo.pViewportState = &vpInfo;
    pipeInfo.pRasterizationState = &rsInfo;
    pipeInfo.pMultisampleState = &msInfo;
    pipeInfo.pDepthStencilState = &dsInfo;
    pipeInfo.pColorBlendState = &cbInfo;
    pipeInfo.pDynamicState = &dynInfo;
    pipeInfo.layout = m_pipelineLayout;
    pipeInfo.renderPass = m_renderPass;
    pipeInfo.subpass = 0;

    err = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline);
    return err == VK_SUCCESS;
}

// ============================================================================
// Buffer Management
// ============================================================================

void InfScreenUIRenderer::EnsureBuffers(VkDeviceSize vertexSize, VkDeviceSize indexSize)
{
    // Grow buffers if needed (with 1.5x overalloc to reduce reallocations)
    if (m_vertexBuffer == VK_NULL_HANDLE || m_vertexBufferSize < vertexSize) {
        if (m_vertexBuffer) {
            VkBuffer oldBuffer = m_vertexBuffer;
            VmaAllocation oldAlloc = m_vertexAlloc;
            if (m_deletionQueue) {
                VmaAllocator allocator = m_allocator;
                m_deletionQueue->Push(
                    [allocator, oldBuffer, oldAlloc]() { vmaDestroyBuffer(allocator, oldBuffer, oldAlloc); });
            } else {
                vmaDestroyBuffer(m_allocator, m_vertexBuffer, m_vertexAlloc);
            }
        }

        VkDeviceSize allocSize = vertexSize + (vertexSize >> 1); // 1.5x

        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = allocSize;
        ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        vmaCreateBuffer(m_allocator, &ci, &ai, &m_vertexBuffer, &m_vertexAlloc, nullptr);
        m_vertexBufferSize = allocSize;
    }

    if (m_indexBuffer == VK_NULL_HANDLE || m_indexBufferSize < indexSize) {
        if (m_indexBuffer) {
            VkBuffer oldBuffer = m_indexBuffer;
            VmaAllocation oldAlloc = m_indexAlloc;
            if (m_deletionQueue) {
                VmaAllocator allocator = m_allocator;
                m_deletionQueue->Push(
                    [allocator, oldBuffer, oldAlloc]() { vmaDestroyBuffer(allocator, oldBuffer, oldAlloc); });
            } else {
                vmaDestroyBuffer(m_allocator, m_indexBuffer, m_indexAlloc);
            }
        }

        VkDeviceSize allocSize = indexSize + (indexSize >> 1);

        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = allocSize;
        ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        vmaCreateBuffer(m_allocator, &ci, &ai, &m_indexBuffer, &m_indexAlloc, nullptr);
        m_indexBufferSize = allocSize;
    }
}

// ============================================================================
// Helpers
// ============================================================================

ImDrawList *InfScreenUIRenderer::GetDrawList(ScreenUIList list)
{
    return (list == ScreenUIList::Camera) ? m_cameraDrawList : m_overlayDrawList;
}

const ImDrawList *InfScreenUIRenderer::GetDrawList(ScreenUIList list) const
{
    return (list == ScreenUIList::Camera) ? m_cameraDrawList : m_overlayDrawList;
}

} // namespace infengine
