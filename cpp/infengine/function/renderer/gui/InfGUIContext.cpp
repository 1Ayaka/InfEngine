#include "InfGUIContext.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <type_traits>

namespace infengine
{

namespace
{
ImTextureID ToImTextureID(uint64_t textureId)
{
    if constexpr (std::is_pointer_v<ImTextureID>) {
        return (ImTextureID)(static_cast<uintptr_t>(textureId));
    }
    return static_cast<ImTextureID>(textureId);
}
} // namespace

/* basic text & labels */
void InfGUIContext::Label(const std::string &text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.c_str());
}

/* buttons / clickables */
void InfGUIContext::Button(const std::string &label, std::function<void()> onClick, float width, float height)
{
    if (ImGui::Button(label.c_str(), ImVec2(width, height)) && onClick)
        onClick();
}

bool InfGUIContext::RadioButton(const std::string &label, bool active)
{
    return ImGui::RadioButton(label.c_str(), active);
}

bool InfGUIContext::Selectable(const std::string &label, bool selected, int flags, float width, float height)
{
    return ImGui::Selectable(label.c_str(), selected, flags, ImVec2(width, height));
}

/* value editors */
void InfGUIContext::Checkbox(const std::string &label, bool *value)
{
    ImGui::Checkbox(label.c_str(), value);
}

void InfGUIContext::IntSlider(const std::string &label, int *value, int min, int max)
{
    ImGui::SliderInt(label.c_str(), value, min, max);
}

void InfGUIContext::FloatSlider(const std::string &label, float *value, float min, float max)
{
    ImGui::SliderFloat(label.c_str(), value, min, max);
}

bool InfGUIContext::DragFloat(const std::string &label, float *value, float speed, float min, float max,
                              const char *fmt, float power)
{
    CompensateWarp();
    bool changed = ImGui::DragFloat(label.c_str(), value, speed, min, max, fmt, power);
    HandleDragCapture();
    return changed;
}

bool InfGUIContext::DragInt(const std::string &label, int *value, float speed, int min, int max, const char *fmt)
{
    CompensateWarp();
    bool changed = ImGui::DragInt(label.c_str(), value, speed, min, max, fmt);
    HandleDragCapture();
    return changed;
}

void InfGUIContext::TextInput(const std::string &label, char *buffer, size_t bufferSize)
{
    ImGui::InputText(label.c_str(), buffer, bufferSize);
}

void InfGUIContext::TextArea(const std::string &label, char *buffer, size_t bufferSize)
{
    ImGui::InputTextMultiline(label.c_str(), buffer, bufferSize, ImVec2(-FLT_MIN, 100));
}

bool InfGUIContext::InputTextWithHint(const std::string &label, const std::string &hint, char *buffer,
                                      size_t bufferSize, int flags)
{
    return ImGui::InputTextWithHint(label.c_str(), hint.c_str(), buffer, bufferSize, flags);
}

bool InfGUIContext::InputInt(const std::string &label, int *value, int step, int stepFast, int flags)
{
    return ImGui::InputInt(label.c_str(), value, step, stepFast, flags);
}

bool InfGUIContext::InputFloat(const std::string &label, float *value, float step, float stepFast, int flags)
{
    return ImGui::InputFloat(label.c_str(), value, step, stepFast, "%.3f", static_cast<ImGuiInputTextFlags>(flags));
}
void InfGUIContext::ColorEdit(const std::string &label, float color[4])
{
    ImGui::ColorEdit4(label.c_str(), color);
}

// Unity-style helper: label on the left, DragFloatN on the right
static void LabeledDragFloatN(const char *label, float *value, int components, float speed, float labelWidth = 0.0f)
{
    if (labelWidth <= 0.0f)
        labelWidth = ImGui::CalcTextSize(label).x + 20.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail);
    std::string hiddenLabel = std::string("##") + label;
    switch (components) {
    case 2:
        ImGui::DragFloat2(hiddenLabel.c_str(), value, speed);
        break;
    case 3:
        ImGui::DragFloat3(hiddenLabel.c_str(), value, speed);
        break;
    case 4:
        ImGui::DragFloat4(hiddenLabel.c_str(), value, speed);
        break;
    default:
        ImGui::DragFloat(hiddenLabel.c_str(), value, speed);
        break;
    }
}

