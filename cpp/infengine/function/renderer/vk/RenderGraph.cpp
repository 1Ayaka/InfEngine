/**
 * @file RenderGraph.cpp
 * @brief RenderGraph public API — RenderContext, PassBuilder, lifecycle, Compile/Execute
 *        orchestration, and resource resolution.
 *
 * Compilation internals (culling, sorting, allocation, barriers, caching) are in
 * RenderGraphCompile.cpp.
 */

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifdef _WIN32
#define NOMINMAX
#endif

#include "RenderGraph.h"
#include "VkDeviceContext.h"
#include "VkPipelineManager.h"
#include <core/error/InfError.h>

#include <algorithm>
#include <sstream>

namespace infengine
{
namespace vk
{

// ============================================================================
// RenderContext Implementation
// ============================================================================

RenderContext::RenderContext(VkCommandBuffer cmdBuffer, RenderGraph *graph) : m_cmdBuffer(cmdBuffer), m_graph(graph)
{
}

void RenderContext::SetViewport(const VkViewport &viewport)
{
    m_viewport = viewport;
    vkCmdSetViewport(m_cmdBuffer, 0, 1, &m_viewport);
}

void RenderContext::SetScissor(const VkRect2D &scissor)
{
    m_scissor = scissor;
    vkCmdSetScissor(m_cmdBuffer, 0, 1, &m_scissor);
}

void RenderContext::BindPipeline(VkPipeline pipeline)
{
    vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void RenderContext::BindComputePipeline(VkPipeline pipeline)
{
    vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

void RenderContext::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(m_cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                                uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_cmdBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void RenderContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    vkCmdDispatch(m_cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void RenderContext::NextSubpass()
{
    vkCmdNextSubpass(m_cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);
}

VkImageView RenderContext::GetTexture(ResourceHandle handle) const
{
    return m_graph ? m_graph->ResolveTextureView(handle) : VK_NULL_HANDLE;
}

VkBuffer RenderContext::GetBuffer(ResourceHandle handle) const
{
    return m_graph ? m_graph->ResolveBuffer(handle) : VK_NULL_HANDLE;
}

// ============================================================================
// PassBuilder Implementation
// ============================================================================

PassBuilder::PassBuilder(RenderGraph *graph, uint32_t passId) : m_graph(graph), m_passId(passId)
{
}

ResourceHandle PassBuilder::CreateTexture(const std::string &name, uint32_t width, uint32_t height, VkFormat format,
                                          VkSampleCountFlagBits samples)
{
    ResourceHandle handle = m_graph->CreateResource(name, ResourceType::Texture2D);
    if (!handle.IsValid()) {
        return handle;
    }

    auto &resource = m_graph->m_resources[handle.id];
    resource.textureDesc.name = name;
    resource.textureDesc.width = width;
    resource.textureDesc.height = height;
    resource.textureDesc.format = format;
    resource.textureDesc.samples = samples;
    resource.textureDesc.isTransient = true;

    return handle;
}

ResourceHandle PassBuilder::CreateDepthStencil(const std::string &name, uint32_t width, uint32_t height,
                                               VkFormat format, VkSampleCountFlagBits samples)
{
    ResourceHandle handle = m_graph->CreateResource(name, ResourceType::DepthStencil);
    if (!handle.IsValid()) {
        return handle;
    }

    auto &resource = m_graph->m_resources[handle.id];
    resource.textureDesc.name = name;
    resource.textureDesc.width = width;
    resource.textureDesc.height = height;
    resource.textureDesc.format = format;
    resource.textureDesc.samples = samples;
    resource.textureDesc.isTransient = true;

    return handle;
}

ResourceHandle PassBuilder::CreateBuffer(const std::string &name, VkDeviceSize size, VkBufferUsageFlags usage)
{
    ResourceHandle handle = m_graph->CreateResource(name, ResourceType::Buffer);
    if (!handle.IsValid()) {
        return handle;
    }

    auto &resource = m_graph->m_resources[handle.id];
    resource.bufferDesc.name = name;
    resource.bufferDesc.size = size;
    resource.bufferDesc.usage = usage;
    resource.bufferDesc.isTransient = true;

    return handle;
}

ResourceHandle PassBuilder::ImportTexture(const std::string &name, VkImage image, VkImageView view, VkFormat format,
                                          uint32_t width, uint32_t height)
{
    ResourceHandle handle = m_graph->CreateResource(name, ResourceType::Texture2D);
    if (!handle.IsValid()) {
        return handle;
    }

    auto &resource = m_graph->m_resources[handle.id];
    resource.textureDesc.name = name;
    resource.textureDesc.width = width;
    resource.textureDesc.height = height;
    resource.textureDesc.format = format;
    resource.textureDesc.isTransient = false;
    resource.isExternal = true;
    resource.externalImage = image;
    resource.externalView = view;

    return handle;
}

ResourceHandle PassBuilder::ImportBuffer(const std::string &name, VkBuffer buffer, VkDeviceSize size)
{
    ResourceHandle handle = m_graph->CreateResource(name, ResourceType::Buffer);
    if (!handle.IsValid()) {
        return handle;
    }

    auto &resource = m_graph->m_resources[handle.id];
    resource.bufferDesc.name = name;
    resource.bufferDesc.size = size;
    resource.bufferDesc.isTransient = false;
    resource.isExternal = true;
    resource.externalBuffer = buffer;

    return handle;
}

ResourceHandle PassBuilder::Read(ResourceHandle handle, VkPipelineStageFlags stages)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Read | ResourceUsage::ShaderRead;
    access.stages = stages;
    access.access = VK_ACCESS_SHADER_READ_BIT;
    access.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    pass.reads.push_back(access);

    return handle;
}

ResourceHandle PassBuilder::WriteColor(ResourceHandle handle, uint32_t attachmentIndex)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Write | ResourceUsage::ColorOutput;
    access.stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    access.access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    pass.writes.push_back(access);

    // Ensure color outputs vector is large enough
    if (pass.colorOutputs.size() <= attachmentIndex) {
        pass.colorOutputs.resize(attachmentIndex + 1);
    }
    pass.colorOutputs[attachmentIndex] = handle;

    // New version of the resource
    ResourceHandle newHandle = handle;
    newHandle.version++;

    return newHandle;
}

ResourceHandle PassBuilder::WriteDepth(ResourceHandle handle)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Write | ResourceUsage::DepthOutput;
    access.stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    access.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    pass.writes.push_back(access);
    pass.depthOutput = handle;

    ResourceHandle newHandle = handle;
    newHandle.version++;

    return newHandle;
}

ResourceHandle PassBuilder::ReadDepth(ResourceHandle handle)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Read | ResourceUsage::DepthRead;
    access.stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    access.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    // Read-only depth: the render pass uses DEPTH_STENCIL_READ_ONLY_OPTIMAL
    // for both the subpass attachment and initialLayout/finalLayout.
    // The barrier must transition to this layout (not ATTACHMENT_OPTIMAL).
    access.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    pass.reads.push_back(access);
    pass.depthInput = handle;

    return handle; // No version bump â€” read-only
}

ResourceHandle PassBuilder::ReadWrite(ResourceHandle handle, VkPipelineStageFlags stages)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::ReadWrite;
    access.stages = stages;
    access.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_GENERAL;

