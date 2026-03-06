"""
Unity-style Scene View panel with 3D viewport and camera controls.
"""

import math
from InfEngine.lib import InfGUIContext
from .closable_panel import ClosablePanel
from .theme import Theme
from .viewport_utils import ViewportInfo, capture_viewport_info
from . import imgui_keys as _keys

# Gizmo axis IDs — must match C++ EditorTools constants
from InfEngine.lib._InfEngine import GIZMO_X_AXIS_ID, GIZMO_Y_AXIS_ID, GIZMO_Z_AXIS_ID

_GIZMO_IDS = {GIZMO_X_AXIS_ID: 1, GIZMO_Y_AXIS_ID: 2, GIZMO_Z_AXIS_ID: 3}
_AXIS_DIRS = {1: (1.0, 0.0, 0.0), 2: (0.0, 1.0, 0.0), 3: (0.0, 0.0, 1.0)}

# Tool mode constants — must match C++ EditorTools::ToolMode
TOOL_NONE = 0
TOOL_TRANSLATE = 1
TOOL_ROTATE = 2
TOOL_SCALE = 3


# ======================================================================
# Quaternion math helpers  (matches GLM convention: ZYX intrinsic order,
# euler = (pitch/X, yaw/Y, roll/Z) in degrees)
# ======================================================================

def _euler_deg_to_quat(ex, ey, ez):
    """Euler angles (degrees, XYZ = pitch/yaw/roll) → quaternion (w,x,y,z).

    Matches ``glm::quat(glm::radians(vec3(ex,ey,ez)))``.
    """
    rx = math.radians(ex) * 0.5
    ry = math.radians(ey) * 0.5
    rz = math.radians(ez) * 0.5
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    return (
        cx * cy * cz + sx * sy * sz,   # w
        sx * cy * cz - cx * sy * sz,   # x
        cx * sy * cz + sx * cy * sz,   # y
        cx * cy * sz - sx * sy * cz,   # z
    )


def _quat_to_euler_deg(q):
    """Quaternion (w,x,y,z) → Euler angles (degrees, XYZ = pitch/yaw/roll).

    Matches ``glm::degrees(glm::eulerAngles(q))``.
    """
    w, x, y, z = q
    # pitch (X)
    sinp = 2.0 * (w * x + y * z)
    cosp = 1.0 - 2.0 * (x * x + y * y)
    # Avoid atan2(0,0) only when both are exactly 0
    pitch = math.atan2(sinp, cosp) if abs(sinp) > 1e-12 or abs(cosp) > 1e-12 else 0.0

    # yaw (Y) — clamped asin
    siny = -2.0 * (x * z - w * y)
    siny = max(-1.0, min(1.0, siny))
    yaw = math.asin(siny)

    # roll (Z)
    sinr = 2.0 * (w * z + x * y)
    cosr = 1.0 - 2.0 * (y * y + z * z)
    roll = math.atan2(sinr, cosr) if abs(sinr) > 1e-12 or abs(cosr) > 1e-12 else 0.0

    return (math.degrees(pitch), math.degrees(yaw), math.degrees(roll))


