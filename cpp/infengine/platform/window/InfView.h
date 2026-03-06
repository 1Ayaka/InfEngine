#pragma once

#include <core/log/InfLog.h>
#include <core/types/InfApplication.h>

#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace infengine
{
class InfView
{
  public:
    friend class InfRenderer;

    InfView();

    InfView(const InfView &) = delete;
    InfView(InfView &&) = delete;
    InfView &operator=(const InfView &) = delete;
    InfView &operator=(InfView &&) = delete;

    const char *const *GetVkExtensions(uint32_t *count);

    void Init(int width, int height);
    void ProcessEvent();
    void Quit();

    int GetUserEvent();
    void Show();
    void Hide();
    void SetWindowIcon(const std::string &iconPath);

    /// Close-request interception: SDL_EVENT_QUIT sets this flag instead of
    /// immediately terminating.  Python checks the flag each frame and may
    /// show a "save before exit?" dialog before calling ConfirmClose().
    bool IsCloseRequested() const
    {
        return m_closeRequested;
    }
    void ConfirmClose()
    {
        m_keepRunning = false;
    }
    void CancelClose()
    {
        m_closeRequested = false;
    }

    bool IsMinimized() const
    {
        return m_isMinimized;
    }

    void CreateSurface(VkInstance *vkInstance, VkSurfaceKHR *vkSurface);
    void SetAppMetadata(InfAppMetadata appMetaData);

  private:
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    SDL_Window *m_window = nullptr;
    SDL_Renderer *m_sdlRenderer = nullptr;

    bool m_keepRunning;
    bool m_closeRequested = false;
    bool m_isMinimized = false;
    InfAppMetadata m_appMetadata;

    void SDLInit();
    void SDLQuit();
};
} // namespace infengine
