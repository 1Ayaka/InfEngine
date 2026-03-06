<p align="center">
  <img src="docs/assets/logo.png" alt="InfernuxEngine Logo" width="128" />
</p>

<h1 align="center">InfernuxEngine</h1>

<p align="center">
  <strong>首个 AI 原生游戏引擎 — C++ 后端，Python 前端。</strong>
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
  <a href="README.md">🇬🇧 English</a> · <a href="#快速开始">快速开始</a> · <a href="#架构">架构</a> · <a href="https://github.com/ChenlizheMe/InfEngine">GitHub</a>
</p>

---

## InfernuxEngine 是什么？

InfernuxEngine 是一个面向 AI 时代的开源游戏引擎。性能关键的后端（渲染、物理、场景图）使用 C++ 和 Vulkan 编写，而整个脚本层、编辑器 UI 和渲染管线配置全部在 Python 中运行 — 通过 pybind11 无缝连接。

**为什么选择 Python？** 因为在 LLM 和 Vibe Coding 的时代：
- Python 在 LLM 训练数据中占比最大 — AI 生成的游戏脚本在 Python 中质量最高
- 整个 AI/ML 生态（NumPy、PyTorch、OpenCV、Transformers、OpenAI SDK）可以在游戏脚本中通过一句 `import` 原生使用
- 热重载即时生效 — 修改脚本后编辑器中立刻看到变化，无需编译

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

如果你用过 Unity，你已经会用 InfernuxEngine — API 设计几乎一致。

---

## 功能特性

