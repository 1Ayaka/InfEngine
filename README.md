<p align="center">
  <img src="docs/assets/logo.png" alt="InfernuxEngine Logo" width="128" />
</p>

<h1 align="center">InfernuxEngine</h1>

<p align="center">
  <strong>The first AI-native game engine — C++ backend, Python frontend.</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License" /></a>
  <img src="https://img.shields.io/badge/version-0.1.0-orange.svg" alt="Version" />
  <img src="https://img.shields.io/badge/platform-Windows-lightgrey.svg" alt="Platform" />
  <img src="https://img.shields.io/badge/python-3.12+-brightgreen.svg" alt="Python" />
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++ 17" />
  <img src="https://img.shields.io/badge/graphics-Vulkan-red.svg" alt="Vulkan" />
</p>

<p align="center">
  <a href="README-zh.md">🇨🇳 中文文档</a> · <a href="#quick-start">Quick Start</a> · <a href="#architecture">Architecture</a> · <a href="https://github.com/ChenlizheMe/InfEngine">GitHub</a>
</p>

---

## What is InfernuxEngine?

InfernuxEngine is an open-source game engine purpose-built for the AI era. The performance-critical backend (rendering, physics, scene graph) is written in C++ with Vulkan, while the entire scripting layer, editor UI, and render pipeline configuration run in Python — connected seamlessly via pybind11.

**Why Python?** Because in the age of LLMs and Vibe Coding:
- Python has the largest share of LLM training data — AI-generated game scripts are highest quality in Python.
- The entire AI/ML ecosystem (NumPy, PyTorch, OpenCV, Transformers, OpenAI SDK) is natively available inside your game scripts with a simple `import`.
- Hot-reload is instant — change a script, see it live in the editor, no compilation step.

```python
from InfEngine.components import InfComponent, serialized_field
from InfEngine.math import Vector3

class PlayerController(InfComponent):
    speed: float = serialized_field(default=5.0)

    def update(self, delta_time: float):
        h = Input.get_axis("Horizontal")
        v = Input.get_axis("Vertical")
        self.transform.position += Vector3(h, 0, v) * self.speed * delta_time
```

If you've used Unity, you already know how to use InfernuxEngine — the API is designed to feel familiar.

---

## Features

