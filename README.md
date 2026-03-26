<p align="center">
  <img src="docs/assets/logo.png" alt="InfernuxEngine Logo" width="128" />
</p>

<h1 align="center">InfernuxEngine</h1>

<p align="center">
  <strong>Open-source game engine · C++17 / Vulkan · Python scripting · MIT licensed</strong>
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
  <a href="README-zh.md">🇨🇳 中文文档</a> · <a href="#quick-start">Quick Start</a> · <a href="#architecture">Architecture</a> · <a href="https://chenlizheme.github.io/InfEngine/">Website</a>
</p>

---

## Overview

InfernuxEngine is a from-scratch game engine built with a **C++17 / Vulkan runtime** and a **Python scripting layer**. The C++ side handles rendering, physics, and resource management; the Python side handles gameplay logic, editor tools, and content workflows.

Key points:

- **C++17 / Vulkan core** — forward and deferred rendering, PBR, RenderGraph-based pipeline, Jolt physics
- **Python scripting** — Unity-style component model, hot-reload, editor extensions, access to the Python ecosystem
- **MIT licensed** — no royalties, no runtime fees, full source access

---

## Design Stance

### Scriptable rendering

The render pipeline is exposed through a RenderGraph API. You can author and modify render passes from Python instead of working against a closed editor pipeline.

### Fast iteration

Python handles not just gameplay code but also editor extensions, asset workflows, and tooling — keeping the inner loop short.

### No licensing surprises

MIT license. No royalties, no runtime fees, no vendor lock-in.

---

## Features

### Runtime foundation

- Vulkan forward and deferred rendering
- PBR materials, cascaded shadows, MSAA, post-processing (bloom, tonemapping, color grading, film grain, etc.)
- RenderGraph-based pass orchestration (Python-scriptable render pipeline)
- Jolt physics (rigidbodies, 4 collider types, raycasting, collision callbacks, physics layers)
- SDL3 audio with 3D spatialization (AudioSource, AudioListener, streaming)
- Input system (Unity-style keyboard/mouse API)
- Scene and resource systems (GUID-based AssetDatabase, .meta files, dependency tracking)

### Python layer

- Unity-style component lifecycle
- `serialized_field` metadata for Inspector-driven authoring
- Decorators for component requirements and editor execution
- Hot-reload for scripts and content
- Coroutine system (WaitForSeconds, WaitUntil, etc.)
- Game UI system (Canvas, Text, Image, Button with event system)
- Prefab save/instantiate with override tracking
- Full access to the Python ecosystem (NumPy, PyTorch, OpenCV, etc.)

### Editor

- 12 panels: Hierarchy, Inspector, Scene View, Game View, Console, Project, UI Editor, Toolbar, Menu Bar, Status Bar, Tags & Layers, Build Settings
- Transform gizmos (translate/rotate/scale), multi-selection, undo/redo
- Play-mode scene isolation (serialize/restore)
- Material, texture, audio, shader, and font inspectors
- Asset browser with thumbnail preview and hot-reload
- Nuitka-based standalone game builder

---

## Why the C++ / Python Split Matters

The engine is structured around a practical division of labor.

| Layer | Role |
|:------|:-----|
| C++17 / Vulkan | Renderer, scene systems, resources, physics, platform |
| pybind11 bridge | Bindings between native systems and the Python API |
| Python | Gameplay, editor logic, render authoring, tooling |

---

## Quick Start

### Prerequisites

| Dependency | Version |
|:-----------|:--------|
| Windows | 10 / 11 (64-bit) |
| Python | 3.12+ |
| Vulkan SDK | 1.3+ |
| CMake | 3.22+ |
| Visual Studio | 2022 (MSVC v143) |
| pybind11 | 2.11+ |

Notes:

- `python packaging/launcher.py` is the development path. It uses your current Python environment.
- The packaged Hub is a separate distribution path. It manages a private Python 3.12 runtime for end users.
- The current release targets Windows. Official prebuilt wheels are currently `win_amd64`.

### Clone