void InfGUIContext::Vector2Control(const std::string &label, float value[2], float speed, float labelWidth)
{
    CompensateWarp();
    LabeledDragFloatN(label.c_str(), value, 2, speed, labelWidth);
    HandleDragCapture();
}

void InfGUIContext::Vector3Control(const std::string &label, float value[3], float speed, float labelWidth)
{
    CompensateWarp();
    LabeledDragFloatN(label.c_str(), value, 3, speed, labelWidth);
    HandleDragCapture();
}

void InfGUIContext::Vector4Control(const std::string &label, float value[4], float speed, float labelWidth)
{
    CompensateWarp();
    LabeledDragFloatN(label.c_str(), value, 4, speed, labelWidth);
    HandleDragCapture();
}

/* combo & lists */
bool InfGUIContext::Combo(const std::string &label, int *currentItem, const std::vector<std::string> &items,
                          int popupMaxHeightInItems)
{
    std::vector<const char *> cstrs;
    cstrs.reserve(items.size());
    for (const auto &s : items)
        cstrs.push_back(s.c_str());
    return ImGui::Combo(label.c_str(), currentItem, cstrs.data(), static_cast<int>(cstrs.size()),
                        popupMaxHeightInItems);
}

bool InfGUIContext::ListBox(const std::string &label, int *currentItem, const std::vector<std::string> &items,
                            int heightInItems)
{
    std::vector<const char *> cstrs;
    cstrs.reserve(items.size());
    for (const auto &s : items)
        cstrs.push_back(s.c_str());
    return ImGui::ListBox(label.c_str(), currentItem, cstrs.data(), static_cast<int>(cstrs.size()), heightInItems);
}

/* progress & indicators */
void InfGUIContext::ProgressBar(float fraction, float width, float height, const std::string &overlay)
{
    ImGui::ProgressBar(fraction, ImVec2(width, height), overlay.c_str());
}

/* layout helpers */
void InfGUIContext::BeginGroup(const std::string &name)
{
    ImGui::BeginGroup();
    if (!name.empty())
        ImGui::TextUnformatted(name.c_str());
}

void InfGUIContext::EndGroup()
{
    ImGui::EndGroup();
}

void InfGUIContext::SameLine(float offsetFromStartX, float spacing)
{
    ImGui::SameLine(offsetFromStartX, spacing);
}

void InfGUIContext::AlignTextToFramePadding()
{
    ImGui::AlignTextToFramePadding();
}

void InfGUIContext::SetScrollHereY(float centerYRatio)
{
    ImGui::SetScrollHereY(centerYRatio);
}

float InfGUIContext::GetScrollY()
{
    return ImGui::GetScrollY();
}

float InfGUIContext::GetScrollMaxY()
{
    return ImGui::GetScrollMaxY();
}

void InfGUIContext::CloseCurrentPopup()
{
    ImGui::CloseCurrentPopup();
}

void InfGUIContext::Separator()
{
    ImGui::Separator();
}

void InfGUIContext::Spacing()
{
    ImGui::Spacing();
}

void InfGUIContext::Dummy(float width, float height)
{
    ImGui::Dummy(ImVec2(width, height));
}

void InfGUIContext::NewLine()
{
    ImGui::NewLine();
}

/* tree & collapsing */
bool InfGUIContext::TreeNode(const std::string &label)
{
    return ImGui::TreeNode(label.c_str());
}

bool InfGUIContext::TreeNodeEx(const std::string &label, int flags)
{
    return ImGui::TreeNodeEx(label.c_str(), static_cast<ImGuiTreeNodeFlags>(flags));
}

void InfGUIContext::TreePop()
{
    ImGui::TreePop();
}

void InfGUIContext::SetNextItemOpen(bool is_open, int cond)
{
    ImGui::SetNextItemOpen(is_open, cond);
}

void InfGUIContext::SetNextItemAllowOverlap()
{
    ImGui::SetNextItemAllowOverlap();
}

bool InfGUIContext::CollapsingHeader(const std::string &label)
{
    return ImGui::CollapsingHeader(label.c_str());
}

bool InfGUIContext::IsItemClicked(int mouseButton)
{
    return ImGui::IsItemClicked(static_cast<ImGuiMouseButton>(mouseButton));
}

