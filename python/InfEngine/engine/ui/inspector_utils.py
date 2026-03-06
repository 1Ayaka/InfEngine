"""
Shared layout utility functions for Inspector panel sub-modules.
"""

from InfEngine.lib import InfGUIContext

# Padding added to text measurement for comfortable spacing.
LABEL_PAD = 20


def max_label_w(ctx: InfGUIContext, labels) -> float:
    """Return the pixel width needed for the widest label + padding."""
    w = 0.0
    for lb in labels:
        tw = ctx.calc_text_width(lb)
        if tw > w:
            w = tw
    return w + LABEL_PAD


def field_label(ctx: InfGUIContext, label: str, width: float = 0.0):
    """Label on the left, next widget fills remaining row width.
    If *width* is 0 the column is auto-sized to this label."""
    if width <= 0.0:
        width = ctx.calc_text_width(label) + LABEL_PAD
    ctx.align_text_to_frame_padding()
    ctx.label(label)
    ctx.same_line(width)
    ctx.set_next_item_width(-1)


# ---------------------------------------------------------------------------
# Shared Apply / Revert buttons for read-only asset import settings
# ---------------------------------------------------------------------------

# ImGui style colour indices (matches theme.ImGuiCol)
_COL_BUTTON = 21  # ImGuiCol.Button


def render_apply_revert(ctx: InfGUIContext, is_dirty: bool,
                        on_apply, on_revert) -> None:
    """Render a unified Apply / Revert button bar for import-settings editors.

    *on_apply* and *on_revert* are zero-arg callables invoked on click.
    When *is_dirty* is False both buttons are greyed-out and non-interactive.
    When *is_dirty* is True the Apply button turns green.
    """
    ctx.separator()
    if is_dirty:
        ctx.push_style_color(_COL_BUTTON, 0.2, 0.6, 0.2, 1.0)
        ctx.button("Apply", on_apply)
        ctx.pop_style_color(1)
        ctx.same_line()
        ctx.button("Revert", on_revert)
    else:
        ctx.begin_disabled(True)
        ctx.button("Apply", None)
        ctx.same_line()
        ctx.button("Revert", None)
        ctx.end_disabled()