    pass.reads.push_back(access);
    pass.writes.push_back(access);

    ResourceHandle newHandle = handle;
    newHandle.version++;

    return newHandle;
}

ResourceHandle PassBuilder::TransferRead(ResourceHandle handle)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Read | ResourceUsage::Transfer;
    access.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    access.access = VK_ACCESS_TRANSFER_READ_BIT;
    access.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    pass.reads.push_back(access);

    return handle;
}

ResourceHandle PassBuilder::TransferWrite(ResourceHandle handle)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];

    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Write | ResourceUsage::Transfer;
    access.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    access.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    pass.writes.push_back(access);

    ResourceHandle newHandle = handle;
    newHandle.version++;

    return newHandle;
}

ResourceHandle PassBuilder::WriteResolve(ResourceHandle handle)
{
    if (!handle.IsValid()) {
        return handle;
    }

    auto &pass = m_graph->m_passes[m_passId];
    pass.resolveOutput = handle;

    // Track as a write so dependency/lifetime analysis picks it up
    ResourceAccess access;
    access.handle = handle;
    access.usage = ResourceUsage::Write | ResourceUsage::ColorOutput;
    access.stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    access.access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    pass.writes.push_back(access);

    ResourceHandle newHandle = handle;
    newHandle.version++;
    return newHandle;
}

void PassBuilder::SetRenderArea(uint32_t width, uint32_t height)
{
    m_graph->m_passes[m_passId].renderArea = {width, height};
}

void PassBuilder::SetClearColor(float r, float g, float b, float a)
{
    auto &pass = m_graph->m_passes[m_passId];
    pass.clearColor = {{r, g, b, a}};
    pass.clearColorEnabled = true;
}

void PassBuilder::SetClearDepth(float depth, uint32_t stencil)
{
    auto &pass = m_graph->m_passes[m_passId];
    pass.clearDepth = {depth, stencil};
    pass.clearDepthEnabled = true;
}

