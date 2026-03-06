/**
 * @file CommandBuffer.cpp
 * @brief Implementation of the deferred-recording CommandBuffer.
 *
 * Each public method simply pushes a RenderCommand variant onto the internal
 * command list.  Actual GPU work is deferred to
 * ScriptableRenderContext::ExecuteCommandBuffer().
 */

#include "CommandBuffer.h"
#include <core/log/InfLog.h>
#include <stdexcept>

namespace infengine
{

// ============================================================================
// Construction / Clear
// ============================================================================

CommandBuffer::CommandBuffer(const std::string &name) : m_name(name)
{
    m_commands.reserve(32); // Typical small pipeline: ~10-20 commands
}

void CommandBuffer::Clear()
{
    m_commands.clear();
    // NOTE: m_nextHandleId is intentionally NOT reset.
    // Handle IDs are unique across the lifetime of this CommandBuffer object
    // to prevent aliasing after a clear-then-reuse cycle.
}

// ============================================================================
// Render Target Management
// ============================================================================

RenderTargetHandle CommandBuffer::GetTemporaryRT(int width, int height, VkFormat format, VkSampleCountFlagBits samples)
{
    if (width <= 0 || height <= 0) {
        INFLOG_WARN("CommandBuffer '", m_name, "': GetTemporaryRT with invalid size (", width, "x", height, ")");
        return RenderTargetHandle{UINT32_MAX};
    }

    RenderTargetHandle handle{m_nextHandleId++};

    GetTemporaryRTParams params;
    params.handleId = handle.id;
    params.width = width;
    params.height = height;
    params.format = format;
    params.samples = samples;

    m_commands.push_back({RenderCommandType::GetTemporaryRT, params});
    return handle;
}

void CommandBuffer::ReleaseTemporaryRT(RenderTargetHandle handle)
{
    if (!handle.IsValid()) {
        INFLOG_WARN("CommandBuffer '", m_name, "': ReleaseTemporaryRT with invalid handle");
        return;
    }

    ReleaseTemporaryRTParams params;
    params.handleId = handle.id;

    m_commands.push_back({RenderCommandType::ReleaseTemporaryRT, params});
}

void CommandBuffer::SetRenderTarget(RenderTargetHandle colorTarget)
{
    SetRenderTargetParams params;
    params.colorHandleId = colorTarget.id;
    params.depthHandleId = UINT32_MAX; // no explicit depth

    m_commands.push_back({RenderCommandType::SetRenderTarget, params});
}

void CommandBuffer::SetRenderTarget(RenderTargetHandle colorTarget, RenderTargetHandle depthTarget)
{
    SetRenderTargetParams params;
    params.colorHandleId = colorTarget.id;
    params.depthHandleId = depthTarget.id;

    m_commands.push_back({RenderCommandType::SetRenderTarget, params});
}

void CommandBuffer::ClearRenderTarget(bool clearColor, bool clearDepth, float r, float g, float b, float a, float depth)
{
    ClearRenderTargetParams params;
    params.clearColor = clearColor;
    params.clearDepth = clearDepth;
    params.r = r;
    params.g = g;
    params.b = b;
    params.a = a;
    params.depth = depth;

    m_commands.push_back({RenderCommandType::ClearRenderTarget, params});
}

// ============================================================================
// Global Shader Parameters
// ============================================================================

void CommandBuffer::SetGlobalTexture(const std::string &name, RenderTargetHandle handle)
{
    SetGlobalTextureParams params;
    params.name = name;
    params.handleId = handle.id;

    m_commands.push_back({RenderCommandType::SetGlobalTexture, params});
}

void CommandBuffer::SetGlobalFloat(const std::string &name, float value)
{
    SetGlobalFloatParams params;
    params.name = name;
    params.value = value;

    m_commands.push_back({RenderCommandType::SetGlobalFloat, params});
}

void CommandBuffer::SetGlobalVector(const std::string &name, float x, float y, float z, float w)
{
    SetGlobalVectorParams params;
    params.name = name;
    params.x = x;
    params.y = y;
    params.z = z;
    params.w = w;

    m_commands.push_back({RenderCommandType::SetGlobalVector, params});
}

void CommandBuffer::SetGlobalMatrix(const std::string &name, const std::array<float, 16> &data)
{
    SetGlobalMatrixParams params;
    params.name = name;
    params.data = data;

    m_commands.push_back({RenderCommandType::SetGlobalMatrix, params});
}

// ============================================================================
// Async Readback
// ============================================================================

void CommandBuffer::RequestAsyncReadback(RenderTargetHandle handle, const std::string &callbackId)
{
    if (!handle.IsValid()) {
        INFLOG_WARN("CommandBuffer '", m_name, "': RequestAsyncReadback with invalid handle");
        return;
    }
    if (callbackId.empty()) {
        INFLOG_WARN("CommandBuffer '", m_name, "': RequestAsyncReadback with empty callbackId");
        return;
    }

    RequestAsyncReadbackParams params;
    params.handleId = handle.id;
    params.callbackId = callbackId;

    m_commands.push_back({RenderCommandType::RequestAsyncReadback, params});
}

} // namespace infengine
