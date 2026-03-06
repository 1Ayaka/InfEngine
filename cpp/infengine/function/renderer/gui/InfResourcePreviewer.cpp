#include "InfResourcePreviewer.h"
#include "InfGUI.h"
#include <core/log/InfLog.h>
#include <function/resources/InfFileLoader/InfTextureLoader.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace infengine
{

// ============================================================================
// IResourcePreviewer
// ============================================================================

bool IResourcePreviewer::CanPreview(const std::string &filePath) const
{
    // Get extension in lowercase
    std::string ext;
    size_t dotPos = filePath.rfind('.');
    if (dotPos != std::string::npos) {
        ext = filePath.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    auto supported = GetSupportedExtensions();
    return std::find(supported.begin(), supported.end(), ext) != supported.end();
}

// ============================================================================
// ResourcePreviewManager
// ============================================================================

ResourcePreviewManager::ResourcePreviewManager()
{
}

ResourcePreviewManager::~ResourcePreviewManager()
{
    UnloadPreview();
}

void ResourcePreviewManager::SetGUI(InfGUI *gui)
{
    m_gui = gui;

    // Register default previewers
    RegisterPreviewer(std::make_shared<ImagePreviewer>(gui));
    RegisterPreviewer(std::make_shared<TextPreviewer>());
    RegisterPreviewer(std::make_shared<BinaryPreviewer>());
}

void ResourcePreviewManager::RegisterPreviewer(std::shared_ptr<IResourcePreviewer> previewer)
{
    m_previewers.push_back(previewer);

    // Map extensions to this previewer
    for (const auto &ext : previewer->GetSupportedExtensions()) {
        m_extensionMap[ext] = previewer.get();
    }

    std::string extList;
    for (const auto &e : previewer->GetSupportedExtensions()) {
        if (!extList.empty())
            extList += ", ";
        extList += e;
    }
    INFLOG_INFO("Registered ", previewer->GetTypeName(), " previewer for extensions: ", extList);
}

bool ResourcePreviewManager::HasPreviewer(const std::string &extension) const
{
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }
    return m_extensionMap.find(ext) != m_extensionMap.end();
}

std::string ResourcePreviewManager::GetPreviewerTypeName(const std::string &extension) const
{
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }

    auto it = m_extensionMap.find(ext);
    if (it != m_extensionMap.end()) {
        return it->second->GetTypeName();
    }
    return "";
}