// ============================================================================
// RenderGraph Implementation
// ============================================================================

RenderGraph::RenderGraph() = default;

RenderGraph::~RenderGraph()
{
    Destroy();
}

RenderGraph::RenderGraph(RenderGraph &&other) noexcept
    : m_context(other.m_context), m_pipelineManager(other.m_pipelineManager), m_passes(std::move(other.m_passes)),
      m_resources(std::move(other.m_resources)), m_executionOrder(std::move(other.m_executionOrder)),
      m_backbuffer(other.m_backbuffer), m_output(other.m_output), m_compiled(other.m_compiled)
{
    other.m_context = nullptr;
    other.m_pipelineManager = nullptr;
    other.m_compiled = false;
}

RenderGraph &RenderGraph::operator=(RenderGraph &&other) noexcept
{
    if (this != &other) {
        Destroy();

        m_context = other.m_context;
        m_pipelineManager = other.m_pipelineManager;
        m_passes = std::move(other.m_passes);
        m_resources = std::move(other.m_resources);
        m_executionOrder = std::move(other.m_executionOrder);
        m_backbuffer = other.m_backbuffer;
        m_output = other.m_output;
        m_compiled = other.m_compiled;

        other.m_context = nullptr;
        other.m_pipelineManager = nullptr;
        other.m_compiled = false;
    }
    return *this;
}

void RenderGraph::Initialize(VkDeviceContext *context, VkPipelineManager *pipelineManager)
{
    m_context = context;
    m_pipelineManager = pipelineManager;
}

void RenderGraph::Reset()
{
    // Phase 1: Only free per-frame resources, NOT cached VkRenderPass/Framebuffer
    // FreeResources() destroys per-frame framebuffers and transient images.
    // RenderPass cache and framebuffer cache persist across frames.
    FreeResources();
    m_passes.clear();
    m_resources.clear();
    m_executionOrder.clear();
    m_resourceStates.clear();
    m_usedRenderPassKeys.clear();
    m_usedFramebufferKeys.clear();
    m_backbuffer = {};
    m_output = {};
    m_compiled = false;

    // GC: flush unused cache entries periodically
    FlushUnusedCaches();
}

void RenderGraph::Destroy()
{
    FreeResources();

    // Destroy cached render passes
    if (m_context) {
        VkDevice device = m_context->GetDevice();
        for (auto &[key, rp] : m_renderPassCache) {
            if (rp != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, rp, nullptr);
            }
        }
        for (auto &[key, entry] : m_framebufferCache) {
            if (entry.framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, entry.framebuffer, nullptr);
            }
        }
    }
    m_renderPassCache.clear();
    m_framebufferCache.clear();

    m_passes.clear();
    m_resources.clear();
    m_executionOrder.clear();
    m_resourceStates.clear();
    m_context = nullptr;
    m_pipelineManager = nullptr;
}

PassHandle RenderGraph::AddPass(const std::string &name, PassSetupCallback setup)
{
    PassHandle handle;
    handle.id = static_cast<uint32_t>(m_passes.size());

    RenderPassData passData;
    passData.name = name;
    passData.id = handle.id;
    passData.type = PassType::Graphics;

    m_passes.push_back(std::move(passData));

    // Run setup callback
    PassBuilder builder(this, handle.id);
    auto executeCallback = setup(builder);
    m_passes[handle.id].executeCallback = std::move(executeCallback);

    return handle;
}

PassHandle RenderGraph::AddComputePass(const std::string &name, PassSetupCallback setup)
{
    PassHandle handle;
    handle.id = static_cast<uint32_t>(m_passes.size());

    RenderPassData passData;
    passData.name = name;
    passData.id = handle.id;
    passData.type = PassType::Compute;

    m_passes.push_back(std::move(passData));

    PassBuilder builder(this, handle.id);
    auto executeCallback = setup(builder);
    m_passes[handle.id].executeCallback = std::move(executeCallback);

    return handle;
}

PassHandle RenderGraph::AddTransferPass(const std::string &name, PassSetupCallback setup)
{
    PassHandle handle;
    handle.id = static_cast<uint32_t>(m_passes.size());

    RenderPassData passData;
    passData.name = name;
    passData.id = handle.id;
    passData.type = PassType::Transfer;

    m_passes.push_back(std::move(passData));

    PassBuilder builder(this, handle.id);
    auto executeCallback = setup(builder);
    m_passes[handle.id].executeCallback = std::move(executeCallback);

    return handle;
}