/* tab bars */
bool InfGUIContext::BeginTabBar(const std::string &id)
{
    return ImGui::BeginTabBar(id.c_str());
}

void InfGUIContext::EndTabBar()
{
    ImGui::EndTabBar();
}

bool InfGUIContext::BeginTabItem(const std::string &label, bool *open)
{
    return ImGui::BeginTabItem(label.c_str(), open);
}

void InfGUIContext::EndTabItem()
{
    ImGui::EndTabItem();
}

/* main menu / menus */
bool InfGUIContext::BeginMainMenuBar()
{
    return ImGui::BeginMainMenuBar();
}

void InfGUIContext::EndMainMenuBar()
{
    ImGui::EndMainMenuBar();
}

bool InfGUIContext::BeginMenu(const std::string &label, bool enabled)
{
    return ImGui::BeginMenu(label.c_str(), enabled);
}

void InfGUIContext::EndMenu()
{
    ImGui::EndMenu();
}

bool InfGUIContext::MenuItem(const std::string &label, const std::string &shortcut, bool selected, bool enabled)
{
    return ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), selected, enabled);
}

/* child & windows */
bool InfGUIContext::BeginChild(const std::string &id, float width, float height, bool border)
{
    return ImGui::BeginChild(id.c_str(), ImVec2(width, height), border);
}

void InfGUIContext::EndChild()
{
    ImGui::EndChild();
}

/* popups & tooltips */
void InfGUIContext::OpenPopup(const std::string &id)
{
    ImGui::OpenPopup(id.c_str());
}

bool InfGUIContext::BeginPopup(const std::string &id)
{
    return ImGui::BeginPopup(id.c_str());
}

bool InfGUIContext::BeginPopupModal(const std::string &title, int flags)
{
    return ImGui::BeginPopupModal(title.c_str(), nullptr, static_cast<ImGuiWindowFlags>(flags));
}

bool InfGUIContext::BeginPopupContextItem(const std::string &id, int mouseButton)
{
    return ImGui::BeginPopupContextItem(id.empty() ? nullptr : id.c_str(), static_cast<ImGuiPopupFlags>(mouseButton));
}

bool InfGUIContext::BeginPopupContextWindow(const std::string &id, int mouseButton)
{
    return ImGui::BeginPopupContextWindow(id.empty() ? nullptr : id.c_str(), static_cast<ImGuiPopupFlags>(mouseButton));
}

void InfGUIContext::EndPopup()
{
    ImGui::EndPopup();
}

void InfGUIContext::BeginTooltip()
{
    ImGui::BeginTooltip();
}

void InfGUIContext::EndTooltip()
{
    ImGui::EndTooltip();
}

void InfGUIContext::SetTooltip(const std::string &text)
{
    ImGui::SetTooltip("%s", text.c_str());
}

/* images */
void InfGUIContext::Image(void *textureId, float width, float height, float uv0_x, float uv0_y, float uv1_x,
                          float uv1_y)
{
    ImGui::Image(reinterpret_cast<ImTextureID>(textureId), ImVec2(width, height), ImVec2(uv0_x, uv0_y),
                 ImVec2(uv1_x, uv1_y));
}

bool InfGUIContext::ImageButton(const std::string &id, void *textureId, float width, float height, float uv0_x,
                                float uv0_y, float uv1_x, float uv1_y)
{
    return ImGui::ImageButton(id.c_str(), reinterpret_cast<ImTextureID>(textureId), ImVec2(width, height),
                              ImVec2(uv0_x, uv0_y), ImVec2(uv1_x, uv1_y));
}

/* tables */
bool InfGUIContext::BeginTable(const std::string &id, int columns, int flags, float innerWidth)
{
    return ImGui::BeginTable(id.c_str(), columns, flags, ImVec2(innerWidth, 0));
}

void InfGUIContext::EndTable()
{
    ImGui::EndTable();
}

void InfGUIContext::TableSetupColumn(const std::string &label, int flags, float initWidthOrWeight, int userID)
{
    ImGui::TableSetupColumn(label.c_str(), flags, initWidthOrWeight, userID);
}

void InfGUIContext::TableHeadersRow()
{
    ImGui::TableHeadersRow();
}

