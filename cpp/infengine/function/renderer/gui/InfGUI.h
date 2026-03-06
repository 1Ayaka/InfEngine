#pragma once

#include "InfVkCoreModular.h"
#include "gui/InfGUIContext.h"
#include "gui/InfGUIRenderable.h"
#include "gui/InfResourcePreviewer.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <vector>
#include <vulkan/vulkan.h>

namespace infengine
{

class InfGUI
{
  public:
    InfGUI(InfVkCoreModular *vkCore);
    ~InfGUI();

    void Init(SDL_Window *window);

    void SetGUIFont(const char *fontPath, float fontSize);
    void BuildFrame();

    void RecordCommand(VkCommandBuffer cmdBuf);
    void Shutdown();

    void Register(const std::string &name, std::shared_ptr<InfGUIRenderable> renderable);
    void Unregister(const std::string &name);

    /// @brief Upload texture data to GPU for use in ImGui
    /// @param name Unique identifier for the texture
    /// @param pixels RGBA pixel data
    /// @param width Texture width
    /// @param height Texture height
    /// @return Texture ID (VkDescriptorSet as uint64_t) for use in ImGui::Image
    uint64_t UploadTextureForImGui(const std::string &name, const unsigned char *pixels, int width, int height);

    /// @brief Remove a previously uploaded ImGui texture
    /// @param name Texture identifier
    void RemoveImGuiTexture(const std::string &name);

    /// @brief Check if a texture is already uploaded
    /// @param name Texture identifier
    /// @return true if texture exists
    bool HasImGuiTexture(const std::string &name) const;

    /// @brief Get texture ID for an already uploaded texture
    /// @param name Texture identifier
    /// @return Texture ID or 0 if not found
    uint64_t GetImGuiTextureId(const std::string &name) const;

    /// @brief Get the resource preview manager
    ResourcePreviewManager &GetResourcePreviewManager()
    {
        return m_resourcePreviewManager;
    }

  private:
    struct ImGuiTextureResource
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    InfVkCoreModular *m_vkCore_ptr = nullptr;
    ImGuiContext *m_imguiContext_ptr = nullptr;
    VkDescriptorPool m_descriptorPool_vk = VK_NULL_HANDLE;
    VkRenderPass m_imguiRenderPass = VK_NULL_HANDLE;

    std::unordered_map<std::string, std::shared_ptr<InfGUIRenderable>> m_renderables_umap;
    std::vector<std::string> m_renderableOrder;
    std::unordered_map<std::string, ImGuiTextureResource> m_textures_umap;
    ResourcePreviewManager m_resourcePreviewManager;
};

} // namespace infengine
