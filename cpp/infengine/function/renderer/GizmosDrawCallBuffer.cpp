#include "GizmosDrawCallBuffer.h"

#include <algorithm>
#include <cstring>
#include <glm/glm.hpp>
#include <unordered_set>

namespace infengine
{

// ============================================================================
// SetData — replace buffer contents with a fresh frame of gizmo geometry
// ============================================================================

void GizmosDrawCallBuffer::SetData(std::vector<Vertex> vertices, std::vector<uint32_t> indices,
                                   std::vector<DrawDescriptor> descriptors)
{
    m_vertices = std::move(vertices);
    m_indices = std::move(indices);
    m_descriptors = std::move(descriptors);
    m_slicesDirty = true;
}

// ============================================================================
// Clear
// ============================================================================

void GizmosDrawCallBuffer::Clear()
{
    m_vertices.clear();
    m_indices.clear();
    m_descriptors.clear();
    m_slicedVertices.clear();
    m_slicedIndices.clear();
    m_slicesDirty = true;

    m_iconEntries.clear();
    m_iconSlicedVertices.clear();
    m_iconSlicedIndices.clear();
    m_iconSlicesDirty = true;
}

// ============================================================================
// HasData
// ============================================================================

bool GizmosDrawCallBuffer::HasData() const
{
    return !m_descriptors.empty();
}

// ============================================================================
// RebuildSlices — split packed arrays into per-descriptor slices
// ============================================================================

void GizmosDrawCallBuffer::RebuildSlices() const
{
    if (!m_slicesDirty)
        return;

    m_slicedVertices.clear();
    m_slicedIndices.clear();
    m_slicedVertices.resize(m_descriptors.size());
    m_slicedIndices.resize(m_descriptors.size());

    for (size_t i = 0; i < m_descriptors.size(); ++i) {
        const auto &desc = m_descriptors[i];

        // Collect all unique vertex indices referenced by this descriptor's
        // index range, then build a compact vertex slice.
        uint32_t idxEnd = desc.indexStart + desc.indexCount;
        if (idxEnd > static_cast<uint32_t>(m_indices.size()))
            idxEnd = static_cast<uint32_t>(m_indices.size());

        // Find min/max vertex index to know the referenced vertex range
        uint32_t minVert = UINT32_MAX;
        uint32_t maxVert = 0;
        for (uint32_t j = desc.indexStart; j < idxEnd; ++j) {
            uint32_t vi = m_indices[j];
            if (vi < minVert)
                minVert = vi;
            if (vi > maxVert)
                maxVert = vi;
        }

        if (minVert > maxVert) {
            // No valid indices
            continue;
        }

        // Copy the referenced vertex range into the slice
        uint32_t vertCount = maxVert - minVert + 1;
        if (maxVert < static_cast<uint32_t>(m_vertices.size())) {
            m_slicedVertices[i].assign(m_vertices.begin() + minVert, m_vertices.begin() + minVert + vertCount);
        }

        // Rebase indices to the slice's local vertex range
        auto &sliceIndices = m_slicedIndices[i];
        sliceIndices.reserve(desc.indexCount);
        for (uint32_t j = desc.indexStart; j < idxEnd; ++j) {
            sliceIndices.push_back(m_indices[j] - minVert);
        }
    }

    m_slicesDirty = false;
}

// ============================================================================
// GetDrawCalls — produce DrawCallResult for SubmitCulling()
// ============================================================================

DrawCallResult GizmosDrawCallBuffer::GetDrawCalls(std::shared_ptr<InfMaterial> gizmoMaterial) const
{
    DrawCallResult result;
    if (m_descriptors.empty())
        return result;

    RebuildSlices();

    result.drawCalls.reserve(m_descriptors.size());

    for (size_t i = 0; i < m_descriptors.size(); ++i) {
        const auto &desc = m_descriptors[i];
        if (m_slicedIndices[i].empty())
            continue;

        // Build world matrix from the flat float[16]
        glm::mat4 world;
        std::memcpy(&world, desc.worldMatrix, sizeof(float) * 16);

        DrawCall dc;
        dc.indexStart = 0; // Each slice starts at 0
        dc.indexCount = static_cast<uint32_t>(m_slicedIndices[i].size());
        dc.worldMatrix = world;
        dc.material = gizmoMaterial;
        dc.objectId = OBJECT_ID_PREFIX | static_cast<uint64_t>(i);
        dc.meshVertices = &m_slicedVertices[i];
        dc.meshIndices = &m_slicedIndices[i];
        dc.forceBufferUpdate = true; // Immediate-mode: data changes every frame

        result.drawCalls.push_back(dc);
    }

    return result;
}

// ============================================================================
// Icon billboard methods
// ============================================================================

void GizmosDrawCallBuffer::SetIconData(std::vector<IconEntry> entries)
{
    m_iconEntries = std::move(entries);
    m_iconSlicesDirty = true;
}

void GizmosDrawCallBuffer::ClearIcons()
{
    m_iconEntries.clear();
    m_iconSlicedVertices.clear();
    m_iconSlicedIndices.clear();
    m_iconSlicesDirty = true;
}

bool GizmosDrawCallBuffer::HasIconData() const
{
    return !m_iconEntries.empty();
}

DrawCallResult GizmosDrawCallBuffer::GetIconDrawCalls(std::shared_ptr<InfMaterial> iconMaterial,
                                                      const glm::vec3 &cameraPos) const
{
    DrawCallResult result;
    if (m_iconEntries.empty() || !iconMaterial)
        return result;

    // Rebuild billboard geometry if entries changed
    if (m_iconSlicesDirty) {
        m_iconSlicedVertices.clear();
        m_iconSlicedIndices.clear();
        m_iconSlicedVertices.resize(m_iconEntries.size());
        m_iconSlicedIndices.resize(m_iconEntries.size());
        m_iconSlicesDirty = false;
    }

    result.drawCalls.reserve(m_iconEntries.size());

    for (size_t i = 0; i < m_iconEntries.size(); ++i) {
        const auto &icon = m_iconEntries[i];

        // Compute billboard orientation
        glm::vec3 toCamera = cameraPos - icon.position;
        float distance = glm::length(toCamera);
        if (distance < 0.001f)
            continue; // skip icons at camera position

        toCamera /= distance; // normalize

        // Build camera-facing basis
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        // Handle degenerate case when looking straight up/down
        if (std::abs(glm::dot(toCamera, worldUp)) > 0.99f) {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        glm::vec3 right = glm::normalize(glm::cross(worldUp, toCamera));
        glm::vec3 up = glm::cross(toCamera, right);

        // Constant angular size
        float worldSize = distance * ICON_SIZE_FACTOR;

        // Diamond quad: 4 outer vertices + center
        //       top
        //      / | \
        //   left-+-right
        //      \ | /
        //      bottom
        glm::vec3 top = icon.position + up * worldSize;
        glm::vec3 bottom = icon.position - up * worldSize;
        glm::vec3 leftPt = icon.position - right * worldSize;
        glm::vec3 rightPt = icon.position + right * worldSize;

        auto makeVertex = [&](const glm::vec3 &pos) -> Vertex {
            Vertex v;
            v.pos = pos;
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            v.color = icon.color;
            v.texCoord = glm::vec2(0.0f);
            return v;
        };

        // 4 vertices: top(0), right(1), bottom(2), left(3)
        auto &verts = m_iconSlicedVertices[i];
        verts.clear();
        verts.push_back(makeVertex(top));
        verts.push_back(makeVertex(rightPt));
        verts.push_back(makeVertex(bottom));
        verts.push_back(makeVertex(leftPt));

        // 2 triangles forming the diamond
        auto &indices = m_iconSlicedIndices[i];
        indices = {0, 1, 2, 0, 2, 3};

        DrawCall dc;
        dc.indexStart = 0;
        dc.indexCount = 6;
        dc.worldMatrix = glm::mat4(1.0f); // identity — vertices are in world space
        dc.material = iconMaterial;
        dc.objectId = icon.objectId; // actual GameObject ID!
        dc.meshVertices = &m_iconSlicedVertices[i];
        dc.meshIndices = &m_iconSlicedIndices[i];
        dc.forceBufferUpdate = true;

        result.drawCalls.push_back(dc);
    }

    return result;
}

} // namespace infengine