void InfGUIContext::TableNextRow()
{
    ImGui::TableNextRow();
}

void InfGUIContext::TableSetColumnIndex(int columnIndex)
{
    ImGui::TableSetColumnIndex(columnIndex);
}

bool InfGUIContext::TableNextColumn()
{
    return ImGui::TableNextColumn();
}

/* misc helpers */
bool InfGUIContext::CheckboxFlags(const std::string &label, unsigned int *flags, unsigned int flagValue)
{
    return ImGui::CheckboxFlags(label.c_str(), flags, flagValue);
}

void InfGUIContext::SetNextItemWidth(float width)
{
    ImGui::SetNextItemWidth(width);
}

void InfGUIContext::SetNextWindowSize(float width, float height, int cond)
{
    ImGui::SetNextWindowSize(ImVec2(width, height), static_cast<ImGuiCond>(cond));
}

void InfGUIContext::SetNextWindowPos(float x, float y, int cond, float pivot_x, float pivot_y)
{
    ImGui::SetNextWindowPos(ImVec2(x, y), static_cast<ImGuiCond>(cond), ImVec2(pivot_x, pivot_y));
}

void InfGUIContext::SetNextWindowFocus()
{
    ImGui::SetNextWindowFocus();
}

bool InfGUIContext::BeginWindow(const std::string &name, bool *open, int flags)
{
    return ImGui::Begin(name.c_str(), open, flags);
}

void InfGUIContext::EndWindow()
{
    ImGui::End();
}

/* layout query */
float InfGUIContext::GetContentRegionAvailWidth()
{
    return ImGui::GetContentRegionAvail().x;
}

float InfGUIContext::GetContentRegionAvailHeight()
{
    return ImGui::GetContentRegionAvail().y;
}

float InfGUIContext::GetCursorPosX()
{
    return ImGui::GetCursorPosX();
}

float InfGUIContext::GetCursorPosY()
{
    return ImGui::GetCursorPosY();
}

void InfGUIContext::SetCursorPosX(float x)
{
    ImGui::SetCursorPosX(x);
}

void InfGUIContext::SetCursorPosY(float y)
{
    ImGui::SetCursorPosY(y);
}

float InfGUIContext::GetWindowPosX()
{
    return ImGui::GetWindowPos().x;
}

float InfGUIContext::GetWindowPosY()
{
    return ImGui::GetWindowPos().y;
}

float InfGUIContext::CalcTextWidth(const std::string &text)
{
    return ImGui::CalcTextSize(text.c_str()).x;
}

float InfGUIContext::GetWindowWidth()
{
    return ImGui::GetWindowWidth();
}

float InfGUIContext::GetItemRectMinX()
{
    return ImGui::GetItemRectMin().x;
}

float InfGUIContext::GetItemRectMinY()
{
    return ImGui::GetItemRectMin().y;
}

float InfGUIContext::GetItemRectMaxX()
{
    return ImGui::GetItemRectMax().x;
}

float InfGUIContext::GetItemRectMaxY()
{
    return ImGui::GetItemRectMax().y;
}

/* invisible button (for splitter) */
bool InfGUIContext::InvisibleButton(const std::string &id, float width, float height)
{
    return ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
}

bool InfGUIContext::IsItemActive()
{
    return ImGui::IsItemActive();
}

bool InfGUIContext::IsItemHovered()
{
    return ImGui::IsItemHovered();
}

/* focus & activation */
void InfGUIContext::SetKeyboardFocusHere(int offset)
{
    ImGui::SetKeyboardFocusHere(offset);
}

bool InfGUIContext::IsItemDeactivated()
{
    return ImGui::IsItemDeactivated();
}

bool InfGUIContext::IsItemDeactivatedAfterEdit()
{
    return ImGui::IsItemDeactivatedAfterEdit();
}

float InfGUIContext::GetMouseDragDeltaY(int button)
{
    // Use 0.0f threshold for immediate response (no lock threshold)
    return ImGui::GetMouseDragDelta(button, 0.0f).y;
}

void InfGUIContext::ResetMouseDragDelta(int button)
{
    ImGui::ResetMouseDragDelta(button);
}

/* ID stack */
void InfGUIContext::PushID(int id)
{
    ImGui::PushID(id);
}