ResourceHandle RenderGraph::SetBackbuffer(VkImage image, VkImageView view, VkFormat format, uint32_t width,
                                          uint32_t height, VkSampleCountFlagBits samples)
{
    ResourceHandle handle;
    handle.id = static_cast<uint32_t>(m_resources.size());
    handle.version = 0;

    ResourceData resource;
    resource.name = "Backbuffer";
    resource.type = ResourceType::Texture2D;
    resource.textureDesc.name = "Backbuffer";
    resource.textureDesc.width = width;
    resource.textureDesc.height = height;
    resource.textureDesc.format = format;
    resource.textureDesc.samples = samples;
    resource.textureDesc.isTransient = false;
    resource.isExternal = true;
    resource.externalImage = image;
    resource.externalView = view;

    m_resources.push_back(std::move(resource));
    m_backbuffer = handle;

    return handle;
}

ResourceHandle RenderGraph::ImportResolveTarget(VkImage image, VkImageView view, VkFormat format, uint32_t width,
                                                uint32_t height)
{
    ResourceHandle handle;
    handle.id = static_cast<uint32_t>(m_resources.size());
    handle.version = 0;

    ResourceData resource;
    resource.name = "ResolveTarget";
    resource.type = ResourceType::Texture2D;
    resource.textureDesc.name = "ResolveTarget";
    resource.textureDesc.width = width;
    resource.textureDesc.height = height;
    resource.textureDesc.format = format;
    resource.textureDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    resource.textureDesc.isTransient = false;
    resource.isExternal = true;
    resource.externalImage = image;
    resource.externalView = view;

    m_resources.push_back(std::move(resource));
    return handle;
}

void RenderGraph::SetOutput(ResourceHandle handle)
{
    m_output = handle;
}

ResourceHandle RenderGraph::RegisterTransientTexture(const std::string &name, uint32_t width, uint32_t height,
                                                     VkFormat format, VkSampleCountFlagBits samples, bool isTransient)
{
    ResourceHandle handle = CreateResource(name, ResourceType::Texture2D);
    if (handle.IsValid()) {
        auto &res = m_resources[handle.id];
        res.textureDesc.name = name;
        res.textureDesc.width = width;
        res.textureDesc.height = height;
        res.textureDesc.format = format;
        res.textureDesc.samples = samples;
        res.textureDesc.isTransient = isTransient;
    }
    return handle;
}

bool RenderGraph::UpdatePassClearColor(const std::string &passName, float r, float g, float b, float a)
{
    for (auto &pass : m_passes) {
        if (pass.name == passName) {
            pass.clearColor = {{r, g, b, a}};
            return true;
        }
    }
    return false;
}

bool RenderGraph::UpdatePassClearDepth(const std::string &passName, float depth, uint32_t stencil)
{
    for (auto &pass : m_passes) {
        if (pass.name == passName) {
            pass.clearDepth = {depth, stencil};
            return true;
        }
    }
    return false;
}

ResourceHandle RenderGraph::CreateResource(const std::string &name, ResourceType type)
{
    ResourceHandle handle;
    handle.id = static_cast<uint32_t>(m_resources.size());
    handle.version = 0;

    ResourceData resource;
    resource.name = name;
    resource.type = type;

    m_resources.push_back(std::move(resource));

    return handle;
}

bool RenderGraph::Compile()
{
    if (m_passes.empty()) {
        INFLOG_WARN("RenderGraph::Compile - No passes to compile");
        return true;
    }

    // Step 1: Cull unused passes
    CullPasses();

    // Step 2: Compute resource lifetimes
    ComputeResourceLifetimes();

    // Step 3: Topological sort (Phase 1 â€” Kahn's algorithm)
    TopologicalSort();

    // Step 4: Allocate transient resources
    if (!AllocateResources()) {
        return false;
    }

    // Step 5: Create Vulkan render passes
    if (!CreateVulkanRenderPasses()) {
        return false;
    }

    // Step 6: Create framebuffers
    if (!CreateFramebuffers()) {
        return false;
    }

    m_compiled = true;
    return true;
}

