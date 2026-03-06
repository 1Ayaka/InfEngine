#include "InfView.h"

#include <iostream>

#include <imgui_impl_sdl3.h>
#include <platform/input/InputManager.h>
#include <stb_image.h>

namespace infengine
{
InfView::InfView()
{
}

const char *const *InfView::GetVkExtensions(uint32_t *count)
{
    INFLOG_DEBUG("Get Vulkan Extensions.");
    unsigned int extensionCount = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!extensions) {
        INFLOG_ERROR("SDL_Vulkan_GetInstanceExtensions failed: ", SDL_GetError());
        return nullptr;
    }
    if (count) {
        *count = extensionCount;
    }
    return extensions;
}

void InfView::Init(int width, int height)
{
    m_keepRunning = true;
    m_windowWidth = width;
    m_windowHeight = height;

    INFLOG_DEBUG("Initialize InfView Window with size: ", m_windowWidth, "x", m_windowHeight);
    SDLInit();
}

void InfView::ProcessEvent()
{
    // Begin a new input frame: swap current → previous, clear deltas
    InputManager::Instance().BeginFrame();

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        // Feed every event into the input manager
        InputManager::Instance().ProcessSDLEvent(event);

        if (event.type == SDL_EVENT_QUIT) {
            m_closeRequested = true;
            break;
        }

        // Track window minimized / restored / occluded so the renderer
        // can skip Vulkan draw calls when the window is not visible.
        if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
            m_isMinimized = true;
        }
        if (event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_EXPOSED ||
            event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            m_isMinimized = false;
        }
        if (event.type == SDL_EVENT_WINDOW_OCCLUDED) {
            m_isMinimized = true;
        }
    }
    SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
}

void InfView::Quit()
{
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    // Note: We intentionally don't call SDL_Quit() here to avoid
    // affecting other parts of the application (like a launcher).
    // SDL_Quit() would terminate all SDL subsystems which could
    // cause issues if the application continues running.
    INFLOG_DEBUG("Quit the InfView Window.");
}

int InfView::GetUserEvent()
{
    return m_keepRunning ? 1 : 0;
}

void InfView::Show()
{
    if (m_window) {
        SDL_ShowWindow(m_window);
    } else {
        INFLOG_ERROR("InfView Window is not initialized.");
    }
}

void InfView::Hide()
{
    if (m_window) {
        SDL_HideWindow(m_window);
    } else {
        INFLOG_ERROR("InfView Window is not initialized.");
    }
}

void InfView::SetWindowIcon(const std::string &iconPath)
{
    if (!m_window) {
        INFLOG_ERROR("Cannot set window icon: window not initialized.");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char *pixels = stbi_load(iconPath.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        INFLOG_ERROR("Failed to load icon: ", iconPath);
        return;
    }

    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    if (surface) {
        SDL_SetWindowIcon(m_window, surface);
        SDL_DestroySurface(surface);
        INFLOG_DEBUG("Window icon set from: ", iconPath);
    } else {
        INFLOG_ERROR("Failed to create SDL surface for icon: ", SDL_GetError());
    }

    stbi_image_free(pixels);
}

void InfView::SDLInit()
{
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        INFLOG_ERROR("SDL_Init failed: ", SDL_GetError());
    } else {
        INFLOG_DEBUG("SDL_Init succeeded.");
    }

    INFLOG_DEBUG("Window engine: SDL Vulkan");
    m_window = SDL_CreateWindow(m_appMetadata.appName, m_windowWidth, m_windowHeight,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!m_window) {
        INFLOG_ERROR("Could not create a window: ", SDL_GetError());
    } else {
        INFLOG_DEBUG("Window created successfully.");
    }

    SDL_MaximizeWindow(m_window);
}

void InfView::CreateSurface(VkInstance *vkInstance, VkSurfaceKHR *vkSurface)
{
    if (!SDL_Vulkan_CreateSurface(m_window, *vkInstance, nullptr, vkSurface)) {
        INFLOG_FATAL("Could not create Vulkan surface: ", SDL_GetError());
    } else {
        INFLOG_DEBUG("Vulkan surface created successfully.");
    }
}

void InfView::SetAppMetadata(InfAppMetadata appMetaData)
{
    m_appMetadata = appMetaData;
    INFLOG_DEBUG("Set InfView application metadata: ", m_appMetadata.appName);
}
} // namespace infengine