void InfGUIContext::PushID(const std::string &id)
{
    ImGui::PushID(id.c_str());
}

void InfGUIContext::PopID()
{
    ImGui::PopID();
}

void InfGUIContext::PushStyleColor(int idx, float r, float g, float b, float a)
{
    ImGui::PushStyleColor(static_cast<ImGuiCol_>(idx), ImVec4(r, g, b, a));
}

void InfGUIContext::PopStyleColor(int count)
{
    ImGui::PopStyleColor(count);
}

void InfGUIContext::PushStyleVarFloat(int idx, float val)
{
    ImGui::PushStyleVar(static_cast<ImGuiStyleVar_>(idx), val);
}

void InfGUIContext::PushStyleVarVec2(int idx, float x, float y)
{
    ImGui::PushStyleVar(static_cast<ImGuiStyleVar_>(idx), ImVec2(x, y));
}

void InfGUIContext::PopStyleVar(int count)
{
    ImGui::PopStyleVar(count);
}

void InfGUIContext::BeginDisabled(bool disabled)
{
    ImGui::BeginDisabled(disabled);
}

void InfGUIContext::EndDisabled()
{
    ImGui::EndDisabled();
}

/* Drag and Drop */
bool InfGUIContext::BeginDragDropSource(int flags)
{
    return ImGui::BeginDragDropSource(static_cast<ImGuiDragDropFlags>(flags));
}

bool InfGUIContext::SetDragDropPayload(const std::string &type, uint64_t data)
{
    return ImGui::SetDragDropPayload(type.c_str(), &data, sizeof(data));
}

bool InfGUIContext::SetDragDropPayload(const std::string &type, const std::string &data)
{
    return ImGui::SetDragDropPayload(type.c_str(), data.c_str(), data.size() + 1);
}

void InfGUIContext::EndDragDropSource()
{
    ImGui::EndDragDropSource();
}

bool InfGUIContext::BeginDragDropTarget()
{
    return ImGui::BeginDragDropTarget();
}

bool InfGUIContext::AcceptDragDropPayload(const std::string &type, uint64_t *outData)
{
    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(type.c_str());
    if (payload && payload->DataSize == sizeof(uint64_t)) {
        *outData = *static_cast<const uint64_t *>(payload->Data);
        return true;
    }
    return false;
}

bool InfGUIContext::AcceptDragDropPayload(const std::string &type, std::string *outData)
{
    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(type.c_str());
    if (payload && payload->DataSize > 0) {
        *outData = std::string(static_cast<const char *>(payload->Data), payload->DataSize - 1);
        return true;
    }
    return false;
}

void InfGUIContext::EndDragDropTarget()
{
    ImGui::EndDragDropTarget();
}

void InfGUIContext::SetMouseCursor(int cursorType)
{
    ImGui::SetMouseCursor(static_cast<ImGuiMouseCursor>(cursorType));
}

// ========================================================================
// Scene View Input API implementation
// ========================================================================

bool InfGUIContext::IsMouseButtonDown(int button)
{
    return ImGui::IsMouseDown(button);
}

bool InfGUIContext::IsMouseButtonClicked(int button)
{
    return ImGui::IsMouseClicked(button);
}

bool InfGUIContext::IsMouseDoubleClicked(int button)
{
    return ImGui::IsMouseDoubleClicked(button);
}

bool InfGUIContext::IsMouseDragging(int button, float lockThreshold)
{
    return ImGui::IsMouseDragging(button, lockThreshold);
}

float InfGUIContext::GetMouseDragDeltaX(int button)
{
    // Use 0.0f threshold for immediate response (no lock threshold)
    return ImGui::GetMouseDragDelta(button, 0.0f).x;
}

float InfGUIContext::GetMousePosX()
{
    return ImGui::GetMousePos().x;
}

float InfGUIContext::GetMousePosY()
{
    return ImGui::GetMousePos().y;
}

float InfGUIContext::GetMouseWheelDelta()
{
    return ImGui::GetIO().MouseWheel;
}

bool InfGUIContext::IsKeyDown(int keyCode)
{
    return ImGui::IsKeyDown(static_cast<ImGuiKey>(keyCode));
}

bool InfGUIContext::IsKeyPressed(int keyCode)
{
    return ImGui::IsKeyPressed(static_cast<ImGuiKey>(keyCode));
}