def _quat_mul(a, b):
    """Hamilton product of two quaternions (w,x,y,z)."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    )


def _axis_angle_to_quat(ax, ay, az, angle_deg):
    """Axis-angle → quaternion (w,x,y,z).  Axis must be unit-length."""
    half = math.radians(angle_deg) * 0.5
    s = math.sin(half)
    return (math.cos(half), ax * s, ay * s, az * s)


class SceneViewPanel(ClosablePanel):
    """
    Unity-style Scene View panel with 3D viewport and camera controls.
    
    Controls (Unity-style):
    - Right-click + drag: Rotate camera (look around)
    - Middle-click + drag: Pan camera
    - Scroll wheel: Zoom in/out (dolly)
    - Right-click + WASD: Fly mode movement
    - Right-click + QE: Up/Down in fly mode
    - Shift: Speed boost in fly mode
    """
    
    WINDOW_TYPE_ID = "scene_view"
    WINDOW_DISPLAY_NAME = "场景 Scene"

    # Key codes imported from shared imgui_keys module
    KEY_W = _keys.KEY_W
    KEY_A = _keys.KEY_A
    KEY_S = _keys.KEY_S
    KEY_D = _keys.KEY_D
    KEY_Q = _keys.KEY_Q
    KEY_E = _keys.KEY_E
    KEY_R = _keys.KEY_R
    KEY_LEFT_SHIFT = _keys.KEY_LEFT_SHIFT
    KEY_RIGHT_SHIFT = _keys.KEY_RIGHT_SHIFT
    
    def __init__(self, title: str = "场景 Scene", engine=None):
        super().__init__(title, window_id="scene_view")
        self._engine = engine
        self._play_mode_manager = None
        self._last_frame_time = 0.0
        self._on_object_picked = None
        
        # Scene render target size tracking
        self._last_scene_width = 0
        self._last_scene_height = 0
        
        # Mouse button state tracking for detecting press/release
        self._was_right_down = False
        self._was_middle_down = False
        
        # Mouse position tracking for delta calculation
        self._last_mouse_x = 0.0
        self._last_mouse_y = 0.0

        # Drag-outside-panel state (Unity-style: keep controlling camera
        # even when cursor leaves the panel, wrap at screen edges)
        self._is_camera_dragging = False
        self._last_global_x = 0.0
        self._last_global_y = 0.0
        self._screen_bounds = None  # (x, y, w, h)  cached display bounds

        # Editor gizmo drag state (shared across translate/rotate/scale)
        self._is_gizmo_dragging = False
        self._gizmo_drag_axis = 0          # 1=X, 2=Y, 3=Z
        self._gizmo_drag_axis_dir = (1.0, 0.0, 0.0)
        self._gizmo_drag_start_t = 0.0     # parameter along axis at grab (translate/scale)
        self._gizmo_drag_start_pos = (0.0, 0.0, 0.0)  # object pos at grab
        self._gizmo_drag_start_euler = (0.0, 0.0, 0.0)  # object euler at grab (rotate)
        self._gizmo_drag_start_scale = (1.0, 1.0, 1.0)  # object local_scale at grab (scale)
        self._gizmo_drag_start_screen = (0.0, 0.0) # screen pos at grab (rotate)
        self._gizmo_drag_obj_id = 0        # object being dragged
        self._gizmo_tool_mode = TOOL_TRANSLATE  # current tool mode (Python tracking)
        self._coord_space = 0  # 0=Global, 1=Local

        # Scene picking cycle state (Unity-style repeated click cycling)
        self._pick_cycle_candidates = []
        self._pick_cycle_index = -1
        self._pick_cycle_last_mouse = (-1.0, -1.0)
        self._pick_cycle_last_viewport = (0, 0)

        # Focus tracking for auto-exit UI Mode
        self._was_focused: bool = False
        self._on_focus_gained = None   # callback() when panel gains focus
    
    def set_engine(self, engine):
        """Set the engine reference for camera control."""
        self._engine = engine

    def set_play_mode_manager(self, manager):
        """Set the PlayModeManager so the panel can show play-mode border."""
        self._play_mode_manager = manager

    def set_on_object_picked(self, callback):
        """Set callback for scene object picking (receives object ID or 0)."""
        self._on_object_picked = callback
    
    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            if self._engine:
                self._engine.set_scene_view_visible(False)
                if self._last_scene_width != 1 or self._last_scene_height != 1:
                    self._engine.resize_scene_render_target(1, 1)
                    self._last_scene_width = 1
                    self._last_scene_height = 1
            return

        if self._engine:
            self._engine.set_scene_view_visible(True)
        
        import time
        current_time = time.time()
        delta_time = current_time - self._last_frame_time if self._last_frame_time > 0 else 0.016
        self._last_frame_time = current_time
        
        # Clamp delta time
        delta_time = min(delta_time, 0.1)
            
        ctx.set_next_window_size(800, 600, Theme.COND_FIRST_USE_EVER)

        # Determine play-mode border colour (if any)
        _play_border_clr = None
        pm = self._play_mode_manager
        if pm is None:
            from InfEngine.engine.play_mode import PlayModeManager, PlayModeState
            pm = PlayModeManager.get_instance()
        if pm and not pm.is_edit_mode:
            from InfEngine.engine.play_mode import PlayModeState
            _play_border_clr = Theme.BORDER_PAUSE if pm.state == PlayModeState.PAUSED else Theme.BORDER_PLAY
        
        # Viewport flags: don't steal focus + no scrollbars
        window_flags = Theme.WINDOW_FLAGS_VIEWPORT | Theme.WINDOW_FLAGS_NO_SCROLL
        if self._begin_closable_window(ctx, window_flags):
            # Track focus to auto-exit UI Mode
            focused = ctx.is_window_focused(0)
            if focused and not self._was_focused:
                if self._on_focus_gained:
                    self._on_focus_gained()
            self._was_focused = focused

            # Get content region for scene viewport
            avail_width = ctx.get_content_region_avail_width()
            avail_height = ctx.get_content_region_avail_height()
            
            # Use full available space for scene
            scene_width = max(int(avail_width), 64)
            scene_height = max(int(avail_height), 64)
            
            # Remember cursor position for overlay
            cursor_start_x = ctx.get_cursor_pos_x()
            cursor_start_y = ctx.get_cursor_pos_y()
            
            # Resize scene render target if size changed
            if self._engine and (scene_width != self._last_scene_width or scene_height != self._last_scene_height):
                self._engine.resize_scene_render_target(scene_width, scene_height)
                self._last_scene_width = scene_width
                self._last_scene_height = scene_height
            
            # Get and display scene texture
            scene_texture_id = 0
            if self._engine:
                scene_texture_id = self._engine.get_scene_texture_id()
            
            if scene_texture_id != 0:
                # Display scene render target
                ctx.image(scene_texture_id, float(scene_width), float(scene_height), 0.0, 0.0, 1.0, 1.0)

                # Capture viewport info from the image widget just drawn
                vp = capture_viewport_info(ctx)
                is_scene_hovered = vp.is_hovered

                # Draw coordinate-space dropdown overlay (top-left of scene)
                # MUST be drawn BEFORE the picking check so that ImGui marks
                # the combo as hovered/active, preventing clicks on the combo
                # from being treated as scene picks (which would deselect).
                ctx.set_cursor_pos_x(cursor_start_x + 8)
                ctx.set_cursor_pos_y(cursor_start_y + 8)
                self._draw_coord_space_dropdown(ctx)
                overlay_hovered = ctx.is_item_hovered()

                # Draw camera position overlay (top-right of scene)
                self._draw_pos_overlay(ctx, cursor_start_x, cursor_start_y, scene_width)

                # --- Unity-style tool switching shortcuts (Q/W/E/R) ---
                # Process when no text input widget is active and no
                # right-mouse camera control is active (otherwise W/E/R
                # are fly-mode keys).  No window-focus gate so that
                # hotkeys work immediately after selecting in hierarchy.
                if not ctx.want_text_input() and not ctx.is_mouse_button_down(1):
                    if ctx.is_key_pressed(self.KEY_Q):
                        self._set_tool_mode(TOOL_NONE)
                    elif ctx.is_key_pressed(self.KEY_W):
                        self._set_tool_mode(TOOL_TRANSLATE)
                    elif ctx.is_key_pressed(self.KEY_E):
                        self._set_tool_mode(TOOL_ROTATE)
                    elif ctx.is_key_pressed(self.KEY_R):
                        self._set_tool_mode(TOOL_SCALE)

                # --- Editor tools (translate/rotate/scale gizmo) interaction ---
                # All hover/drag logic lives in Python, using the existing
                # pick_scene_object_id() system which now also tests gizmo
                # arrow meshes and returns their special axis IDs.
                left_down = ctx.is_mouse_button_down(0)
                gizmo_consumed = False

                if self._engine:
                    local_mx, local_my = vp.mouse_local(ctx)

                    gizmo_consumed = self._update_gizmo_interaction(
                        local_mx, local_my, vp.width, vp.height,
                        left_down, is_scene_hovered)
                
                # Start drag when mouse button pressed while hovering scene
                right_down = ctx.is_mouse_button_down(1)
                middle_down = ctx.is_mouse_button_down(2)
                if is_scene_hovered and (right_down or middle_down) and not self._is_camera_dragging:
                    self._is_camera_dragging = True
                    # Cache display bounds once when drag begins
                    self._screen_bounds = ctx.get_display_bounds()
                    self._last_global_x = ctx.get_global_mouse_pos_x()
                    self._last_global_y = ctx.get_global_mouse_pos_y()
                
                # Stop drag when buttons released
                if self._is_camera_dragging and not right_down and not middle_down:
                    self._is_camera_dragging = False
                
                # Process camera input when hovering OR when dragging outside
                if is_scene_hovered or self._is_camera_dragging:
                    self._process_camera_input(ctx, delta_time)

                    # Handle left-click picking (only when hovering AND gizmo
                    # is NOT consuming the mouse AND the overlay combo is NOT
                    # being interacted with).
                    if (is_scene_hovered and not gizmo_consumed
                            and not overlay_hovered
                            and ctx.is_mouse_button_clicked(0)):
                        picked_id = self._pick_scene_object(ctx, vp)
                        if self._on_object_picked:
                            self._on_object_picked(picked_id)

                # Draw play-mode outline on top of the scene image
                if _play_border_clr is not None:
                    ctx.draw_rect(
                        vp.image_min_x, vp.image_min_y,
                        vp.image_max_x, vp.image_max_y,
                        *_play_border_clr,
                        thickness=Theme.BORDER_THICKNESS,
                    )

            else:
                # Placeholder when texture not ready
                ctx.invisible_button("scene_placeholder", float(scene_width), float(scene_height))
                ctx.set_cursor_pos_x(cursor_start_x + 8)
                ctx.set_cursor_pos_y(cursor_start_y + 8)
                ctx.label("场景加载中...")
        else:
            if self._engine:
                self._engine.set_scene_view_visible(False)
            
        ctx.end_window()
    
    def _draw_coord_space_dropdown(self, ctx: InfGUIContext):
        """Draw Global/Local coordinate-space dropdown in the top-left corner."""
        _SPACE_LABELS = ["Global", "Local"]
        ctx.push_id_str("coord_space_dropdown")
        ctx.set_next_item_width(80)
        new_val = ctx.combo("##coord_space", self._coord_space, _SPACE_LABELS)
        if new_val != self._coord_space:
            self._coord_space = new_val
            # Sync local mode to C++ so gizmo visuals align to object rotation
            if self._engine:
                self._engine.set_editor_tool_local_mode(self._coord_space == 1)
        ctx.pop_id()

    def _draw_pos_overlay(self, ctx: InfGUIContext, cursor_start_x, cursor_start_y, scene_width):
        """Draw camera position in the top-right corner of the scene."""
        if not self._engine:
            return
        cam_pos = self._engine.get_editor_camera_position()
        text = f"Pos: ({cam_pos[0]:.1f}, {cam_pos[1]:.1f}, {cam_pos[2]:.1f})"
        # Estimate text width (~7px per char) and place near the right edge
        text_width = len(text) * 7.0
        ctx.set_cursor_pos_x(cursor_start_x + scene_width - text_width - 12)
        ctx.set_cursor_pos_y(cursor_start_y + 8)
        ctx.label(text)

    def _pick_scene_object(self, ctx: InfGUIContext, vp: ViewportInfo) -> int:
        """Pick scene object under mouse cursor with repeated-click cycling."""
        if not self._engine:
            return 0

        local_x, local_y = vp.mouse_local(ctx)

        # Clamp within viewport
        if local_x < 0 or local_y < 0 or local_x > vp.width or local_y > vp.height:
            return 0

        candidates = self._engine.pick_scene_object_ids(local_x, local_y, vp.width, vp.height)

        # Filter invalid IDs and gizmo axis pseudo-IDs.
        ids = []
        for candidate in candidates:
            object_id = int(candidate)
            if object_id > 0 and object_id not in _GIZMO_IDS:
                ids.append(object_id)

        # Fallback to old single-hit API if list API is unavailable/empty.
        if not ids:
            fallback_id = int(self._engine.pick_scene_object_id(local_x, local_y, vp.width, vp.height))

            if fallback_id > 0 and fallback_id not in _GIZMO_IDS:
                self._pick_cycle_candidates = [fallback_id]
                self._pick_cycle_index = 0
                self._pick_cycle_last_mouse = (local_x, local_y)
                self._pick_cycle_last_viewport = (int(vp.width), int(vp.height))
                return fallback_id

            self._pick_cycle_candidates = []
            self._pick_cycle_index = -1
            return 0

        same_viewport = self._pick_cycle_last_viewport == (int(vp.width), int(vp.height))
        last_x, last_y = self._pick_cycle_last_mouse
        same_spot = abs(local_x - last_x) <= 3.0 and abs(local_y - last_y) <= 3.0
        same_candidates = ids == self._pick_cycle_candidates

        if same_viewport and same_spot and same_candidates and self._pick_cycle_index >= 0:
            index = (self._pick_cycle_index + 1) % len(ids)
        else:
            index = 0

        self._pick_cycle_candidates = ids
        self._pick_cycle_index = index
        self._pick_cycle_last_mouse = (local_x, local_y)
        self._pick_cycle_last_viewport = (int(vp.width), int(vp.height))

        return ids[index]
    
    def _process_camera_input(self, ctx: InfGUIContext, delta_time: float):
        """Process Unity-style camera input using mouse position delta.
        
        When the mouse is being dragged (right/middle button held), we track
        global screen coordinates so the camera keeps moving even when the
        cursor leaves the panel.  When the cursor reaches a screen edge it
        wraps to the opposite side, just like Unity.
        """
        if not self._engine:
            return
        
        # Mouse button states
        right_down = ctx.is_mouse_button_down(1)
        middle_down = ctx.is_mouse_button_down(2)
        
        # Detect button just pressed
        right_just_pressed = right_down and not self._was_right_down
        middle_just_pressed = middle_down and not self._was_middle_down
        
        # ------------------------------------------------------------------
        # Use GLOBAL mouse position when dragging so that leaving the panel
        # doesn't interrupt camera control.
        # ------------------------------------------------------------------
        if self._is_camera_dragging:
            gx = ctx.get_global_mouse_pos_x()
            gy = ctx.get_global_mouse_pos_y()
        else:
            gx = ctx.get_mouse_pos_x()
            gy = ctx.get_mouse_pos_y()
        
        mouse_delta_x = 0.0
        mouse_delta_y = 0.0
        
        if (right_down or middle_down) and not right_just_pressed and not middle_just_pressed:
            raw_dx = gx - self._last_global_x
            raw_dy = gy - self._last_global_y
            
            # Ignore very large jumps caused by screen-edge warp
            if abs(raw_dx) < 400 and abs(raw_dy) < 400:
                if abs(raw_dx) > 0.1:
                    mouse_delta_x = raw_dx
                if abs(raw_dy) > 0.1:
                    mouse_delta_y = raw_dy
            
            # --- Screen-edge wrapping (Unity-style) ---
            if self._is_camera_dragging and self._screen_bounds:
                sx, sy, sw, sh = self._screen_bounds
                margin = 2.0
                warped = False
                new_gx, new_gy = gx, gy
                
                if gx <= sx + margin:
                    new_gx = sx + sw - margin - 1
                    warped = True
                elif gx >= sx + sw - margin:
                    new_gx = sx + margin + 1
                    warped = True
                
                if gy <= sy + margin:
                    new_gy = sy + sh - margin - 1
                    warped = True
                elif gy >= sy + sh - margin:
                    new_gy = sy + margin + 1
                    warped = True
                
                if warped:
                    ctx.warp_mouse_global(new_gx, new_gy)
                    gx, gy = new_gx, new_gy
        
        # Update tracking state
        self._last_global_x = gx
        self._last_global_y = gy
        # Also keep local tracking in sync for picking etc.
        self._last_mouse_x = ctx.get_mouse_pos_x()
        self._last_mouse_y = ctx.get_mouse_pos_y()
        self._was_right_down = right_down
        self._was_middle_down = middle_down
        
        # Scroll wheel: zoom
        scroll_delta = ctx.get_mouse_wheel_delta()
        
        # Keyboard for fly mode (only when right mouse held)
        key_w = right_down and ctx.is_key_down(self.KEY_W)
        key_s = right_down and ctx.is_key_down(self.KEY_S)
        key_a = right_down and ctx.is_key_down(self.KEY_A)
        key_d = right_down and ctx.is_key_down(self.KEY_D)
        key_q = right_down and ctx.is_key_down(self.KEY_Q)
        key_e = right_down and ctx.is_key_down(self.KEY_E)
        key_shift = ctx.is_key_down(self.KEY_LEFT_SHIFT) or ctx.is_key_down(self.KEY_RIGHT_SHIFT)
        
        # Send to engine
        self._engine.process_scene_view_input(
            delta_time,
            right_down,
            middle_down,
            mouse_delta_x,
            mouse_delta_y,
            scroll_delta,
            key_w, key_a, key_s, key_d,
            key_q, key_e, key_shift
        )
    
    # ------------------------------------------------------------------
    # Tool mode management
    # ------------------------------------------------------------------

    def _set_tool_mode(self, mode: int):
        """Switch the active editor tool (syncs to C++ and resets drag)."""
        if mode == self._gizmo_tool_mode:
            return
        self._gizmo_tool_mode = mode
        self._is_gizmo_dragging = False
        if self._engine:
            self._engine.set_editor_tool_mode(mode)
            self._engine.set_editor_tool_highlight(0)

    # ------------------------------------------------------------------
    # Gizmo interaction helpers (all in Python)
    # ------------------------------------------------------------------

    @staticmethod
    def _closest_param_on_axis(ray_o, ray_d, axis_o, axis_d):
        """Closest-point-between-two-lines: parameter *s* along the axis line.

        Given ray P = ray_o + t*ray_d  and  axis Q = axis_o + s*axis_d,
        returns the s that minimises distance between the two lines.
        """
        w = (ray_o[0] - axis_o[0], ray_o[1] - axis_o[1], ray_o[2] - axis_o[2])
        a = ray_d[0]*ray_d[0] + ray_d[1]*ray_d[1] + ray_d[2]*ray_d[2]
        b = ray_d[0]*axis_d[0] + ray_d[1]*axis_d[1] + ray_d[2]*axis_d[2]
        c = axis_d[0]*axis_d[0] + axis_d[1]*axis_d[1] + axis_d[2]*axis_d[2]
        d = ray_d[0]*w[0] + ray_d[1]*w[1] + ray_d[2]*w[2]
        e = axis_d[0]*w[0] + axis_d[1]*w[1] + axis_d[2]*w[2]
        denom = a * c - b * b
        if abs(denom) < 1e-10:
            return -e / c if abs(c) > 1e-10 else 0.0
        return (a * e - b * d) / denom

    def _update_gizmo_interaction(self, local_mx, local_my, scene_w, scene_h,
                                   left_down, is_hovered):
        """Python-side hover highlight + axis-constrained drag for all tool modes.

        Returns True if the gizmo consumed the input this frame.
        """
        engine = self._engine
        if not engine:
            return False

        mode = self._gizmo_tool_mode
        if mode == TOOL_NONE:
            return False

        # -----------------------------------------------------------
        # DRAG CONTINUATION (dispatches to mode-specific handler)
        # -----------------------------------------------------------
        if self._is_gizmo_dragging:
            if not left_down:
                # Release drag — record undo command for the completed operation
                self._record_gizmo_undo(mode)
                self._is_gizmo_dragging = False
                engine.set_editor_tool_highlight(0)
                return False

            if mode == TOOL_TRANSLATE:
                self._drag_translate(engine, local_mx, local_my, scene_w, scene_h)
            elif mode == TOOL_ROTATE:
                self._drag_rotate(engine, local_mx, local_my, scene_w, scene_h)
            elif mode == TOOL_SCALE:
                self._drag_scale(engine, local_mx, local_my, scene_w, scene_h)

            return True  # consumed

        # -----------------------------------------------------------
        # HOVER DETECTION (using existing picking infrastructure)
        # -----------------------------------------------------------
        if not is_hovered:
            engine.set_editor_tool_highlight(0)
            return False

        picked = engine.pick_scene_object_id(local_mx, local_my, scene_w, scene_h)

        axis = _GIZMO_IDS.get(picked, 0)
        engine.set_editor_tool_highlight(axis)

        if axis == 0:
            return False  # not hovering any gizmo handle

        # -----------------------------------------------------------
        # DRAG START (common for all modes)
        # -----------------------------------------------------------
        if left_down:
            self._is_gizmo_dragging = True
            self._gizmo_drag_axis = axis
            self._gizmo_drag_start_screen = (local_mx, local_my)

            from InfEngine.lib._InfEngine import SceneManager as _SM
            scene = _SM.instance().get_active_scene()
            sel_id = engine.get_selected_object_id()
            obj_pos = (0.0, 0.0, 0.0)
            obj_euler = (0.0, 0.0, 0.0)
            obj_scale = (1.0, 1.0, 1.0)
            if scene and sel_id:
                obj = scene.find_by_id(sel_id)
                if obj:
                    p = obj.transform.position
                    obj_pos = (p[0], p[1], p[2])
                    e = obj.transform.euler_angles
                    obj_euler = (e[0], e[1], e[2])
                    s = obj.transform.local_scale
                    obj_scale = (s[0], s[1], s[2])

                    # Compute axis direction based on coordinate space
                    if self._coord_space == 1 and obj:
                        # Local space: use object's local axes
                        if axis == 1:
                            r = obj.transform.right
                            self._gizmo_drag_axis_dir = (r[0], r[1], r[2])
                        elif axis == 2:
                            u = obj.transform.up
                            self._gizmo_drag_axis_dir = (u[0], u[1], u[2])
                        elif axis == 3:
                            f = obj.transform.forward
                            self._gizmo_drag_axis_dir = (-f[0], -f[1], -f[2])  # forward = -Z
                    else:
                        self._gizmo_drag_axis_dir = _AXIS_DIRS[axis]
            else:
                self._gizmo_drag_axis_dir = _AXIS_DIRS[axis]
            self._gizmo_drag_obj_id = sel_id
            self._gizmo_drag_start_pos = obj_pos
            self._gizmo_drag_start_euler = obj_euler
            self._gizmo_drag_start_scale = obj_scale

            # For translate/scale: record initial axis parameter
            if mode in (TOOL_TRANSLATE, TOOL_SCALE):
                ray = engine.screen_to_world_ray(local_mx, local_my, scene_w, scene_h)
                self._gizmo_drag_start_t = self._closest_param_on_axis(
                    ray[:3], ray[3:], self._gizmo_drag_start_pos, self._gizmo_drag_axis_dir)

            return True  # consumed

        return True  # hovering a gizmo handle — consume to suppress picking

    # ------------------------------------------------------------------
    # Mode-specific drag handlers
    # ------------------------------------------------------------------

    def _drag_translate(self, engine, local_mx, local_my, scene_w, scene_h):
        """Axis-constrained translation: project mouse ray onto drag axis."""
        ray = engine.screen_to_world_ray(local_mx, local_my, scene_w, scene_h)
        ad = self._gizmo_drag_axis_dir
        sp = self._gizmo_drag_start_pos

        cur_t = self._closest_param_on_axis(ray[:3], ray[3:], sp, ad)
        delta = cur_t - self._gizmo_drag_start_t

        new_pos = (sp[0] + ad[0] * delta,
                   sp[1] + ad[1] * delta,
                   sp[2] + ad[2] * delta)
        from InfEngine.lib._InfEngine import SceneManager as _SM, vec3f
        scene = _SM.instance().get_active_scene()
        if scene:
            obj = scene.find_by_id(self._gizmo_drag_obj_id)
            if obj:
                obj.transform.position = vec3f(new_pos[0], new_pos[1], new_pos[2])

    def _drag_rotate(self, engine, local_mx, local_my, scene_w, scene_h):
        """Rotation around the drag axis (world or local depending on coord space)."""
        # Screen-space delta from drag start → rotation angle.
        # 200 pixels of horizontal movement ≈ 180°, like Unity.
        dx = local_mx - self._gizmo_drag_start_screen[0]

        ad = self._gizmo_drag_axis_dir  # world-space axis (global or local)

        # Camera-relative sign correction so the visible ring always follows
        # the mouse drag direction.
        #
        # Derivation: the front-most point on the ring (nearest the camera)
        # moves by  δθ · cross(A, P_front).  The horizontal screen component
        # of that movement must have the same sign as the mouse dx.
        # Working through the projection math:
        #   sign = sign( dot(A, camera_up) )
        # where camera_up = cross(camera_right, view_fwd) and
        #       camera_right = normalize(cross(view_fwd, world_up)).
        cam_pos = engine.get_editor_camera_position()
        op = self._gizmo_drag_start_pos
        vf = (op[0] - cam_pos[0], op[1] - cam_pos[1], op[2] - cam_pos[2])
        vf_len = math.sqrt(vf[0]**2 + vf[1]**2 + vf[2]**2)
        if vf_len > 1e-9:
            vf = (vf[0]/vf_len, vf[1]/vf_len, vf[2]/vf_len)
            # camera_right = normalize(cross(view_fwd, world_up=(0,1,0)))
            #              = normalize((-vf_z, 0, vf_x))
            cr_x, cr_z = -vf[2], vf[0]
            cr_len = math.sqrt(cr_x**2 + cr_z**2)
            if cr_len > 1e-9:
                cr = (cr_x/cr_len, 0.0, cr_z/cr_len)
            else:
                cr = (1.0, 0.0, 0.0)  # camera looking straight up/down
            # camera_up = cross(camera_right, view_fwd)
            cu = (cr[1]*vf[2] - cr[2]*vf[1],
                  cr[2]*vf[0] - cr[0]*vf[2],
                  cr[0]*vf[1] - cr[1]*vf[0])
            sign_val = ad[0]*cu[0] + ad[1]*cu[1] + ad[2]*cu[2]
            sign = 1.0 if sign_val >= 0 else -1.0
        else:
            sign = 1.0

        angle_deg = dx * (180.0 / 200.0) * sign

        se = self._gizmo_drag_start_euler
        q_start = _euler_deg_to_quat(se[0], se[1], se[2])
        q_delta = _axis_angle_to_quat(ad[0], ad[1], ad[2], angle_deg)

        # Always pre-multiply: the axis in q_delta is already expressed in
        # world space for both Global mode (world unit axis) and Local mode
        # (object's local axis mapped to world space).
        q_new = _quat_mul(q_delta, q_start)
        new_euler = _quat_to_euler_deg(q_new)

        from InfEngine.lib._InfEngine import SceneManager as _SM, vec3f
        scene = _SM.instance().get_active_scene()
        if scene:
            obj = scene.find_by_id(self._gizmo_drag_obj_id)
            if obj:
                obj.transform.euler_angles = vec3f(new_euler[0], new_euler[1], new_euler[2])

    def _drag_scale(self, engine, local_mx, local_my, scene_w, scene_h):
        """Scale along the drag axis. In Local mode, scale applies directly to
        the corresponding local_scale component. In Global mode, the world-axis
        scale factor is decomposed onto local axes."""
        ray = engine.screen_to_world_ray(local_mx, local_my, scene_w, scene_h)
        ad = self._gizmo_drag_axis_dir
        sp = self._gizmo_drag_start_pos

        cur_t = self._closest_param_on_axis(ray[:3], ray[3:], sp, ad)
        start_t = self._gizmo_drag_start_t

        # Scale factor: ratio of current projection to initial projection
        if abs(start_t) < 1e-6:
            factor = 1.0 + (cur_t - start_t)
        else:
            factor = cur_t / start_t
        factor = max(factor, 0.01)

        from InfEngine.lib._InfEngine import SceneManager as _SM, vec3f
        scene = _SM.instance().get_active_scene()
        if not scene:
            return
        obj = scene.find_by_id(self._gizmo_drag_obj_id)
        if not obj:
            return

        ss = self._gizmo_drag_start_scale
        new_scale = list(ss)

        if self._coord_space == 1:
            # Local mode: scale directly on the axis component (1=X, 2=Y, 3=Z)
            axis_idx = self._gizmo_drag_axis - 1  # 0, 1, or 2
            new_scale[axis_idx] = max(ss[axis_idx] * factor, 0.001)
        else:
            # Global mode: decompose world-axis scale onto local axes
            r = obj.transform.right
            u = obj.transform.up
            f = obj.transform.forward
            local_axes = [
                (r[0], r[1], r[2]),
                (u[0], u[1], u[2]),
                (-f[0], -f[1], -f[2]),
            ]
            for i in range(3):
                dot_val = (ad[0] * local_axes[i][0] +
                           ad[1] * local_axes[i][1] +
                           ad[2] * local_axes[i][2])
                local_factor = 1.0 + (factor - 1.0) * dot_val * dot_val
                new_scale[i] = max(ss[i] * local_factor, 0.001)

        obj.transform.local_scale = vec3f(new_scale[0], new_scale[1], new_scale[2])

    def _record_gizmo_undo(self, mode: int):
        """Record an undo command for the gizmo drag that just finished."""
        from InfEngine.lib._InfEngine import SceneManager as _SM, vec3f
        from InfEngine.engine.undo import UndoManager, SetPropertyCommand

        scene = _SM.instance().get_active_scene()
        if not scene or not self._gizmo_drag_obj_id:
            return
        obj = scene.find_by_id(self._gizmo_drag_obj_id)
        if not obj:
            return

        transform = obj.transform

        if mode == TOOL_TRANSLATE:
            old_val = vec3f(*self._gizmo_drag_start_pos)
            new_val_raw = transform.position
            new_val = vec3f(new_val_raw[0], new_val_raw[1], new_val_raw[2])
            if self._vec3_approx_equal(old_val, new_val):
                return
            cmd = SetPropertyCommand(transform, "position",
                                     old_val, new_val, "Translate")
        elif mode == TOOL_ROTATE:
            old_val = vec3f(*self._gizmo_drag_start_euler)
            new_val_raw = transform.euler_angles
            new_val = vec3f(new_val_raw[0], new_val_raw[1], new_val_raw[2])
            if self._vec3_approx_equal(old_val, new_val):
                return
            cmd = SetPropertyCommand(transform, "euler_angles",
                                     old_val, new_val, "Rotate")
        elif mode == TOOL_SCALE:
            old_val = vec3f(*self._gizmo_drag_start_scale)
            new_val_raw = transform.local_scale
            new_val = vec3f(new_val_raw[0], new_val_raw[1], new_val_raw[2])
            if self._vec3_approx_equal(old_val, new_val):
                return
            cmd = SetPropertyCommand(transform, "local_scale",
                                     old_val, new_val, "Scale")
        else:
            return

        UndoManager.instance().record(cmd)

    @staticmethod
    def _vec3_approx_equal(a, b, eps=1e-5):
        """Check if two vec3f-like objects are approximately equal."""
        return (abs(a[0] - b[0]) < eps and
                abs(a[1] - b[1]) < eps and
                abs(a[2] - b[2]) < eps)

    def reset_camera(self):
        """Reset camera to default position."""
        if self._engine:
            self._engine.reset_editor_camera()
    
    def focus_on(self, x: float, y: float, z: float, distance: float = 10.0):
        """Focus camera on a point."""
        if self._engine:
            self._engine.focus_editor_camera_on(x, y, z, distance)