### 核心引擎
- **Vulkan 渲染器** — 现代图形管线，支持 MSAA、阴影贴图、离屏场景/游戏渲染目标
- **可编程渲染管线 (SRP)** — 完全由 Python 驱动的渲染图，支持注入点，对标 Unity 2023 SRP
- **Jolt 物理引擎** — 基于 [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 的多线程物理模拟，支持 Rigidbody 插值、Box/Sphere/Capsule/Mesh 碰撞体、Layer 矩阵过滤、复合碰撞体精确归因与射线检测
- **组件系统** — Unity 风格的 `InfComponent`，完整生命周期（`awake`、`start`、`update`、`fixed_update`、`late_update`、`on_destroy`）、序列化字段、装饰器（`@require_component`、`@disallow_multiple`、`@execute_in_edit_mode`）
- **资产系统** — 基于 GUID 的 AssetDatabase，`.meta` 伴随文件，Material/Shader/Texture 管线，热重载
- **输入系统** — Unity 风格的静态 `Input` 类（键盘、鼠标、轴）

### 编辑器（Python + ImGui）
- **层级面板 (Hierarchy)** — 场景树，支持拖放重排
- **检视器 (Inspector)** — 组件属性编辑、材质编辑、搜索式添加组件弹窗
- **场景视图 (Scene View)** — WASD + 右键拖拽相机控制、物体拾取、Gizmos 可视化、选中描边
- **游戏视图 (Game View)** — 摄像机预览，分辨率预设，屏幕空间 UI 叠加
- **UI 编辑器** — Figma 风格 2D 画布编辑器，缩放/平移/像素对齐/调整手柄/距离参考线
- **控制台 (Console)** — 日志输出与过滤
- **项目面板 (Project)** — 文件浏览与资产预览
- **撤销系统 (Undo)** — 属性修改和层级操作的完整撤销/重做
- **Play Mode** — Edit → Play → Pause，场景状态保存/恢复（Unity 风格隔离）

### AI 集成（原生）
- `Texture.to_numpy()` → NumPy 数组，可直接用于 OpenCV / PyTorch
- `RenderGraph.ReadPassColorPixels()` → Python 端可对渲染帧进行 ML 推理
- 任何 Python AI 库只需一句 `import` 即可在游戏脚本中使用
- 无需插件、无需桥接、无需 Socket — **原生 Python 调用**

---

## 快速开始

### 环境要求

| 依赖 | 版本 |
|:-----|:-----|
| Windows | 10 / 11 (64 位) |
| Python | 3.12+ |
| Vulkan SDK | 1.3+（含 SPIRV-Cross） |
| CMake | 3.22+ |
| Visual Studio | 2022 (MSVC v143) |
| pybind11 | 2.11+（pip 安装） |

### 克隆

```bash
git clone --recurse-submodules https://github.com/ChenlizheMe/InfEngine.git
cd InfEngine
```

如果你已经克隆过但没加 `--recurse-submodules`：

```bash
git submodule update --init --recursive
```

### 构建

```bash
# 安装 Python 依赖
pip install -r requirements.txt

# 配置并构建（Release）
cmake --preset release
cmake --build --preset release
```

构建会将 C++ 后端编译为 `_InfEngine.pyd`，并将其连同所有 DLL 依赖复制到 `python/InfEngine/lib/`。

### 运行编辑器

```bash
python packaging/launcher.py
```

这会打开项目启动器。创建或打开一个项目后，完整的编辑器将启动。

### 运行测试

```bash
cd python
python -m pytest test/ -v
```

---

## 架构

```
┌──────────────────────────────────────────────────────────┐
│                 Python 前端（114 个文件）                   │
│                                                            │
│  引擎 / 编辑器     组件系统       渲染管线栈    UI / 输入    │
│  (Play Mode,       (InfComponent, (SRP 图,     (Canvas,     │
│   场景管理器,       装饰器,        注入点)       Text,        │
│   25 个编辑器面板)   BuiltinCmp)                 枚举)        │
│                                                            │
│             ↕ pybind11 (_InfEngine.pyd，14 个绑定文件)      │
├────────────────────────────────────────────────────────────┤
│                 C++ 后端（161 个文件）                       │
│                                                            │
│  Vulkan 渲染器    场景图         Jolt 物理      资产管理     │
│  (67 文件:        (43 文件:      (PhysicsWorld, (AssetDB,    │
│   SceneRenderGraph, GameObject,  Rigidbody,    Material,    │
│   CommandBuffer,   Transform,    碰撞体,       Shader,      │
│   材质管线,        Component,     射线检测)      Texture)     │
│   编辑器 Gizmos)   Camera/Light)                             │
│                                                            │
├────────────────────────────────────────────────────────────┤
│  外部依赖: Vulkan · SDL3 · Jolt · Assimp · ImGui · GLM     │
│           glslang · stb · VMA · nlohmann-json               │
└────────────────────────────────────────────────────────────┘
```

### 数据流：渲染

```
C++ 主循环
  → RenderPipelineCallback.Render()          [虚函数，pybind11 trampoline]
    → Python: RenderStackPipeline.render()
      → RenderStack.build_graph()
        → DefaultForwardPipeline.define_topology(graph)
          → graph.add_pass("Opaque"), graph.injection_point("after_opaque"), ...
        → graph.build() → RenderGraphDescription      [C++ POD 通过 pybind11 传递]
      → context.apply_graph(desc)                      [发送到 C++ SceneRenderGraph]
      → context.submit_culling()                       [触发 GPU 渲染]
```

### 数据流：物理

```
C++ SceneManager.FixedUpdate()
  → PhysicsWorld.Step(dt)                    [Jolt 多线程模拟]
  → Rigidbody.SyncPhysicsToTransform()       [Jolt 刚体 → Transform 同步]
  → Python: InfComponent.fixed_update(dt)    [用户脚本在此执行]
```

---

## 项目结构

```
InfEngine/
├── cpp/                        # C++ 后端
│   ├── infengine/
│   │   ├── core/               # 日志、错误处理、数学、类型
│   │   ├── function/
│   │   │   ├── renderer/       # Vulkan 渲染器（67 个文件）
│   │   │   ├── scene/          # 场景图 + 物理（43 个文件）
│   │   │   └── resources/      # 资产管线（20 个文件）
│   │   ├── platform/           # SDL3 窗口、输入、文件系统
│   │   └── tools/pybinding/    # pybind11 绑定（14 个文件）
│   └── test/                   # C++ 测试
├── python/
│   ├── InfEngine/
│   │   ├── engine/             # 引擎核心 + 编辑器 UI 面板
│   │   ├── components/         # 组件系统 + 内建组件
│   │   ├── renderstack/        # 可编程渲染管线
│   │   ├── rendergraph/        # 渲染图构建器
│   │   ├── core/               # Material、Texture、Mesh、Shader 包装
│   │   ├── ui/                 # UICanvas、UIText、屏幕空间 UI
│   │   ├── physics/            # Physics.Raycast API
│   │   ├── input/              # 输入系统（Unity 风格）
│   │   ├── gizmos/             # 编辑器 Gizmos
│   │   ├── scene/              # 场景管理器、标签/层级查询
│   │   ├── math/               # 向量数学工具
│   │   ├── lib/                # 编译产物 .pyd + DLL + 类型桩
│   │   └── resources/          # 内建着色器、字体、图标
│   └── test/                   # Python 测试（12 个文件）
├── packaging/                  # 项目启动器（PySide6）
├── external/                   # Git 子模块（Jolt、SDL、Assimp 等）
├── docs/                       # 文档站点
├── CMakeLists.txt
├── CMakePresets.json
├── pyproject.toml
└── requirements.txt
```

---

## 代码示例

### 自定义组件

```python
from InfEngine.components import InfComponent, serialized_field
from InfEngine.components.builtin import Rigidbody
from InfEngine.math import Vector3
from InfEngine.input import Input, KeyCode

class Launcher(InfComponent):
    force: float = serialized_field(default=500.0, tooltip="发射力度")

    def start(self):
        self.rb = self.get_component(Rigidbody)

    def update(self, dt: float):
        if Input.get_key_down(KeyCode.SPACE):
            self.rb.add_force(Vector3(0, self.force, 0), mode="Impulse")
```

### 在游戏脚本中使用 AI

```python
from InfEngine.components import InfComponent, serialized_field
import openai  # 只需 pip install openai

class NPCDialogue(InfComponent):
    prompt: str = serialized_field(default="你是一个中世纪的商人。")

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

### 自定义渲染 Pass

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

## 当前状态 (v0.1)

| 系统 | 状态 | 说明 |
|:-----|:-----|:-----|
| Vulkan 渲染器 | ✅ 可用 | 前向渲染、MSAA、阴影、SRP |
| 物理 (Jolt) | ✅ 可用 | Rigidbody 插值、4 种碰撞体、Layer 矩阵、复合命中归因、射线检测。1024 个立方体约 250fps |
| 组件系统 | ✅ 完整 | 完整生命周期、序列化、装饰器 |
| 编辑器 | ✅ 可用 | Hierarchy、Inspector、Scene/Game View、UI Editor、Console |
| 资产系统 | ✅ 基础 | GUID 追踪、Material/Shader/Texture 管线 |
| UI 系统 | ⚠️ 早期 | Canvas + Text。Button 和 Image 开发中 |
| 音频 | ❌ 未开始 | 已规划 |
| 动画 | ❌ 未开始 | 已规划 |
| Prefab 预制体 | ❌ 未开始 | 已规划 |

---

## 路线图

- **v0.2** — 碰撞回调（`on_collision_enter` / `on_trigger_enter`）、UIButton、UIImage、基础 Prefab
- **v0.3** — 后处理栈（Bloom、色调映射）、模型导入管线（.fbx）、音频系统
- **v0.4** — 动画系统、粒子系统、Figma 数据导入
- **v1.0** — 独立运行时播放器、完整文档、示例项目

---

## 参与贡献

欢迎贡献！请提交 Issue 或 Pull Request。

1. Fork 本仓库
2. 创建你的功能分支（`git checkout -b feature/amazing-feature`）
3. 提交更改（`git commit -m '添加了很棒的功能'`）
4. 推送到分支（`git push origin feature/amazing-feature`）
5. 打开一个 Pull Request

---

## 联系方式

- **作者**: Lizhe Chen
- **邮箱**: [chenlizheme@outlook.com](mailto:chenlizheme@outlook.com)
- **GitHub**: [https://github.com/ChenlizheMe/InfEngine](https://github.com/ChenlizheMe/InfEngine)

---

## 许可证

本项目基于 MIT 许可证开源 — 详见 [LICENSE](LICENSE) 文件。

Copyright (c) 2024-2026 Lizhe Chen