bool InfGUIContext::IsKeyReleased(int keyCode)
{
    return ImGui::IsKeyReleased(static_cast<ImGuiKey>(keyCode));
}

bool InfGUIContext::IsWindowFocused(int flags)
{
    return ImGui::IsWindowFocused(flags);
}

bool InfGUIContext::IsWindowHovered(int flags)
{
    return ImGui::IsWindowHovered(flags);
}

bool InfGUIContext::WantTextInput()
{
    return ImGui::GetIO().WantTextInput;
}

void InfGUIContext::CaptureMouseFromApp(bool capture)
{
    ImGui::GetIO().WantCaptureMouse = capture;
}

void InfGUIContext::CaptureKeyboardFromApp(bool capture)
{
    ImGui::GetIO().WantCaptureKeyboard = capture;
}

void InfGUIContext::WarpMouseGlobal(float x, float y)
{
    SDL_WarpMouseGlobal(x, y);
}

float InfGUIContext::GetGlobalMousePosX()
{
    float gx = 0, gy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    return gx;
}

float InfGUIContext::GetGlobalMousePosY()
{
    float gx = 0, gy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    return gy;
}

void InfGUIContext::GetMainViewportBounds(float *x, float *y, float *w, float *h)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    *x = vp->Pos.x;
    *y = vp->Pos.y;
    *w = vp->Size.x;
    *h = vp->Size.y;
}

void InfGUIContext::SetClipboardText(const std::string &text)
{
    ImGui::SetClipboardText(text.c_str());
}

std::string InfGUIContext::GetClipboardText()
{
    const char *t = ImGui::GetClipboardText();
    return t ? std::string(t) : std::string();
}

void InfGUIContext::InputTextMultiline(const std::string &label, const std::string &text, float width, float height,
                                       int flags)
{
    // We use a temporary buffer so ImGui can render selection / scrolling,
    // but the content is effectively read-only (ImGuiInputTextFlags_ReadOnly = 1 << 14).
    std::vector<char> buf(text.size() + 1, 0);
    std::copy(text.begin(), text.end(), buf.begin());
    ImGui::InputTextMultiline(label.c_str(), buf.data(), buf.size(), ImVec2(width, height), flags);
}

void InfGUIContext::GetDisplayBounds(float *x, float *y, float *w, float *h)
{
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (displays && count > 0) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(displays[0], &bounds)) {
            *x = static_cast<float>(bounds.x);
            *y = static_cast<float>(bounds.y);
            *w = static_cast<float>(bounds.w);
            *h = static_cast<float>(bounds.h);
            SDL_free(displays);
            return;
        }
        SDL_free(displays);
    }
    // Fallback
    *x = 0;
    *y = 0;
    *w = 1920;
    *h = 1080;
}

// ==========================================================================
// Infinite drag — warp cursor to opposite screen edge when it hits a
// boundary, giving a Unity-style infinite-drag feel for DragFloat etc.
// Does NOT use SDL relative mouse mode (that conflicts with ImGui).
// ==========================================================================

void InfGUIContext::HandleDragCapture()
{
    const bool active = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

    if (active && !m_dragCaptured) {
        // Just started dragging
        m_dragCaptured = true;
    }

    if (active) {
        // Warp cursor to opposite edge when hitting screen boundary
        float mx, my;
        SDL_GetGlobalMouseState(&mx, &my);

        SDL_DisplayID did = SDL_GetPrimaryDisplay();
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(did, &bounds)) {
            const float margin = 2.0f;
            const float left = static_cast<float>(bounds.x) + margin;
            const float right = static_cast<float>(bounds.x + bounds.w) - margin;
            const float top = static_cast<float>(bounds.y) + margin;
            const float bottom = static_cast<float>(bounds.y + bounds.h) - margin;

            float newMx = mx, newMy = my;
            bool warped = false;
            if (mx <= left) {
                newMx = right - margin;
                warped = true;
            } else if (mx >= right) {
                newMx = left + margin;
                warped = true;
            }
            if (my <= top) {
                newMy = bottom - margin;
                warped = true;
            } else if (my >= bottom) {
                newMy = top + margin;
                warped = true;
            }

            if (warped) {
                SDL_WarpMouseGlobal(newMx, newMy);
                // Ignore two frames of mouse delta after warp to prevent
                // artificial value jumps in DragFloat / DragInt.
                // (Some backends report the warp jump on the next frame,
                // others one frame later.)
                m_ignoreMouseDeltaFrames = 2;
            }
        }
    } else if (m_dragCaptured) {
        // Drag ended — do NOT snap cursor back to start position.
        // Snap-back causes visible stutter and introduces artificial deltas.
        m_dragCaptured = false;
    }
}