```bash
git clone --recurse-submodules https://github.com/ChenlizheMe/InfEngine.git
cd InfEngine
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

### Build

```bash
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
cmake --preset release
cmake --build --preset release
```

The build produces the native Python module, copies required runtime dependencies, and installs the package so `import InfEngine` works immediately inside the workspace environment.

### Run

```bash
python packaging/launcher.py
```

This launches the Hub in development mode. In this mode:

- no installer is required
- no private Hub runtime is installed
- project `.venv` environments are created from the current Python interpreter
- project creation still requires a prebuilt InfEngine wheel in `dist/`

### Test

```bash
cd python
python -m pytest test/ -v
```

---

## Hub Packaging

There are now two distinct ways to distribute the Hub.

### 1. Standalone Hub bundle

```bash
cmake --build --preset packaging
```

This produces the PyInstaller output directory under `dist/InfEngine Hub/`.

Use this when you want:

- a developer-facing portable bundle
- a quick local smoke test of the packaged Hub
- a fallback path when you do not have an installer toolchain installed

Behavior of the standalone bundle:

- the Hub can still bootstrap Python 3.12 for itself if needed
- runtime preparation happens on first launch in the background
- this path is intended as a fallback, not the primary end-user distribution format

### 2. Windows installer

```bash
cmake --build --preset packaging-installer
```

This produces a graphical Windows installer executable for InfEngine Hub.

This target now builds a custom graphical installer executable that users can double-click directly.

Use this when you want the Hub to behave like a real installed application:

- copy files into a proper install directory
- download the correct Python 3.12 installer for the current machine architecture during setup
- install the Hub's private Python runtime into `InfEngineHubData/python312`
- each new project receives its own full copy of this runtime

### Installer dependency

The installer target now uses the existing Python packaging toolchain in this repository.

- no external Windows installer authoring tool is required
- normal source development still does not need the installer target
- `packaging-installer` depends on the already-packaged Hub payload from `packaging`

### Installer behavior

The installer itself is now a graphical application and performs these steps directly:

- detects the host architecture
- downloads the matching Python 3.12 installer
- installs Python into the Hub's private data directory
- new projects receive their own full copy of the runtime

### Installer note

The InfEngine Hub installer pipeline is an area I am not deeply familiar with yet, so the current installer-related workflow was produced with AI assistance.

The current installer stack includes:

- PyInstaller for the standalone Hub bundle
- PyInstaller for the graphical installer executable
- a bundled installer UI written with PySide6
- the official Python 3.12 Windows installers downloaded per machine architecture

If you spot problems, packaging edge cases, or better Windows installer practices, please open an issue.

### Current architecture note

The Python runtime installer can be selected per machine architecture, but the engine distribution path is still constrained by the wheels you publish.

Today, the project ships Windows wheels for the current supported architecture. If you want full non-`amd64` support, you must also publish matching InfEngine wheels for those architectures.

---

## Hub Runtime Model

The Hub now distinguishes clearly between development and end-user environments.

### Development mode

- launched with `python packaging/launcher.py`
- uses the current Python environment
- does not install a private Hub runtime
- uses local build outputs and local wheels

### Installed Hub mode

- launched from the packaged application
- uses a private Python 3.12 runtime under `InfEngineHubData/`
- copies the full Python runtime into each new project at `.runtime/python312/`
- installs the selected InfEngine version into each project's runtime

This means each project owns a complete, self-contained Python copy. It does not share a runtime with other projects.

---

## Architecture

```text
Python authoring layer
  -> editor panels, components, RenderGraph authoring, tooling, project workflows
  -> pybind11 binding seam
C++ engine core
  -> renderer, scene, resources, physics, audio, platform services
External stack
  -> Vulkan, SDL3, Jolt, ImGui, Assimp, GLM, glslang, VMA
```

### Practical flow

1. Author gameplay or rendering logic in Python.
2. Bind that logic to editor-visible data and scene objects.
3. Describe render passes through the RenderGraph API.
4. The native backend handles scheduling, memory, and GPU execution.

This is the main architectural promise of the engine: **high-level iteration without surrendering low-level ownership**.

---

## Status

### Working

- Vulkan rendering (forward + deferred), PBR, shadows, 8 post-processing effects
- Python scripting with hot-reload and editor integration
- Full editor (12 panels, gizmos, undo/redo, play mode)
- Jolt physics (rigidbodies, colliders, raycasting, collision layers)
- SDL3 audio with 3D spatialization
- Asset pipeline (GUID-based AssetDatabase, .meta files, dependency graph)
- Prefab system (save/instantiate/override tracking)
- Game UI system (Canvas, Text, Image, Button, event system)
- Standalone game build via Nuitka
- Hub launcher and Windows installer

### In progress

- Animation system (skeletal animation, state machines)
- Advanced UI controls (ScrollView, Slider, layout groups)
- Documentation, tutorials, and example projects
- Cross-platform support (Linux, macOS)

---

## Roadmap

| Version | Focus |
|:--------|:------|
| v0.1 | **Current** — Rendering, physics, audio, scripting, editor, prefabs, game UI, standalone build. Usable for basic games without animation |
| v0.2 | Animation system, advanced UI controls (ScrollView, Slider, layout), asset rename safety |
| v0.3 | Particles, terrain, model/content pipeline improvements |
| v0.4 | Cross-platform (Linux/macOS), networking foundations |
| v1.0 | Documentation, examples, production readiness |

---

## Contributing

1. Read the README and the docs site first.
2. Check the roadmap to understand current priorities.
3. Open an issue or discussion before pushing broad architectural changes.
4. Submit focused pull requests with a clear engineering goal.

This repository benefits most from contributions that preserve the core idea of the project: explicit architecture, short iteration loops, and a stack the team actually owns.

---

## Contact

- Email: [chenlizheme@outlook.com](mailto:chenlizheme@outlook.com)
- GitHub: [https://github.com/ChenlizheMe/InfEngine](https://github.com/ChenlizheMe/InfEngine)

---

## License

MIT License. See [LICENSE](LICENSE) for details.
