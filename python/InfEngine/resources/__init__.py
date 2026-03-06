import os

current_dir = os.path.dirname(os.path.abspath(__file__))

icon_path = os.path.join(current_dir, "pictures", "icon.png")
engine_font_path = os.path.join(current_dir, "fonts", "PingFangTC-Regular.otf")
engine_lib_path = os.path.join(current_dir, "..", "lib")
resources_path = os.path.join(current_dir)
file_type_icons_dir = os.path.join(current_dir, "icons")
component_icons_dir = os.path.join(current_dir, "icons", "components")

__all__ = [
    "icon_path",
    "engine_font_path",
    "engine_lib_path",
    "resources_path",
    "file_type_icons_dir",
    "component_icons_dir",
]