void InfGUIContext::CompensateWarp()
{
    ImGuiIO &io = ImGui::GetIO();

    if (m_ignoreMouseDeltaFrames > 0) {
        // Drop delta right after warp (teleport artifact).
        io.MouseDelta = ImVec2(0.0f, 0.0f);
        --m_ignoreMouseDeltaFrames;
        return;
    }

    // Safety net (same idea as Scene panel): ignore implausibly large deltas
    // during drag, which are almost always edge-wrap teleport artifacts.
    constexpr float kWarpJumpThreshold = 400.0f;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        (std::fabs(io.MouseDelta.x) > kWarpJumpThreshold || std::fabs(io.MouseDelta.y) > kWarpJumpThreshold)) {
        io.MouseDelta = ImVec2(0.0f, 0.0f);
    }
}

void InfGUIContext::SetWindowFontScale(float scale)
{
    ImGui::SetWindowFontScale(scale);
}

void InfGUIContext::DrawRect(float minX, float minY, float maxX, float maxY, float r, float g, float b, float a,
                             float thickness, float rounding)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (drawList) {
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
        drawList->AddRect(ImVec2(minX, minY), ImVec2(maxX, maxY), col, rounding, 0, thickness);
    }
}

void InfGUIContext::DrawFilledRect(float minX, float minY, float maxX, float maxY, float r, float g, float b, float a,
                                   float rounding)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (drawList) {
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
        drawList->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY), col, rounding);
    }
}

void InfGUIContext::DrawImageRect(uint64_t textureId, float minX, float minY, float maxX, float maxY, float uv0_x,
                                  float uv0_y, float uv1_x, float uv1_y, float tintR, float tintG, float tintB,
                                  float tintA)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (drawList) {
        ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(tintR, tintG, tintB, tintA));
        drawList->AddImage(ToImTextureID(textureId), ImVec2(minX, minY), ImVec2(maxX, maxY), ImVec2(uv0_x, uv0_y),
                           ImVec2(uv1_x, uv1_y), tint);
    }
}

void InfGUIContext::DrawText(float x, float y, const std::string &text, float r, float g, float b, float a,
                             float fontSize)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (!drawList)
        return;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    ImFont *font = ImGui::GetFont();
    float size = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
    drawList->AddText(font, size, ImVec2(x, y), col, text.c_str());
}

void InfGUIContext::DrawTextAligned(float minX, float minY, float maxX, float maxY, const std::string &text, float r,
                                    float g, float b, float a, float alignX, float alignY, float fontSize, bool clip)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (!drawList)
        return;

    ImFont *font = ImGui::GetFont();
    float size = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
    ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());

    float boxW = maxX - minX;
    float boxH = maxY - minY;
    float textX = minX + (boxW - textSize.x) * alignX;
    float textY = minY + (boxH - textSize.y) * alignY;

    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));

    if (clip)
        drawList->PushClipRect(ImVec2(minX, minY), ImVec2(maxX, maxY), true);

    drawList->AddText(font, size, ImVec2(textX, textY), col, text.c_str());

    if (clip)
        drawList->PopClipRect();
}

std::pair<float, float> InfGUIContext::CalcTextSizeA(const std::string &text, float fontSize)
{
    ImFont *font = ImGui::GetFont();
    float size = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    return {ts.x, ts.y};
}

void InfGUIContext::PushDrawListClipRect(float minX, float minY, float maxX, float maxY, bool intersectWithCurrent)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (drawList)
        drawList->PushClipRect(ImVec2(minX, minY), ImVec2(maxX, maxY), intersectWithCurrent);
}

void InfGUIContext::PopDrawListClipRect()
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    if (drawList)
        drawList->PopClipRect();
}

} // namespace infengine
