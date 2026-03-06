"""
Unified ImGui Theme for InfEngine Editor.

Centralises ALL colour and style constants that were previously hardcoded
across toolbar_panel, scene_view_panel, game_view_panel, console_panel,
status_bar, project_panel, tag_layer_settings, hierarchy_panel, etc.

Usage — in any UI panel::

    from InfEngine.engine.ui.theme import Theme, ImGuiCol

    ctx.push_style_color(ImGuiCol.Button,        *Theme.BTN_NORMAL)
    ctx.push_style_color(ImGuiCol.ButtonHovered,  *Theme.BTN_HOVERED)
    ctx.push_style_color(ImGuiCol.ButtonActive,   *Theme.BTN_ACTIVE)
    ...
    ctx.pop_style_color(3)

All colours are in **linear** space (matching ImGui with sRGB framebuffer).
"""

from __future__ import annotations
from typing import Tuple

# Convenience type alias  (R, G, B, A) all float [0..1]
RGBA = Tuple[float, float, float, float]


# ---------------------------------------------------------------------------
#  sRGB → Linear helper (identical to the per-file copies it replaces)
# ---------------------------------------------------------------------------

def srgb_to_linear(s: float) -> float:
    """Convert a single sRGB [0,1] component to linear space."""
    if s <= 0.04045:
        return s / 12.92
    return ((s + 0.055) / 1.055) ** 2.4


def srgb3(r: float, g: float, b: float, a: float = 1.0) -> RGBA:
    """Convert sRGB 0-1 components to linear RGBA tuple."""
    return (srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), a)


def hex_to_linear(hex_r: int, hex_g: int, hex_b: int, a: float = 1.0) -> RGBA:
    """Convert 0-255 sRGB hex components to linear RGBA tuple."""
    return srgb3(hex_r / 255.0, hex_g / 255.0, hex_b / 255.0, a)


# ============================================================================
#  ImGuiCol_ index constants  (avoids magic numbers in every panel)
# ============================================================================

class ImGuiCol:
    """ImGuiCol enum indices — must match imgui.h ImGuiCol_ order exactly."""
    Text                       = 0
    TextDisabled               = 1
    WindowBg                   = 2
    ChildBg                    = 3
    PopupBg                    = 4
    Border                     = 5
    BorderShadow               = 6
    FrameBg                    = 7
    FrameBgHovered             = 8
    FrameBgActive              = 9
    TitleBg                    = 10
    TitleBgActive              = 11
    TitleBgCollapsed           = 12
    MenuBarBg                  = 13
    ScrollbarBg                = 14
    ScrollbarGrab              = 15
    ScrollbarGrabHovered       = 16
    ScrollbarGrabActive        = 17
    CheckMark                  = 18
    SliderGrab                 = 19
    SliderGrabActive           = 20
    Button                     = 21
    ButtonHovered              = 22
    ButtonActive               = 23
    Header                     = 24
    HeaderHovered              = 25
    HeaderActive               = 26
    Separator                  = 27
    SeparatorHovered           = 28
    SeparatorActive            = 29
    ResizeGrip                 = 30
    ResizeGripHovered          = 31
    ResizeGripActive           = 32
    InputTextCursor            = 33
    TabHovered                 = 34
    Tab                        = 35
    TabSelected                = 36
    TabSelectedOverline        = 37
    TabDimmed                  = 38
    TabDimmedSelected          = 39
    TabDimmedSelectedOverline  = 40
    DockingPreview             = 41
    DockingEmptyBg             = 42
    PlotLines                  = 43
    PlotLinesHovered           = 44
    PlotHistogram              = 45
    PlotHistogramHovered       = 46
    TableHeaderBg              = 47
    TableBorderStrong          = 48
    TableBorderLight           = 49
    TableRowBg                 = 50
    TableRowBgAlt              = 51
    TextLink                   = 52
    TextSelectedBg             = 53
    TreeLines                  = 54
    DragDropTarget             = 55
    DragDropTargetBg           = 56
    UnsavedMarker              = 57
    NavCursor                  = 58
    NavWindowingHighlight      = 59
    NavWindowingDimBg          = 60
    ModalWindowDimBg           = 61