std::vector<std::string> ResourcePreviewManager::GetAllSupportedExtensions() const
{
    std::vector<std::string> result;
    for (const auto &pair : m_extensionMap) {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string ResourcePreviewManager::GetExtension(const std::string &filePath) const
{
    size_t dotPos = filePath.rfind('.');
    if (dotPos != std::string::npos) {
        std::string ext = filePath.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }
    return "";
}

IResourcePreviewer *ResourcePreviewManager::FindPreviewer(const std::string &filePath) const
{
    std::string ext = GetExtension(filePath);
    auto it = m_extensionMap.find(ext);
    if (it != m_extensionMap.end()) {
        return it->second;
    }
    return nullptr;
}

bool ResourcePreviewManager::LoadPreview(const std::string &filePath)
{
    // Find appropriate previewer
    IResourcePreviewer *previewer = FindPreviewer(filePath);
    if (!previewer) {
        INFLOG_WARN("No previewer found for: ", filePath);
        return false;
    }

    // Unload previous if different previewer
    if (m_currentPreviewer && m_currentPreviewer != previewer) {
        m_currentPreviewer->Unload();
    }

    // Load with new previewer
    m_currentPreviewer = previewer;
    if (!m_currentPreviewer->Load(filePath)) {
        INFLOG_ERROR("Failed to load preview for: ", filePath);
        m_currentPreviewer = nullptr;
        return false;
    }

    return true;
}

void ResourcePreviewManager::RenderPreview(InfGUIContext *ctx, float availWidth, float availHeight)
{
    if (m_currentPreviewer && m_currentPreviewer->IsLoaded()) {
        m_currentPreviewer->Render(ctx, availWidth, availHeight);
    }
}

void ResourcePreviewManager::RenderMetadata(InfGUIContext *ctx)
{
    if (m_currentPreviewer && m_currentPreviewer->IsLoaded()) {
        auto metadata = m_currentPreviewer->GetMetadata();
        for (const auto &[key, value] : metadata) {
            ctx->Label(key + ": " + value);
        }
    }
}

void ResourcePreviewManager::UnloadPreview()
{
    if (m_currentPreviewer) {
        m_currentPreviewer->Unload();
        m_currentPreviewer = nullptr;
    }
}

bool ResourcePreviewManager::IsPreviewLoaded() const
{
    return m_currentPreviewer && m_currentPreviewer->IsLoaded();
}

std::string ResourcePreviewManager::GetLoadedPath() const
{
    if (m_currentPreviewer) {
        return m_currentPreviewer->GetLoadedPath();
    }
    return "";
}

std::string ResourcePreviewManager::GetCurrentTypeName() const
{
    if (m_currentPreviewer) {
        return m_currentPreviewer->GetTypeName();
    }
    return "";
}

void ResourcePreviewManager::SetPreviewSettings(int displayMode, int maxSize, bool srgb)
{
    if (m_currentPreviewer) {
        PreviewDisplayMode dm = (displayMode == 1) ? PreviewDisplayMode::NormalMap : PreviewDisplayMode::Default;
        m_currentPreviewer->SetPreviewSettings(dm, maxSize, srgb);
    }
}

// ============================================================================
// ImagePreviewer
// ============================================================================

ImagePreviewer::ImagePreviewer(InfGUI *gui) : m_gui(gui)
{
}

ImagePreviewer::~ImagePreviewer()
{
    Unload();
}

std::vector<std::string> ImagePreviewer::GetSupportedExtensions() const
{
    return {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".gif", ".psd", ".pic", ".pnm"};
}

bool ImagePreviewer::Load(const std::string &filePath)
{
    if (m_loadedPath == filePath && m_textureId != 0) {
        return true; // Already loaded
    }

    Unload();

    // Get file size
    try {
        m_fileSize = std::filesystem::file_size(filePath);
    } catch (...) {
        m_fileSize = 0;
    }

    // Load texture using InfTextureLoader
    InfTextureData texData = InfTextureLoader::LoadFromFile(filePath);
    if (!texData.IsValid()) {
        INFLOG_ERROR("Failed to load image: ", filePath);
        return false;
    }

    m_width = texData.width;
    m_height = texData.height;
    m_channels = texData.channels;
    m_originalPixels = texData.pixels; // keep a copy for display-mode re-processing

    // Upload to ImGui using file path as the texture name
    m_textureId = m_gui->UploadTextureForImGui(filePath, texData.pixels.data(), m_width, m_height);
    if (m_textureId == 0) {
        INFLOG_ERROR("Failed to upload texture to ImGui: ", filePath);
        return false;
    }

    m_loadedPath = filePath;
    INFLOG_INFO("Loaded image preview: ", filePath, " (", m_width, "x", m_height, ")");
    return true;
}

void ImagePreviewer::Render(InfGUIContext *ctx, float availWidth, float availHeight)
{
    if (m_textureId == 0)
        return;

    // Calculate image size to fit in available space while preserving aspect ratio
    float imgAspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    float areaAspect = availWidth / availHeight;

    float drawWidth, drawHeight;
    if (imgAspect > areaAspect) {
        // Image is wider - fit by width
        drawWidth = availWidth;
        drawHeight = availWidth / imgAspect;
    } else {
        // Image is taller - fit by height
        drawHeight = availHeight;
        drawWidth = availHeight * imgAspect;
    }

    // Center the image
    float offsetX = (availWidth - drawWidth) * 0.5f;
    float offsetY = (availHeight - drawHeight) * 0.5f;

    float cursorX = ctx->GetCursorPosX();
    float cursorY = ctx->GetCursorPosY();

    ctx->SetCursorPosX(cursorX + offsetX);
    ctx->SetCursorPosY(cursorY + offsetY);
    ctx->Image(reinterpret_cast<void *>(m_textureId), drawWidth, drawHeight);
}

void ImagePreviewer::Unload()
{
    if (m_textureId != 0) {
        // TODO: Implement texture cleanup in InfGUI
        m_textureId = 0;
    }
    m_loadedPath.clear();
    m_width = 0;
    m_height = 0;
    m_channels = 0;
    m_fileSize = 0;
    m_originalPixels.clear();
    m_displayMode = PreviewDisplayMode::Default;
    m_maxSize = 0;
    m_srgb = true;
}

void ImagePreviewer::SetPreviewSettings(PreviewDisplayMode mode, int maxSize, bool srgb)
{
    if (m_displayMode == mode && m_maxSize == maxSize && m_srgb == srgb)
        return;
    m_displayMode = mode;
    m_maxSize = maxSize;
    m_srgb = srgb;
    if (!m_originalPixels.empty() && m_width > 0 && m_height > 0) {
        ApplyPreviewSettings();
    }
}

void ImagePreviewer::ApplyPreviewSettings()
{
    int procW = m_width;
    int procH = m_height;
    std::vector<unsigned char> processed;

    // --- Step 1: display mode (normal-map conversion) ---
    if (m_displayMode == PreviewDisplayMode::NormalMap) {
        const int w = m_width;
        const int h = m_height;
        const float strength = 2.0f;
        processed.resize(static_cast<size_t>(w) * h * 4);

        auto getGray = [&](int px, int py) -> float {
            px = std::clamp(px, 0, w - 1);
            py = std::clamp(py, 0, h - 1);
            size_t idx = (static_cast<size_t>(py) * w + px) * 4;
            return (m_originalPixels[idx] * 0.299f + m_originalPixels[idx + 1] * 0.587f +
                    m_originalPixels[idx + 2] * 0.114f) /
                   255.0f;
        };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float hl = getGray(x - 1, y);
                float hr = getGray(x + 1, y);
                float hu = getGray(x, y - 1);
                float hd = getGray(x, y + 1);

                float nx = (hl - hr) * strength;
                float ny = (hu - hd) * strength;
                float nz = 1.0f;

                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= len;
                ny /= len;
                nz /= len;

                size_t idx = (static_cast<size_t>(y) * w + x) * 4;
                processed[idx + 0] = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
                processed[idx + 1] = static_cast<unsigned char>((ny * 0.5f + 0.5f) * 255.0f);
                processed[idx + 2] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
                processed[idx + 3] = 255;
            }
        }
    } else {
        processed = m_originalPixels;
    }

    // --- Step 2: max-size down-scale (bilinear) ---
    if (m_maxSize > 0 && (procW > m_maxSize || procH > m_maxSize)) {
        float scale = static_cast<float>(m_maxSize) / static_cast<float>(std::max(procW, procH));
        int dstW = std::max(1, static_cast<int>(procW * scale));
        int dstH = std::max(1, static_cast<int>(procH * scale));
        std::vector<unsigned char> resized(static_cast<size_t>(dstW) * dstH * 4);

        for (int dy = 0; dy < dstH; ++dy) {
            for (int dx = 0; dx < dstW; ++dx) {
                float srcXf = (dx + 0.5f) * procW / static_cast<float>(dstW) - 0.5f;
                float srcYf = (dy + 0.5f) * procH / static_cast<float>(dstH) - 0.5f;

                int x0 = std::clamp(static_cast<int>(std::floor(srcXf)), 0, procW - 1);
                int y0 = std::clamp(static_cast<int>(std::floor(srcYf)), 0, procH - 1);
                int x1 = std::clamp(x0 + 1, 0, procW - 1);
                int y1 = std::clamp(y0 + 1, 0, procH - 1);

                float fx = srcXf - std::floor(srcXf);
                float fy = srcYf - std::floor(srcYf);

                for (int c = 0; c < 4; ++c) {
                    float v00 = processed[(static_cast<size_t>(y0) * procW + x0) * 4 + c];
                    float v10 = processed[(static_cast<size_t>(y0) * procW + x1) * 4 + c];
                    float v01 = processed[(static_cast<size_t>(y1) * procW + x0) * 4 + c];
                    float v11 = processed[(static_cast<size_t>(y1) * procW + x1) * 4 + c];
                    float v = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy) + v01 * (1 - fx) * fy + v11 * fx * fy;
                    resized[(static_cast<size_t>(dy) * dstW + dx) * 4 + c] =
                        static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f));
                }
            }
        }
        processed = std::move(resized);
        procW = dstW;
        procH = dstH;
    }

    // --- Step 3: sRGB toggle ---
    // sRGB = true  → texture authored in sRGB, shown as-is (normal)
    // sRGB = false → GPU will treat values as linear; simulate by removing gamma
    //               so the preview looks darker, matching how it appears in-engine
    if (!m_srgb) {
        for (size_t i = 0; i < processed.size(); i += 4) {
            for (int c = 0; c < 3; ++c) {
                float v = processed[i + c] / 255.0f;
                // Apply sRGB→linear (remove gamma)
                v = (v <= 0.04045f) ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
                processed[i + c] = static_cast<unsigned char>(std::clamp(v * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    // --- Upload to ImGui ---
    std::string texName = m_loadedPath + "::prev_" + std::to_string(static_cast<int>(m_displayMode)) + "_" +
                          std::to_string(m_maxSize) + "_" + (m_srgb ? "1" : "0");
    m_textureId = m_gui->UploadTextureForImGui(texName, processed.data(), procW, procH);
}

std::vector<std::pair<std::string, std::string>> ImagePreviewer::GetMetadata() const
{
    std::vector<std::pair<std::string, std::string>> result;
    result.emplace_back("Type", "Image");
    result.emplace_back("Width", std::to_string(m_width) + " px");
    result.emplace_back("Height", std::to_string(m_height) + " px");
    result.emplace_back("Channels", std::to_string(m_channels));

    // Format file size
    std::string sizeStr;
    if (m_fileSize < 1024) {
        sizeStr = std::to_string(m_fileSize) + " B";
    } else if (m_fileSize < 1024 * 1024) {
        sizeStr = std::to_string(m_fileSize / 1024) + " KB";
    } else {
        sizeStr = std::to_string(m_fileSize / (1024 * 1024)) + " MB";
    }
    result.emplace_back("Size", sizeStr);

    return result;
}

// ============================================================================
// TextPreviewer
// ============================================================================

TextPreviewer::TextPreviewer()
{
}

std::vector<std::string> TextPreviewer::GetSupportedExtensions() const
{
    return {".txt", ".md",   ".json", ".xml",  ".yaml", ".yml",  ".ini",  ".cfg",  ".conf",      ".log",
            ".csv", ".html", ".htm",  ".css",  ".js",   ".ts",   ".py",   ".cpp",  ".c",         ".h",
            ".hpp", ".java", ".cs",   ".go",   ".rs",   ".rb",   ".php",  ".lua",  ".sh",        ".bat",
            ".ps1", ".glsl", ".hlsl", ".vert", ".frag", ".comp", ".toml", ".lock", ".gitignore", ".editorconfig"};
}

bool TextPreviewer::Load(const std::string &filePath)
{
    if (m_loadedPath == filePath && m_loaded) {
        return true;
    }

    Unload();

    // Get file size
    try {
        m_fileSize = std::filesystem::file_size(filePath);
    } catch (...) {
        return false;
    }

    // Read file content
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        INFLOG_ERROR("Failed to open text file: ", filePath);
        return false;
    }

    // Read up to MAX_PREVIEW_SIZE bytes
    size_t readSize = (std::min)(m_fileSize, MAX_PREVIEW_SIZE);
    m_content.resize(readSize);
    file.read(m_content.data(), readSize);
    m_truncated = m_fileSize > MAX_PREVIEW_SIZE;

    // Split into lines for display
    m_lines.clear();
    std::istringstream stream(m_content);
    std::string line;
    while (std::getline(stream, line)) {
        m_lines.push_back(line);
    }
    m_lineCount = m_lines.size();
    if (m_truncated) {
        // Estimate total lines
        m_lineCount = static_cast<size_t>(m_lineCount * (static_cast<double>(m_fileSize) / readSize));
    }

    m_loadedPath = filePath;
    m_loaded = true;
    INFLOG_INFO("Loaded text preview: ", filePath, " (", m_lines.size(), " lines)");
    return true;
}

void TextPreviewer::Render(InfGUIContext *ctx, float availWidth, float availHeight)
{
    // Use a child window for scrolling
    if (ctx->BeginChild("TextPreview", availWidth, availHeight, false)) {
        for (const auto &line : m_lines) {
            ctx->Label(line);
        }
        if (m_truncated) {
            ctx->Separator();
            ctx->Label("... (File truncated, showing first 64KB)");
        }
    }
    ctx->EndChild();
}

void TextPreviewer::Unload()
{
    m_loadedPath.clear();
    m_content.clear();
    m_lines.clear();
    m_fileSize = 0;
    m_lineCount = 0;
    m_truncated = false;
    m_loaded = false;
}

std::vector<std::pair<std::string, std::string>> TextPreviewer::GetMetadata() const
{
    std::vector<std::pair<std::string, std::string>> result;

    // Get extension for type
    size_t dotPos = m_loadedPath.rfind('.');
    std::string ext = dotPos != std::string::npos ? m_loadedPath.substr(dotPos) : "";
    result.emplace_back("Type", "Text (" + ext + ")");
    result.emplace_back("Lines", std::to_string(m_lineCount) + (m_truncated ? "+" : ""));

    // Format file size
    std::string sizeStr;
    if (m_fileSize < 1024) {
        sizeStr = std::to_string(m_fileSize) + " B";
    } else if (m_fileSize < 1024 * 1024) {
        sizeStr = std::to_string(m_fileSize / 1024) + " KB";
    } else {
        sizeStr = std::to_string(m_fileSize / (1024 * 1024)) + " MB";
    }
    result.emplace_back("Size", sizeStr);

    return result;
}

// ============================================================================
// BinaryPreviewer
// ============================================================================

std::vector<std::string> BinaryPreviewer::GetSupportedExtensions() const
{
    // Common binary files that we know exist but can't preview yet
    return {".exe", ".dll", ".so",  ".a",   ".lib",  ".obj", ".o",    ".pdb", ".zip",
            ".tar", ".gz",  ".7z",  ".rar", ".mp3",  ".wav", ".ogg",  ".mp4", ".avi",
            ".mov", ".mkv", ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx"};
}

bool BinaryPreviewer::Load(const std::string &filePath)
{
    if (m_loadedPath == filePath && m_loaded) {
        return true;
    }

    Unload();

    try {
        m_fileSize = std::filesystem::file_size(filePath);
    } catch (...) {
        return false;
    }

    m_loadedPath = filePath;
    m_loaded = true;
    return true;
}

void BinaryPreviewer::Render(InfGUIContext *ctx, float availWidth, float availHeight)
{
    ctx->Label("Binary file - preview not available");
    ctx->Label("Use an external program to open this file.");
}

void BinaryPreviewer::Unload()
{
    m_loadedPath.clear();
    m_fileSize = 0;
    m_loaded = false;
}

std::vector<std::pair<std::string, std::string>> BinaryPreviewer::GetMetadata() const
{
    std::vector<std::pair<std::string, std::string>> result;

    // Get extension for type
    size_t dotPos = m_loadedPath.rfind('.');
    std::string ext = dotPos != std::string::npos ? m_loadedPath.substr(dotPos) : "";
    result.emplace_back("Type", "Binary (" + ext + ")");

    // Format file size
    std::string sizeStr;
    if (m_fileSize < 1024) {
        sizeStr = std::to_string(m_fileSize) + " B";
    } else if (m_fileSize < 1024 * 1024) {
        sizeStr = std::to_string(m_fileSize / 1024) + " KB";
    } else {
        sizeStr = std::to_string(m_fileSize / (1024 * 1024)) + " MB";
    }
    result.emplace_back("Size", sizeStr);

    return result;
}

} // namespace infengine
