"""
File-system CRUD operations for the Project panel.

Functions accept the required state as explicit parameters so they don't
depend on ``ProjectPanel`` internals.
"""

import os


# ---------------------------------------------------------------------------
# Templates
# ---------------------------------------------------------------------------

SCRIPT_TEMPLATE = '''
from InfEngine.components import *
from InfEngine.debug import *


class {class_name}(InfComponent):
    # Public fields (automatically serialized and shown in Inspector)
    # speed = 5.0       # float (use .0 for decimals)
    
    def start(self):
        """Called before first update, after all awake() calls."""
    
    def update(self, delta_time: float):
        """Called every frame."""
'''

VERTEX_SHADER_TEMPLATE = '''@shader_id: {shader_id}
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(set = 0, binding = 0) uniform CameraUBO {{
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
}} camera;

layout(set = 0, binding = 1) uniform ModelUBO {{
    mat4 model;
}} model;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out vec3 fragWorldPos;

void main() {{
    vec4 worldPos = model.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    gl_Position = camera.proj * camera.view * worldPos;
    
    mat3 normalMatrix = transpose(inverse(mat3(model.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}}
'''

FRAGMENT_SHADER_TEMPLATE = '''@shader_id: {shader_id}
@property: _BaseColor, Float4, [1.0, 1.0, 1.0, 1.0]
#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {{
    // Base color from material properties
    vec4 baseColor = material._BaseColor;
    
    // Simple lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float NdotL = max(dot(normalize(fragNormal), lightDir), 0.0);
    float lighting = 0.3 + 0.7 * NdotL;  // Ambient + diffuse
    
    outColor = vec4(baseColor.rgb * lighting, baseColor.a);
}}
'''

SCENE_TEMPLATE = '''{{
  "schema_version": 1,
  "name": "{scene_name}",
  "isPlaying": false,
  "objects": []
}}
'''
MATERIAL_TEMPLATE = '''{{
  "name": "{material_name}",
  "guid": "",
  "shaders": {{
    "vertex": "unlit",
    "fragment": "unlit"
  }},
  "renderState": {{
    "cullMode": 1,
    "frontFace": 1,
    "polygonMode": 0,
    "depthTestEnable": true,
    "depthWriteEnable": true,
    "depthCompareOp": 1,
    "blendEnable": false,
    "srcColorBlendFactor": 6,
    "dstColorBlendFactor": 7,
    "colorBlendOp": 0,
    "renderQueue": 2000
  }},
  "properties": {{
    "_BaseColor": {{
      "type": 3,
      "value": [1.0, 1.0, 1.0, 1.0]
    }}
  }}
}}
'''


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def get_unique_name(current_path: str, base_name: str, extension: str = "") -> str:
    """Return a *base_name* that doesn't clash with existing entries in *current_path*.

    *extension* is considered when checking for conflicts but is NOT appended
    to the returned string.
    """
    name = base_name + extension
    full_path = os.path.join(current_path, name)
    full_path_no_ext = os.path.join(current_path, base_name)

    if not os.path.exists(full_path) and not os.path.exists(full_path_no_ext):
        return base_name

    counter = 1
    while True:
        candidate = f"{base_name}{counter}"
        name_with_ext = candidate + extension
        fp = os.path.join(current_path, name_with_ext)
        fp_ne = os.path.join(current_path, candidate)
        if not os.path.exists(fp) and not os.path.exists(fp_ne):
            return candidate
        counter += 1
        if counter > 999:
            break
    return f"{base_name}{counter}"


# ---------------------------------------------------------------------------
# Create operations
# ---------------------------------------------------------------------------

def create_folder(current_path: str, folder_name: str):
    """Create a folder and return ``(True, "")`` or ``(False, error_msg)``."""
    if not folder_name or not current_path:
        return False, "Invalid folder name"

    folder_name = folder_name.strip()
    if not folder_name:
        return False, "Folder name cannot be empty"

    new_path = os.path.join(current_path, folder_name)
    if os.path.exists(new_path):
        return False, f"'{folder_name}' already exists"

    os.makedirs(new_path)
    return True, ""


def create_script(current_path: str, script_name: str, asset_database=None,
                  file_manager=None):
    """Create a Python script from template. Returns ``(True, "")`` or ``(False, error_msg)``."""
    if not script_name or not current_path:
        return False, "Invalid script name"

    script_name = script_name.strip()
    if not script_name:
        return False, "Script name cannot be empty"

    class_name = script_name
    if class_name.endswith('.py'):
        class_name = class_name[:-3]

    if not class_name.isidentifier():
        return False, "Invalid script name (must be valid Python identifier)"

    if not script_name.endswith('.py'):
        script_name = script_name + '.py'

    file_path = os.path.join(current_path, script_name)
    if os.path.exists(file_path):
        return False, f"'{script_name}' already exists"

    content = SCRIPT_TEMPLATE.format(class_name=class_name)
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(content)

    if asset_database:
        guid = asset_database.import_asset(file_path)
        print(f"[ProjectPanel] Registered script: {script_name} -> {guid}")
    elif file_manager:
        from InfEngine.lib import ResourceType
        uid = file_manager.register_resource(file_path, ResourceType.Script)
        print(f"[ProjectPanel] Registered script: {script_name} -> {uid}")

    return True, ""