class ImGuiWindowFlags:
    """ImGuiWindowFlags enum — must match imgui.h ImGuiWindowFlags_ order exactly."""
    NoTitleBar                  = 1 << 0    # 1
    NoResize                    = 1 << 1    # 2
    NoMove                      = 1 << 2    # 4
    NoScrollbar                 = 1 << 3    # 8
    NoScrollWithMouse           = 1 << 4    # 16
    NoCollapse                  = 1 << 5    # 32
    AlwaysAutoResize            = 1 << 6    # 64
    NoBackground                = 1 << 7    # 128
    NoSavedSettings             = 1 << 11   # 2048
    NoFocusOnAppearing          = 1 << 12   # 4096
    NoBringToFrontOnFocus       = 1 << 13   # 8192
    NoDocking                   = 1 << 18   # 262144
    NoNav                       = 1 << 19   # 524288  (NoNavInputs | NoNavFocus)
    NoDecoration                = NoTitleBar | NoResize | NoScrollbar | NoCollapse
    NoInputs                    = 1 << 20   # 1048576


class ImGuiTreeNodeFlags:
    """ImGuiTreeNodeFlags enum — must match imgui.h ImGuiTreeNodeFlags_ order exactly."""
    Selected                    = 1 << 0    # 1
    Framed                      = 1 << 1    # 2
    AllowOverlap                = 1 << 2    # 4
    NoTreePushOnOpen            = 1 << 3    # 8
    NoAutoOpenOnLog             = 1 << 4    # 16
    DefaultOpen                 = 1 << 5    # 32
    OpenOnDoubleClick           = 1 << 6    # 64
    OpenOnArrow                 = 1 << 7    # 128
    Leaf                        = 1 << 8    # 256
    Bullet                      = 1 << 9    # 512
    FramePadding                = 1 << 10   # 1024
    SpanAvailWidth              = 1 << 11   # 2048
    SpanFullWidth               = 1 << 12   # 4096
    SpanAllColumns              = 1 << 13   # 8192
    CollapsingHeader            = Framed | NoTreePushOnOpen | NoAutoOpenOnLog


class ImGuiStyleVar:
    """ImGuiStyleVar enum indices — must match imgui.h ImGuiStyleVar_ order exactly."""
    Alpha                       = 0
    DisabledAlpha               = 1
    WindowPadding               = 2
    WindowRounding              = 3
    WindowBorderSize            = 4
    WindowMinSize               = 5
    WindowTitleAlign            = 6
    ChildRounding               = 7
    ChildBorderSize             = 8
    PopupRounding               = 9
    PopupBorderSize             = 10
    FramePadding                = 11
    FrameRounding               = 12
    FrameBorderSize             = 13
    ItemSpacing                 = 14
    ItemInnerSpacing            = 15
    IndentSpacing               = 16
    CellPadding                 = 17
    ScrollbarSize               = 18
    ScrollbarRounding           = 19
    ScrollbarPadding            = 20
    GrabMinSize                 = 21
    GrabRounding                = 22
    ImageBorderSize             = 23
    TabRounding                 = 24
    TabBorderSize               = 25
    TabMinWidthBase             = 26
    TabMinWidthShrink           = 27
    TabBarBorderSize            = 28
    TabBarOverlineSize          = 29
    TableAngledHeadersAngle     = 30
    TableAngledHeadersTextAlign = 31
    TreeLinesSize               = 32
    TreeLinesRounding           = 33
    ButtonTextAlign             = 34
    SelectableTextAlign         = 35
    SeparatorTextBorderSize     = 36
    SeparatorTextAlign          = 37
    SeparatorTextPadding        = 38
    DockingSeparatorSize        = 39


# ============================================================================
#  Theme — the single source of truth for editor styling
# ============================================================================