### Core Engine
- **Vulkan Renderer** — Modern graphics pipeline with MSAA, shadow mapping, offscreen scene/game render targets
- **Scriptable Render Pipeline (SRP)** — Fully Python-driven render graph with injection points, matching Unity 2023 SRP concepts
- **Jolt Physics** — Multi-threaded physics via [Jolt Physics](https://github.com/jrouwe/JoltPhysics) with Rigidbody interpolation, Box/Sphere/Capsule/Mesh colliders, layer matrix filtering, compound collider attribution, and Raycasting
- **Component System** — Unity-style `InfComponent` with full lifecycle (`awake`, `start`, `update`, `fixed_update`, `late_update`, `on_destroy`), serialized fields, decorators (`@require_component`, `@disallow_multiple`, `@execute_in_edit_mode`)
- **Asset System** — GUID-based AssetDatabase with `.meta` sidecar files, Material/Shader/Texture pipeline, hot-reload
- **Input System** — Unity-style static `Input` class (keyboard, mouse, axes)

### Editor (Python + ImGui)
- **Hierarchy Panel** — Scene tree with drag-and-drop reparenting
- **Inspector Panel** — Component properties, material editing, Add Component search popup
- **Scene View** — WASD + right-click camera controls, object picking, gizmos, selection outline
- **Game View** — Camera preview with resolution presets and screen-space UI overlay
- **UI Editor** — Figma-style 2D canvas editor with zoom, pan, pixel-aligned positioning, resize handles, distance guides
- **Console** — Log output with filtering
- **Project Panel** — File browser with asset preview
- **Undo System** — Full undo/redo for property changes and hierarchy operations
- **Play Mode** — Edit → Play → Pause with scene state save/restore (Unity-style isolation)

### AI Integration (Native)
- `Texture.to_numpy()` → NumPy array for direct use with OpenCV / PyTorch
- `RenderGraph.ReadPassColorPixels()` → Python for ML inference on rendered frames
- Any Python AI library is a simple `import` away inside your game scripts
- No plugins, no bridges, no sockets — **native Python calls**

---

## Quick Start

### Prerequisites

| Dependency | Version |
|:-----------|:--------|
| Windows | 10 / 11 (64-bit) |
| Python | 3.12+ |
| Vulkan SDK | 1.3+ (with SPIRV-Cross) |
| CMake | 3.22+ |
| Visual Studio | 2022 (MSVC v143) |
| pybind11 | 2.11+ (pip) |

### Clone

```bash
git clone --recurse-submodules https://github.com/ChenlizheMe/InfEngine.git
cd InfEngine
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### Build

```bash
# Install Python dependencies
pip install -r requirements.txt

# Configure and build (Release)
cmake --preset release
cmake --build --preset release
```

This builds the C++ backend into `_InfEngine.pyd` and copies it along with all DLL dependencies to `python/InfEngine/lib/`.

### Run the Editor

```bash
python packaging/launcher.py
```

This opens the project launcher. Create or open a project and the full editor will launch.

### Run Tests

```bash
cd python
python -m pytest test/ -v
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  Python Frontend (114 files)              │
│                                                           │
│  Engine / Editor    Components    RenderStack   UI / Input │
│  (Play Mode,        (InfComponent, (SRP Graph,  (Canvas,   │
│   Scene Manager,     Decorators,    Injection    Text,      │
│   25 Editor Panels)  BuiltinCmp)    Points)      Enums)     │
│                                                           │
│              ↕ pybind11 (_InfEngine.pyd, 14 binding files) │
├───────────────────────────────────────────────────────────┤
│                  C++ Backend (161 files)                   │
│                                                           │
│  Vulkan Renderer   Scene Graph    Jolt Physics   Assets    │
│  (67 files:        (43 files:     (PhysicsWorld, (AssetDB, │
│   SceneRenderGraph, GameObject,   Rigidbody,     Material, │
│   CommandBuffer,    Transform,    Colliders,     Shader,   │
│   MaterialPipeline, Component,    Raycast)       Texture)  │
│   EditorGizmos)     Camera/Light)                          │
│                                                           │
├───────────────────────────────────────────────────────────┤
│  External: Vulkan · SDL3 · Jolt · Assimp · ImGui · GLM    │
│            glslang · stb · VMA · nlohmann-json             │
└───────────────────────────────────────────────────────────┘
```

### Data Flow: Rendering

```
C++ Main Loop
  → RenderPipelineCallback.Render()          [virtual, pybind11 trampoline]
    → Python: RenderStackPipeline.render()
      → RenderStack.build_graph()
        → DefaultForwardPipeline.define_topology(graph)
          → graph.add_pass("Opaque"), graph.injection_point("after_opaque"), ...
        → graph.build() → RenderGraphDescription      [C++ POD via pybind11]
      → context.apply_graph(desc)                      [sends to C++ SceneRenderGraph]
      → context.submit_culling()                       [triggers GPU rendering]
```

### Data Flow: Physics

```
C++ SceneManager.FixedUpdate()
  → PhysicsWorld.Step(dt)                    [Jolt multi-threaded simulation]
  → Rigidbody.SyncPhysicsToTransform()       [copy Jolt body → Transform]
  → Python: InfComponent.fixed_update(dt)    [user scripts run here]
```

---

## Project Structure

```
InfEngine/
├── cpp/                        # C++ backend
│   ├── infengine/
│   │   ├── core/               # Logging, error handling, math, types
│   │   ├── function/
│   │   │   ├── renderer/       # Vulkan renderer (67 files)
│   │   │   ├── scene/          # Scene graph + physics (43 files)
│   │   │   └── resources/      # Asset pipeline (20 files)
│   │   ├── platform/           # SDL3 window, input, filesystem
│   │   └── tools/pybinding/    # pybind11 bindings (14 files)
│   └── test/                   # C++ tests
├── python/
│   ├── InfEngine/
│   │   ├── engine/             # Engine core + editor UI panels
│   │   ├── components/         # Component system + builtins
│   │   ├── renderstack/        # Scriptable Render Pipeline
│   │   ├── rendergraph/        # Render graph builder
│   │   ├── core/               # Material, Texture, Mesh, Shader wrappers
│   │   ├── ui/                 # UICanvas, UIText, screen-space UI
│   │   ├── physics/            # Physics.Raycast API
│   │   ├── input/              # Input system (Unity-style)
│   │   ├── gizmos/             # Editor gizmos
│   │   ├── scene/              # Scene manager, tag/layer queries
│   │   ├── math/               # Vector math utilities
│   │   ├── lib/                # Built .pyd + DLLs + type stubs
│   │   └── resources/          # Built-in shaders, fonts, icons
│   └── test/                   # Python tests (12 files)
├── packaging/                  # Project launcher (PySide6)
├── external/                   # Git submodules (Jolt, SDL, Assimp, etc.)
├── docs/                       # Documentation site
├── CMakeLists.txt
├── CMakePresets.json
├── pyproject.toml
└── requirements.txt
```

---

## Code Examples

### Custom Component

```python
from InfEngine.components import InfComponent, serialized_field
from InfEngine.components.builtin import Rigidbody
from InfEngine.math import Vector3
from InfEngine.input import Input, KeyCode

class Launcher(InfComponent):
    force: float = serialized_field(default=500.0, tooltip="Launch force")

    def start(self):
        self.rb = self.get_component(Rigidbody)

    def update(self, dt: float):
        if Input.get_key_down(KeyCode.SPACE):
            self.rb.add_force(Vector3(0, self.force, 0), mode="Impulse")
```

### AI Inside a Game Script

```python
from InfEngine.components import InfComponent, serialized_field
import openai  # just pip install openai

class NPCDialogue(InfComponent):
    prompt: str = serialized_field(default="You are a medieval shopkeeper.")

    def on_interact(self, player_message: str) -> str:
        response = openai.ChatCompletion.create(
            model="gpt-4",
            messages=[
                {"role": "system", "content": self.prompt},
                {"role": "user", "content": player_message},
            ],
        )
        return response.choices[0].message.content
```

### Custom Render Pass

```python
from InfEngine.renderstack import RenderPass

class OutlinePass(RenderPass):
    name = "Outline"
    injection_point = "after_opaque"
    default_order = 100

    def inject(self, graph, bus):
        with graph.add_pass("OutlinePass") as p:
            p.read("depth")
            p.write_color("color")
            p.draw_renderers(queue_range=(0, 2500), sort_mode="front_to_back")
```

---

## Current Status (v0.1)

| System | Status | Notes |
|:-------|:-------|:------|
| Vulkan Renderer | ✅ Functional | Forward rendering, MSAA, shadows, SRP |
| Physics (Jolt) | ✅ Functional | Rigidbody interpolation, 4 colliders, layer matrix, compound hit attribution, raycasting. ~250fps @ 1024 cubes |
| Component System | ✅ Complete | Full lifecycle, serialization, decorators |
| Editor | ✅ Functional | Hierarchy, Inspector, Scene/Game View, UI Editor, Console |
| Asset System | ✅ Basic | GUID tracking, Material/Shader/Texture pipeline |
| UI System | ⚠️ Early | Canvas + Text only. Button and Image in progress |
| Audio | ❌ Not started | Planned |
| Animation | ❌ Not started | Planned |
| Prefab | ❌ Not started | Planned |

---

## Roadmap

- **v0.2** — Collision callbacks (`on_collision_enter`/`on_trigger_enter`), UIButton, UIImage, basic Prefab
- **v0.3** — Post-processing stack (Bloom, Tone Mapping), model import pipeline (.fbx), audio system
- **v0.4** — Animation system, particle system, Figma data import
- **v1.0** — Standalone runtime player, complete documentation, example projects

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## Contact

- **Author**: Lizhe Chen
- **Email**: [chenlizheme@outlook.com](mailto:chenlizheme@outlook.com)
- **GitHub**: [https://github.com/ChenlizheMe/InfEngine](https://github.com/ChenlizheMe/InfEngine)

---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

Copyright (c) 2024-2026 Lizhe Chen