def create_shader(current_path: str, shader_name: str, shader_type: str,
                  asset_database=None, file_manager=None):
    """Create a shader file from template. Returns ``(True, "")`` or ``(False, error_msg)``."""
    if not shader_name or not current_path:
        return False, "Invalid shader name"

    shader_name = shader_name.strip()
    if not shader_name:
        return False, "Shader name cannot be empty"

    for ext in ['.vert', '.frag', '.glsl']:
        if shader_name.endswith(ext):
            shader_name = shader_name[:-len(ext)]
            break

    shader_id = shader_name.lower().replace(' ', '_')
    extension = f'.{shader_type}'
    file_name = shader_name + extension
    file_path = os.path.join(current_path, file_name)

    if os.path.exists(file_path):
        return False, f"'{file_name}' already exists"

    if shader_type == 'vert':
        content = VERTEX_SHADER_TEMPLATE.format(shader_id=shader_id)
    else:
        content = FRAGMENT_SHADER_TEMPLATE.format(shader_id=shader_id)

    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(content)

    if asset_database:
        guid = asset_database.import_asset(file_path)
        print(f"[ProjectPanel] Registered shader: {file_name} -> {guid}")
    elif file_manager:
        from InfEngine.lib import ResourceType
        uid = file_manager.register_resource(file_path, ResourceType.Shader)
        print(f"[ProjectPanel] Registered shader: {file_name} -> {uid}")

    return True, ""


def create_scene(current_path: str, scene_name: str, asset_database=None):
    """Create a ``.scene`` file from template. Returns ``(True, path)`` or ``(False, error_msg)``."""
    if not scene_name or not current_path:
        return False, "Invalid scene name"

    scene_name = scene_name.strip()
    if not scene_name:
        return False, "Scene name cannot be empty"

    if scene_name.endswith('.scene'):
        scene_name = scene_name[:-6]

    file_name = scene_name + '.scene'
    file_path = os.path.join(current_path, file_name)

    if os.path.exists(file_path):
        return False, f"'{file_name}' already exists"

    content = SCENE_TEMPLATE.format(scene_name=scene_name)
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(content)

    if asset_database:
        guid = asset_database.import_asset(file_path)
        print(f"[ProjectPanel] Registered scene: {file_name} -> {guid}")

    return True, file_path


def create_material(current_path: str, material_name: str, asset_database=None):
    """Create a ``.mat`` file from template. Returns ``(True, "")`` or ``(False, error_msg)``."""
    if not material_name or not current_path:
        return False, "Invalid material name"

    material_name = material_name.strip()
    if not material_name:
        return False, "Material name cannot be empty"

    if material_name.endswith('.mat'):
        material_name = material_name[:-4]

    file_name = material_name + '.mat'
    file_path = os.path.join(current_path, file_name)

    if os.path.exists(file_path):
        return False, f"'{file_name}' already exists"

    content = MATERIAL_TEMPLATE.format(material_name=material_name)
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(content)

    if asset_database:
        guid = asset_database.import_asset(file_path)
        print(f"[ProjectPanel] Registered material: {file_name} -> {guid}")

    return True, ""


# ---------------------------------------------------------------------------
# Delete & Rename
# ---------------------------------------------------------------------------

def delete_item(item_path: str, asset_database=None, file_manager=None):
    """Delete a file or directory from the filesystem and notify AssetDB/FileManager."""
    if not item_path or not os.path.exists(item_path):
        return

    is_dir = os.path.isdir(item_path)

    if is_dir:
        import shutil
        shutil.rmtree(item_path)
    else:
        os.remove(item_path)

    if not is_dir:
        if asset_database:
            asset_database.on_asset_deleted(item_path)
        elif file_manager:
            file_manager.delete_resource(item_path)


def do_rename(old_path: str, new_name: str, asset_database=None, file_manager=None):
    """Rename a file or directory. Returns the new path on success, ``None`` on failure."""
    from .project_utils import update_material_name_in_file

    if not old_path or not new_name:
        return None

    safe_name = "".join([c for c in new_name if c.isalnum() or c in "._- "]).strip()
    if not safe_name:
        return None

    if os.path.isfile(old_path):
        _, ext = os.path.splitext(old_path)
        if ext:
            safe_name += ext

    new_path = os.path.join(os.path.dirname(old_path), safe_name)

    if old_path == new_path:
        return new_path  # Nothing to do

    _, ext = os.path.splitext(old_path)
    if ext.lower() == '.mat' and os.path.isfile(old_path):
        update_material_name_in_file(old_path, os.path.splitext(safe_name)[0])

    os.rename(old_path, new_path)

    if asset_database and os.path.isfile(new_path):
        asset_database.on_asset_moved(old_path, new_path)
    elif file_manager and os.path.isfile(new_path):
        file_manager.move_resource(old_path, new_path)

    return new_path