class Theme:
    """
    Central theme constants for the InfEngine Editor UI.

    All colour values are **linear-space RGBA tuples** (float, 0-1).
    Panels should reference these instead of hardcoding numbers.
    """

    # ------------------------------------------------------------------
    #  Base palette (Notion-inspired dark theme)
    # ------------------------------------------------------------------

    # Text
    TEXT              : RGBA = (0.812, 0.812, 0.812, 1.0)
    TEXT_DISABLED     : RGBA = (0.50,  0.50,  0.50,  1.0)
    TEXT_DIM          : RGBA = (0.60,  0.60,  0.60,  1.0)    # e.g. reserved layer names

    # Backgrounds
    WINDOW_BG         : RGBA = srgb3(0.11, 0.11, 0.11)
    CHILD_BG          : RGBA = (0.0, 0.0, 0.0, 0.0)
    POPUP_BG          : RGBA = srgb3(0.12, 0.12, 0.12, 0.96)
    STATUS_BAR_BG     : RGBA = (0.010, 0.010, 0.010, 1.0)

    # Borders
    BORDER            : RGBA = srgb3(0.20, 0.20, 0.20)
    BORDER_TRANSPARENT: RGBA = (0.0, 0.0, 0.0, 0.0)
    BORDER_SHADOW     : RGBA = (0.0, 0.0, 0.0, 0.0)

    # Frames (input fields, sliders)
    FRAME_BG          : RGBA = srgb3(0.14, 0.14, 0.14)
    FRAME_BG_HOVERED  : RGBA = srgb3(0.18, 0.18, 0.18)
    FRAME_BG_ACTIVE   : RGBA = srgb3(0.22, 0.22, 0.22)

    # ------------------------------------------------------------------
    #  Buttons
    # ------------------------------------------------------------------

    # Regular button
    BTN_NORMAL        : RGBA = srgb3(0.16, 0.16, 0.16)
    BTN_HOVERED       : RGBA = srgb3(0.20, 0.20, 0.20)
    BTN_ACTIVE        : RGBA = srgb3(0.24, 0.24, 0.24)

    # Ghost / transparent button (toolbar, status bar)
    BTN_GHOST         : RGBA = (0.0, 0.0, 0.0, 0.0)
    BTN_GHOST_HOVERED : RGBA = (0.18, 0.18, 0.18, 1.0)
    BTN_GHOST_ACTIVE  : RGBA = (0.22, 0.22, 0.22, 1.0)

    # Status-bar ghost button (slightly different hover shade)
    BTN_SB_HOVERED    : RGBA = (0.15, 0.15, 0.15, 1.0)
    BTN_SB_ACTIVE     : RGBA = (0.18, 0.18, 0.18, 1.0)

    # Selected item highlight (e.g. project file grid)
    BTN_SELECTED      : RGBA = (0.200, 0.200, 0.200, 1.0)

    # ------------------------------------------------------------------
    #  Headers / Tree nodes / Selectables
    # ------------------------------------------------------------------

    HEADER            : RGBA = srgb3(0.14, 0.14, 0.14)
    HEADER_HOVERED    : RGBA = srgb3(0.18, 0.18, 0.18)
    HEADER_ACTIVE     : RGBA = srgb3(0.22, 0.22, 0.22)
    SELECTION_BG      : RGBA = srgb3(0.086, 0.086, 0.086)

    # ------------------------------------------------------------------
    #  Console / Log level colours
    # ------------------------------------------------------------------

    LOG_INFO          : RGBA = (0.812, 0.812, 0.812, 1.0)
    LOG_WARNING       : RGBA = (0.890, 0.710, 0.300, 1.0)
    LOG_ERROR         : RGBA = (0.922, 0.341, 0.341, 1.0)
    LOG_TRACE         : RGBA = (0.50,  0.50,  0.50,  1.0)
    LOG_BADGE         : RGBA = (0.55,  0.55,  0.55,  1.0)
    LOG_DIM           : RGBA = (0.133, 0.133, 0.133, 0.6)

    # ------------------------------------------------------------------
    #  Play-mode viewport borders
    # ------------------------------------------------------------------

    BORDER_PLAY       : RGBA = hex_to_linear(0x03, 0xDE, 0x6D)   # #03de6d green
    BORDER_PAUSE      : RGBA = hex_to_linear(0xEB, 0x57, 0x57)   # #eb5757 red
    BORDER_THICKNESS  : float = 2.0

    # ------------------------------------------------------------------
    #  Toolbar compact spacing preset
    # ------------------------------------------------------------------

    TOOLBAR_WIN_PAD   = (4.0, 4.0)
    TOOLBAR_FRAME_PAD = (6.0, 4.0)
    TOOLBAR_ITEM_SPC  = (6.0, 4.0)
    TOOLBAR_FRAME_RND = 3.0
    TOOLBAR_FRAME_BRD = 0.0

    # Popup (Gizmos / Camera dropdown) spacing
    POPUP_WIN_PAD     = (16.0, 12.0)
    POPUP_ITEM_SPC    = (10.0, 8.0)
    POPUP_FRAME_PAD   = (8.0, 6.0)

    # ------------------------------------------------------------------
    #  Window flags (commonly used combos)
    # ------------------------------------------------------------------

    WINDOW_FLAGS_VIEWPORT  = (ImGuiWindowFlags.NoFocusOnAppearing
                              | ImGuiWindowFlags.NoBringToFrontOnFocus)
    WINDOW_FLAGS_NO_SCROLL = (ImGuiWindowFlags.NoScrollbar
                              | ImGuiWindowFlags.NoScrollWithMouse)
    WINDOW_FLAGS_NO_DECOR  = (ImGuiWindowFlags.NoTitleBar
                              | ImGuiWindowFlags.NoResize
                              | ImGuiWindowFlags.NoMove
                              | ImGuiWindowFlags.NoScrollbar
                              | ImGuiWindowFlags.NoScrollWithMouse
                              | ImGuiWindowFlags.NoSavedSettings
                              | ImGuiWindowFlags.NoFocusOnAppearing
                              | ImGuiWindowFlags.NoDocking
                              | ImGuiWindowFlags.NoInputs)
    WINDOW_FLAGS_FLOATING  = (ImGuiWindowFlags.NoCollapse
                              | ImGuiWindowFlags.NoSavedSettings)

    # ------------------------------------------------------------------
    #  ImGuiCond helpers
    # ------------------------------------------------------------------

    COND_FIRST_USE_EVER = 4
    COND_ALWAYS         = 1

    # ------------------------------------------------------------------
    #  Border size constants
    # ------------------------------------------------------------------

    BORDER_SIZE_NONE    = 0.0

    # ------------------------------------------------------------------
    #  Toolbar play-mode button presets
    # ------------------------------------------------------------------

    PLAY_ACTIVE       : RGBA = (0.20, 0.45, 0.30, 1.0)     # green tint — Play active
    PAUSE_ACTIVE      : RGBA = (0.50, 0.40, 0.15, 1.0)     # amber tint — Pause active
    BTN_IDLE          : RGBA = (0.165, 0.165, 0.165, 1.0)   # neutral toolbar button
    BTN_DISABLED      : RGBA = (0.10, 0.10, 0.10, 0.4)     # greyed-out / disabled

    # ------------------------------------------------------------------
    #  Console splitter colours
    # ------------------------------------------------------------------

    SPLITTER_HOVER    : RGBA = (0.3, 0.3, 0.3, 0.6)
    SPLITTER_ACTIVE   : RGBA = (0.3, 0.3, 0.3, 0.8)

    # ------------------------------------------------------------------
    #  Console alternating row background
    # ------------------------------------------------------------------

    ROW_ALT           : RGBA = (0.09, 0.09, 0.09, 0.40)
    ROW_NONE          : RGBA = (0.0,  0.0,  0.0,  0.0)

    # ------------------------------------------------------------------
    #  Drag-drop target colour (red accent — #EB5757 in linear)
    # ------------------------------------------------------------------

    DRAG_DROP_TARGET  : RGBA = (1.0, 1.0, 1.0, 1.0)

    # ------------------------------------------------------------------
    #  Add-Component / Inspector popup spacing
    # ------------------------------------------------------------------

    POPUP_ADD_COMP_PAD  = (10.0, 8.0)
    POPUP_ADD_COMP_SPC  = (6.0, 4.0)

    # ------------------------------------------------------------------
    #  Project panel icon button presets
    # ------------------------------------------------------------------

    ICON_BTN_NO_PAD   = (0.0, 0.0)                          # FramePadding for image icons
    BTN_SUBTLE_HOVER  : RGBA = (0.165, 0.165, 0.165, 1.0)   # subtle hover on unselected icons
    PROJECT_PANEL_PAD = (12.0, 8.0)                          # file grid child window padding

    # ------------------------------------------------------------------
    #  Console toolbar spacing
    # ------------------------------------------------------------------

    CONSOLE_FRAME_PAD = (4.0, 3.0)
    CONSOLE_ITEM_SPC  = (6.0, 4.0)

    # ------------------------------------------------------------------
    #  Status bar layout
    # ------------------------------------------------------------------

    STATUS_BAR_WIN_PAD   = (6.0, 4.0)
    STATUS_BAR_ITEM_SPC  = (8.0, 0.0)
    STATUS_BAR_FRAME_PAD = (0.0, 0.0)

    # ------------------------------------------------------------------
    #  Add Component button frame padding
    # ------------------------------------------------------------------

    ADD_COMP_FRAME_PAD = (6.0, 6.0)

    # ------------------------------------------------------------------
    #  Inspector panel layout
    # ------------------------------------------------------------------

    INSPECTOR_INIT_SIZE        = (300, 500)     # Initial window size (w, h)
    INSPECTOR_MIN_PROPS_H      = 100            # Min height for properties module
    INSPECTOR_MIN_RAWDATA_H    = 100            # Min height for raw-data module
    INSPECTOR_SPLITTER_H       = 8              # Splitter bar height
    INSPECTOR_DEFAULT_RATIO    = 0.4            # Default properties / raw-data ratio
    COMPONENT_ICON_SIZE        = 16             # Component header icon (px)
    COMP_ENABLED_CB_OFFSET     = 40             # Right-aligned enabled checkbox margin
    ADD_COMP_SEARCH_W          = 240            # "Search components…" input width

    # ------------------------------------------------------------------
    #  Build Settings panel
    # ------------------------------------------------------------------

    BUILD_SETTINGS_ROW_SPC = (4.0, 6.0)

    # ------------------------------------------------------------------
    #  Hierarchy tree layout
    # ------------------------------------------------------------------

    TREE_ITEM_SPC     = (0.0, 3.0)
    TREE_FRAME_PAD    = (4.0, 5.0)

    # ------------------------------------------------------------------
    #  Convenience: push/pop helpers
    # ------------------------------------------------------------------

    @staticmethod
    def push_ghost_button_style(ctx) -> int:
        """Push transparent button colours. Returns the number of colours pushed (3)."""
        ctx.push_style_color(ImGuiCol.Button,        *Theme.BTN_GHOST)
        ctx.push_style_color(ImGuiCol.ButtonHovered,  *Theme.BTN_GHOST_HOVERED)
        ctx.push_style_color(ImGuiCol.ButtonActive,   *Theme.BTN_GHOST_ACTIVE)
        return 3

    @staticmethod
    def push_flat_button_style(ctx, r: float, g: float, b: float, a: float = 1.0) -> int:
        """Push a flat-coloured button style. Returns 3."""
        ctx.push_style_color(ImGuiCol.Button,        r, g, b, a)
        ctx.push_style_color(ImGuiCol.ButtonHovered,  min(r + 0.06, 1), min(g + 0.06, 1), min(b + 0.06, 1), a)
        ctx.push_style_color(ImGuiCol.ButtonActive,   min(r + 0.12, 1), min(g + 0.12, 1), min(b + 0.12, 1), a)
        return 3

    @staticmethod
    def push_toolbar_vars(ctx) -> int:
        """Push the compact spacing preset for the toolbar. Returns var count (5)."""
        ctx.push_style_var_vec2(ImGuiStyleVar.WindowPadding, *Theme.TOOLBAR_WIN_PAD)
        ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding,  *Theme.TOOLBAR_FRAME_PAD)
        ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing,   *Theme.TOOLBAR_ITEM_SPC)
        ctx.push_style_var_float(ImGuiStyleVar.FrameRounding, Theme.TOOLBAR_FRAME_RND)
        ctx.push_style_var_float(ImGuiStyleVar.FrameBorderSize, Theme.TOOLBAR_FRAME_BRD)
        return 5

    @staticmethod
    def push_popup_vars(ctx) -> int:
        """Push the wider spacing preset for popups/dropdowns. Returns 3."""
        ctx.push_style_var_vec2(ImGuiStyleVar.WindowPadding, *Theme.POPUP_WIN_PAD)
        ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing,   *Theme.POPUP_ITEM_SPC)
        ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding,  *Theme.POPUP_FRAME_PAD)
        return 3

    @staticmethod
    def push_status_bar_button_style(ctx) -> int:
        """Push status-bar transparent button colours. Returns 3."""
        ctx.push_style_color(ImGuiCol.Button,        *Theme.BTN_GHOST)
        ctx.push_style_color(ImGuiCol.ButtonHovered,  *Theme.BTN_SB_HOVERED)
        ctx.push_style_color(ImGuiCol.ButtonActive,   *Theme.BTN_SB_ACTIVE)
        return 3

    @staticmethod
    def push_transparent_border(ctx) -> int:
        """Push transparent border colour. Returns 1."""
        ctx.push_style_color(ImGuiCol.Border, *Theme.BORDER_TRANSPARENT)
        return 1

    @staticmethod
    def push_drag_drop_target_style(ctx) -> int:
        """Push the drag-drop target highlight colour. Returns 1."""
        ctx.push_style_color(ImGuiCol.DragDropTarget, *Theme.DRAG_DROP_TARGET)
        return 1

    @staticmethod
    def push_console_toolbar_vars(ctx) -> int:
        """Push compact spacing for the console toolbar. Returns 3."""
        ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, *Theme.CONSOLE_FRAME_PAD)
        ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing,  *Theme.CONSOLE_ITEM_SPC)
        ctx.push_style_var_float(ImGuiStyleVar.FrameBorderSize, Theme.TOOLBAR_FRAME_BRD)
        return 3

    @staticmethod
    def push_splitter_style(ctx) -> int:
        """Push transparent button + subtle hover for draggable splitters. Returns 3."""
        ctx.push_style_color(ImGuiCol.Button,        *Theme.BTN_GHOST)
        ctx.push_style_color(ImGuiCol.ButtonHovered,  *Theme.SPLITTER_HOVER)
        ctx.push_style_color(ImGuiCol.ButtonActive,   *Theme.SPLITTER_ACTIVE)
        return 3

    @staticmethod
    def push_selected_icon_style(ctx) -> int:
        """Push highlight colours for a selected icon button. Returns 2."""
        ctx.push_style_color(ImGuiCol.Button,        *Theme.BTN_SELECTED)
        ctx.push_style_color(ImGuiCol.ButtonHovered,  *Theme.BTN_SELECTED)
        return 2

    @staticmethod
    def push_unselected_icon_style(ctx) -> int:
        """Push transparent + subtle hover for unselected icon button. Returns 2 colors + 1 var = 3 pops needed (2 color, 1 var)."""
        ctx.push_style_var_float(ImGuiStyleVar.FrameBorderSize, 0.0)
        ctx.push_style_color(ImGuiCol.Button,        *Theme.BTN_GHOST)
        ctx.push_style_color(ImGuiCol.ButtonHovered,  *Theme.BTN_SUBTLE_HOVER)
        return 2  # color count; caller must also pop 1 style var

    @staticmethod
    def get_play_border_color(is_paused: bool) -> RGBA:
        """Return the appropriate border colour for the current play state."""
        return Theme.BORDER_PAUSE if is_paused else Theme.BORDER_PLAY