void RenderGraph::Execute(VkCommandBuffer commandBuffer)
{
    if (!m_compiled) {
        INFLOG_ERROR("RenderGraph::Execute - Graph not compiled");
        return;
    }

    RenderContext context(commandBuffer, this);

    for (uint32_t passIndex : m_executionOrder) {
        auto &pass = m_passes[passIndex];

        if (pass.culled) {
            continue;
        }

        // Insert barriers
        InsertBarriers(commandBuffer, passIndex);

        // Begin render pass (for graphics passes)
        if (pass.type == PassType::Graphics && pass.vulkanRenderPass != VK_NULL_HANDLE) {
            VkRenderPassBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            beginInfo.renderPass = pass.vulkanRenderPass;
            beginInfo.framebuffer = pass.framebuffer;
            beginInfo.renderArea.offset = {0, 0};
            beginInfo.renderArea.extent = pass.renderArea;

            // Clear values are positional (indexed by attachment index).
            // Always provide a value for each attachment; Vulkan ignores
            // values whose loadOp is not CLEAR.
            std::vector<VkClearValue> clearValues;
            // Color attachment(s)
            for (size_t ci = 0; ci < pass.colorOutputs.size(); ++ci) {
                VkClearValue cv{};
                cv.color = pass.clearColor;
                clearValues.push_back(cv);
            }
            // Depth attachment
            ResourceHandle effectiveDepthExec = GetEffectiveDepth(pass);
            if (effectiveDepthExec.IsValid()) {
                VkClearValue cv{};
                cv.depthStencil = pass.clearDepth;
                clearValues.push_back(cv);
            }
            // Resolve attachment
            if (pass.hasResolveAttachment) {
                VkClearValue cv{};
                cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                clearValues.push_back(cv);
            }

            beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            beginInfo.pClearValues = clearValues.empty() ? nullptr : clearValues.data();

            vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Set default viewport and scissor
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(pass.renderArea.width);
            viewport.height = static_cast<float>(pass.renderArea.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            context.SetViewport(viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = pass.renderArea;
            context.SetScissor(scissor);
        }

        // Execute pass callback
        if (pass.executeCallback) {
            pass.executeCallback(context);
        }

        // End render pass
        if (pass.type == PassType::Graphics && pass.vulkanRenderPass != VK_NULL_HANDLE) {
            vkCmdEndRenderPass(commandBuffer);
        }
    }
}

std::string RenderGraph::GetDebugString() const
{
    std::ostringstream oss;
    oss << "RenderGraph (" << m_passes.size() << " passes, " << m_resources.size() << " resources)\n";

    oss << "\nPasses:\n";
    for (const auto &pass : m_passes) {
        oss << "  [" << pass.id << "] " << pass.name;
        if (pass.culled) {
            oss << " (CULLED)";
        }
        oss << "\n";

        if (!pass.reads.empty()) {
            oss << "    Reads: ";
            for (const auto &read : pass.reads) {
                oss << m_resources[read.handle.id].name << " ";
            }
            oss << "\n";
        }

        if (!pass.writes.empty()) {
            oss << "    Writes: ";
            for (const auto &write : pass.writes) {
                oss << m_resources[write.handle.id].name << " ";
            }
            oss << "\n";
        }
    }

    oss << "\nResources:\n";
    for (const auto &resource : m_resources) {
        oss << "  " << resource.name;
        if (resource.isExternal) {
            oss << " (external)";
        }
        oss << " - first pass: " << resource.firstPass << ", last pass: " << resource.lastPass;
        oss << "\n";
    }

    return oss.str();
}

VkImageView RenderGraph::ResolveTextureView(ResourceHandle handle) const
{
    if (!handle.IsValid() || handle.id >= m_resources.size()) {
        return VK_NULL_HANDLE;
    }

    const auto &resource = m_resources[handle.id];
    if (resource.isExternal) {
        return resource.externalView;
    }
    return resource.allocatedView;
}

VkBuffer RenderGraph::ResolveBuffer(ResourceHandle handle) const
{
    if (!handle.IsValid() || handle.id >= m_resources.size()) {
        return VK_NULL_HANDLE;
    }

    const auto &resource = m_resources[handle.id];
    if (resource.isExternal) {
        return resource.externalBuffer;
    }
    return resource.allocatedBuffer;
}

VkRenderPass RenderGraph::GetPassRenderPass(const std::string &passName) const
{
    for (const auto &pass : m_passes) {
        if (pass.name == passName && pass.vulkanRenderPass != VK_NULL_HANDLE) {
            return pass.vulkanRenderPass;
        }
    }
    return VK_NULL_HANDLE;
}

VkRenderPass RenderGraph::GetCompatibleRenderPass() const
{
    // Return the first non-culled graphics pass render pass
    // This is suitable for pipeline creation since all scene passes
    // share the same attachment format
    for (const auto &pass : m_passes) {
        if (!pass.culled && pass.type == PassType::Graphics && pass.vulkanRenderPass != VK_NULL_HANDLE) {
            return pass.vulkanRenderPass;
        }
    }
    return VK_NULL_HANDLE;
}

} // namespace vk
} // namespace infengine
