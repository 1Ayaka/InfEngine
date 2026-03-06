/**
 * Infernux Engine - Internationalization (i18n)
 * Supports English and Chinese
 */

const translations = {
    en: {
        // Navigation
        "nav.home": "Home",
        "nav.features": "Features",
        "nav.showcase": "Showcase",
        "nav.roadmap": "Roadmap",
        "nav.wiki": "Wiki",
        "nav.api": "API Reference",

        // Hero
        "hero.subtitle": "The AI-Native Game Engine for the Next Generation",
        "hero.description": "Built with Vulkan & C++17 for performance, Python 3.12+ for accessibility.<br>Write game logic in Python. Natively integrate ML models. Vibe Code your games.",
        "hero.reference": "Architecture inspired by Xi Wang's ",
        "hero.reference2": " course",
        "hero.viewGithub": "View on GitHub",
        "hero.roadmap": "Roadmap",

        // Features
        "features.title": "Why Python as Game Engine Frontend?",
        "features.ai.title": "AI Integration is Native",
        "features.ai.desc": "Import PyTorch, TensorFlow, OpenCV directly in your game scripts. No plugin layer. No bridging. Just <code>import torch</code>.",
        "features.vibe.title": "Vibe Coding Ready",
        "features.vibe.desc": "LLMs generate Python more accurately than any other language. Your game logic is naturally LLM-friendly.",
        "features.numpy.title": "NumPy Render Readback",
        "features.numpy.desc": "Read render pass pixels directly as NumPy arrays. Perfect for real-time CV, RL, and GPU-to-CPU pipelines.",
        "features.unity.title": "Unity-Style API",
        "features.unity.desc": "Familiar Component-GameObject architecture with lifecycle methods. URP-inspired Scriptable Render Pipeline in Python.",
        "features.vulkan.title": "Vulkan Powered",
        "features.vulkan.desc": "C++17 backend with Vulkan for maximum GPU performance. Python defines what to render, C++ handles how.",
        "features.opensource.title": "Open Source",
        "features.opensource.desc": "MIT licensed. No royalties, no restrictions. Full access to engine source code.",

        // Showcase
        "showcase.title": "What's Already Built",
        "showcase.subtitle": "Infernux is more than a prototype — here's what's working today.",
        "showcase.rendering.title": "Rendering",
        "showcase.rendering.1": "Vulkan forward renderer",
        "showcase.rendering.2": "PBR-inspired materials (metallic/roughness)",
        "showcase.rendering.3": "4x MSAA anti-aliasing",
        "showcase.rendering.4": "Directional shadow mapping (PCF)",
        "showcase.rendering.5": "Post-processing pipeline",
        "showcase.rendering.6": "Shader hot-reload & SPIR-V reflection",
        "showcase.rendering.7": "Selection outline & editor gizmos",
        "showcase.arch.title": "Render Architecture",
        "showcase.arch.1": "Scriptable Render Pipeline (URP-style SRP)",
        "showcase.arch.2": "ScriptableRenderContext & CommandBuffer",
        "showcase.arch.3": "RenderGraph (Python topology → C++ DAG)",
        "showcase.arch.4": "Transient resource pool",
        "showcase.arch.5": "Frustum culling with CullingResults",
        "showcase.arch.6": "GPU→CPU readback as NumPy arrays",
        "showcase.scene.title": "Scene & Components",
        "showcase.scene.1": "GameObject-Component (Unity-style)",
        "showcase.scene.2": "Transform hierarchy (parent-child)",
        "showcase.scene.3": "Light component (Directional/Point/Spot)",
        "showcase.scene.4": "Camera, MeshRenderer, primitives",
        "showcase.scene.5": "Scene serialization (JSON) & picking",
        "showcase.scene.6": "Play mode (snapshot & restore)",
        "showcase.python.title": "Python Scripting",
        "showcase.python.1": "InfComponent base class with lifecycle",
        "showcase.python.2": "@serialized_field with Inspector UI",
        "showcase.python.3": "@require_component, @disallow_multiple, @execute_in_edit_mode",
        "showcase.python.4": "Component serialization / deserialization",
        "showcase.python.5": "PyComponentProxy C++↔Python bridge",
        "showcase.python.6": "Unity-style Input system (get_key, get_axis…)",
        "showcase.editor.title": "Editor (ImGui)",
        "showcase.editor.1": "Hierarchy, Inspector, Scene View",
        "showcase.editor.2": "Game View, Project Browser, Console",
        "showcase.editor.3": "Material editor in Inspector",
        "showcase.editor.4": "Menu bar, Toolbar (Play/Pause/Stop)",
        "showcase.editor.5": "Drag & drop, windows closable/reopenable",
        "showcase.resources.title": "Resources & Launcher",
        "showcase.resources.1": "AssetDatabase with GUID & .meta files",
        "showcase.resources.2": "Texture (stb), Mesh (Assimp), Shader loading",
        "showcase.resources.3": "Material system (.mat JSON files)",
        "showcase.resources.4": "PySide6 project launcher (MVVM)",
        "showcase.resources.5": "SQLite project database",

        // Code Examples
        "examples.title": "See It in Action",
        "examples.tab1": "Component",
        "examples.tab2": "Render Pipeline",
        "examples.tab3": "AI Integration",

        // Architecture
        "arch.title": "Architecture",
        "arch.subtitle": "C++ handles the hot loops. Python handles the logic. pybind11 bridges both worlds.",
        "arch.layer.launcher": "Launcher",
        "arch.layer.python": "Python Editor & Scripts",
        "arch.layer.binding": "pybind11 Binding Layer",
        "arch.layer.core": "C++ Engine Core",
        "arch.layer.render": "Vulkan Renderer",
        "arch.layer.platform": "Platform Layer",

        // Tech Stack
        "techstack.title": "Tech Stack",

        // CTA
        "cta.title": "Ready to Build the Future?",
        "cta.desc": "Infernux is open source and actively developed. Star the repo, fork it, or contribute.",
        "cta.star": "Star on GitHub",
        "cta.roadmap": "View Roadmap",

        // Footer
        "footer.tagline": "The AI-Native Game Engine",
        "footer.resources": "Resources",
        "footer.community": "Community",
        "footer.issues": "Issues",
        "footer.discussions": "Discussions",

        // Roadmap
        "roadmap.title": "Development Roadmap",
        "roadmap.subtitle": "Track our progress building the AI-native game engine",
        "roadmap.hint": "Drag nodes to reposition \u00b7 Release to spring back",
        "roadmap.reset": "Reset Layout",
        "roadmap.status.completed": "Completed",
        "roadmap.status.inProgress": "In Progress",
        "roadmap.status.planned": "Planned",
        "roadmap.cat.rendering": "Rendering System",
        "roadmap.cat.animation": "Animation System",
        "roadmap.cat.audio": "Audio System",
        "roadmap.cat.gameplay": "GamePlay System",
        "roadmap.cat.toolchain": "Toolchain & Editor",
        "roadmap.cat.network": "Network System",
        "roadmap.cat.dop": "Data-Oriented & Performance",
        "roadmap.cat.infra": "Infrastructure",
        "roadmap.cat.ai": "AI & Machine Learning",
        "roadmap.contribute.title": "Want to Help Shape the Future?",
        "roadmap.contribute.desc": "We welcome contributions of all kinds — code, documentation, ideas, and feedback.",
        "roadmap.contribute.suggest": "Suggest Features",
        "roadmap.contribute.contribute": "Contribute",

        // Wiki Coming Soon
        "wiki.coming.title": "Documentation Coming Soon",
        "wiki.coming.desc": "We're working on comprehensive tutorials and API documentation. In the meantime, check out the README on GitHub for getting started.",
        "wiki.coming.readme": "Read README",
        "wiki.coming.roadmap": "View Roadmap"
    },

    zh: {
        // Navigation
        "nav.home": "首页",
        "nav.features": "特性",
        "nav.showcase": "功能展示",
        "nav.roadmap": "路线图",
        "nav.wiki": "Wiki",
        "nav.api": "接口文档",

        // Hero
        "hero.subtitle": "面向AI时代的下一代游戏引擎",
        "hero.description": "以Vulkan和C++17构建高性能核心，以Python 3.12+提供便捷接口。<br>用Python编写游戏逻辑，原生集成ML模型，用Vibe Coding开发游戏。",
        "hero.reference": "架构设计参考王希的",
        "hero.reference2": "课程",
        "hero.viewGithub": "查看GitHub",
        "hero.roadmap": "路线图",

        // Features
        "features.title": "为什么用 Python 做游戏引擎前端？",
        "features.ai.title": "AI 集成是原生的",
        "features.ai.desc": "直接在游戏脚本中导入 PyTorch、TensorFlow、OpenCV。无需插件层，无需桥接，直接 <code>import torch</code>。",
        "features.vibe.title": "Vibe Coding 就绪",
        "features.vibe.desc": "LLM 生成 Python 代码比任何其他语言都更精确。你的游戏逻辑天然对 LLM 友好。",
        "features.numpy.title": "NumPy 渲染回读",
        "features.numpy.desc": "将渲染 Pass 的像素直接读取为 NumPy 数组，适用于实时 CV、RL 和 GPU→CPU 管线。",
        "features.unity.title": "Unity 风格 API",
        "features.unity.desc": "熟悉的组件-游戏对象架构与生命周期方法。在 Python 中使用 URP 风格的可编程渲染管线。",
        "features.vulkan.title": "Vulkan 驱动",
        "features.vulkan.desc": "C++17 后端配合 Vulkan 实现极致 GPU 性能。Python 定义渲染什么，C++ 负责怎么渲染。",
        "features.opensource.title": "开源免费",
        "features.opensource.desc": "MIT 开源协议，无版税，无限制。完全访问引擎源代码。",

        // Showcase
        "showcase.title": "已实现的功能",
        "showcase.subtitle": "Infernux 不只是原型 —— 以下功能已经可以工作。",
        "showcase.rendering.title": "渲染",
        "showcase.rendering.1": "Vulkan 前向渲染器",
        "showcase.rendering.2": "PBR 风格材质（金属度/粗糙度）",
        "showcase.rendering.3": "4x MSAA 抗锯齿",
        "showcase.rendering.4": "方向光阴影映射（PCF 软阴影）",
        "showcase.rendering.5": "后处理管线",
        "showcase.rendering.6": "Shader 热重载 & SPIR-V 反射",
        "showcase.rendering.7": "选中轮廓线 & 编辑器 Gizmos",
        "showcase.arch.title": "渲染架构",
        "showcase.arch.1": "可编程渲染管线（URP 风格 SRP）",
        "showcase.arch.2": "ScriptableRenderContext & CommandBuffer",
        "showcase.arch.3": "RenderGraph（Python 拓扑 → C++ DAG）",
        "showcase.arch.4": "瞬态资源池",
        "showcase.arch.5": "视锥体剔除与 CullingResults",
        "showcase.arch.6": "GPU→CPU 回读为 NumPy 数组",
        "showcase.scene.title": "场景 & 组件",
        "showcase.scene.1": "GameObject-Component（Unity 风格）",
        "showcase.scene.2": "Transform 层级（父子关系）",
        "showcase.scene.3": "Light 组件（方向光/点光/聚光）",
        "showcase.scene.4": "Camera、MeshRenderer、基础几何体",
        "showcase.scene.5": "场景序列化（JSON）& 拾取",
        "showcase.scene.6": "Play 模式（快照 & 恢复）",
        "showcase.python.title": "Python 脚本",
        "showcase.python.1": "InfComponent 基类与生命周期",
        "showcase.python.2": "@serialized_field（Inspector UI 显示）",
        "showcase.python.3": "@require_component、@disallow_multiple、@execute_in_edit_mode",
        "showcase.python.4": "组件序列化/反序列化",
        "showcase.python.5": "PyComponentProxy C++↔Python 桥接层",
        "showcase.python.6": "Unity 风格输入系统（get_key、get_axis…）",
        "showcase.editor.title": "编辑器（ImGui）",
        "showcase.editor.1": "Hierarchy、Inspector、Scene View",
        "showcase.editor.2": "Game View、Project Browser、Console",
        "showcase.editor.3": "Inspector 中的材质编辑器",
        "showcase.editor.4": "菜单栏、工具栏（Play/Pause/Stop）",
        "showcase.editor.5": "拖放支持、窗口可关闭/重新打开",
        "showcase.resources.title": "资源 & 启动器",
        "showcase.resources.1": "AssetDatabase 与 GUID & .meta 文件",
        "showcase.resources.2": "纹理（stb）、网格（Assimp）、Shader 加载",
        "showcase.resources.3": "材质系统（.mat JSON 文件）",
        "showcase.resources.4": "PySide6 项目启动器（MVVM）",
        "showcase.resources.5": "SQLite 项目数据库",

        // Code Examples
        "examples.title": "功能演示",
        "examples.tab1": "组件",
        "examples.tab2": "渲染管线",
        "examples.tab3": "AI 集成",

        // Architecture
        "arch.title": "架构",
        "arch.subtitle": "C++ 处理高频循环，Python 处理逻辑，pybind11 桥接两者。",
        "arch.layer.launcher": "启动器",
        "arch.layer.python": "Python 编辑器 & 脚本",
        "arch.layer.binding": "pybind11 绑定层",
        "arch.layer.core": "C++ 引擎核心",
        "arch.layer.render": "Vulkan 渲染器",
        "arch.layer.platform": "平台抽象层",

        // Tech Stack
        "techstack.title": "技术栈",

        // CTA
        "cta.title": "准备好创造未来了吗？",
        "cta.desc": "Infernux 是开源的，正在积极开发中。Star 仓库、Fork 或参与贡献。",
        "cta.star": "在 GitHub 上 Star",
        "cta.roadmap": "查看路线图",

        // Footer
        "footer.tagline": "AI 原生游戏引擎",
        "footer.resources": "资源",
        "footer.community": "社区",
        "footer.issues": "问题反馈",
        "footer.discussions": "讨论区",

        // Roadmap
        "roadmap.title": "开发路线图",
        "roadmap.subtitle": "追踪我们构建 AI 原生游戏引擎的进展",
        "roadmap.hint": "拖动节点以重新定位 · 松开后自动弹回",
        "roadmap.reset": "重置布局",
        "roadmap.status.completed": "已完成",
        "roadmap.status.inProgress": "进行中",
        "roadmap.status.planned": "计划中",
        "roadmap.cat.rendering": "渲染系统",
        "roadmap.cat.animation": "动画系统",
        "roadmap.cat.audio": "音效系统",
        "roadmap.cat.gameplay": "GamePlay 系统",
        "roadmap.cat.toolchain": "工具链与编辑器",
        "roadmap.cat.network": "网络系统",
        "roadmap.cat.dop": "面向数据与性能",
        "roadmap.cat.infra": "基础架构",
        "roadmap.cat.ai": "AI 与机器学习",
        "roadmap.contribute.title": "想要一起塑造未来吗？",
        "roadmap.contribute.desc": "我们欢迎各种形式的贡献 —— 代码、文档、创意和反馈。",
        "roadmap.contribute.suggest": "提出建议",
        "roadmap.contribute.contribute": "参与贡献",

        // Wiki Coming Soon
        "wiki.coming.title": "文档即将推出",
        "wiki.coming.desc": "我们正在编写全面的教程和 API 文档。在此期间，请查看 GitHub 上的 README 了解如何开始。",
        "wiki.coming.readme": "阅读 README",
        "wiki.coming.roadmap": "查看路线图"
    }
};

// Current language
let currentLang = localStorage.getItem('infernux-lang') || 'en';

/**
 * Apply translations to all elements with data-i18n attribute
 */
function applyTranslations() {
    const elements = document.querySelectorAll('[data-i18n]');
    elements.forEach(el => {
        const key = el.getAttribute('data-i18n');
        if (translations[currentLang] && translations[currentLang][key]) {
            if (translations[currentLang][key].includes('<')) {
                el.innerHTML = translations[currentLang][key];
            } else {
                el.textContent = translations[currentLang][key];
            }
        }
    });

    const langText = document.getElementById('lang-text');
    if (langText) {
        langText.textContent = currentLang === 'en' ? '中文' : 'English';
    }
    document.documentElement.lang = currentLang;
}

/**
 * Toggle between English and Chinese
 */
function toggleLanguage() {
    currentLang = currentLang === 'en' ? 'zh' : 'en';
    localStorage.setItem('infernux-lang', currentLang);
    applyTranslations();
}

// Apply translations on page load
document.addEventListener('DOMContentLoaded', applyTranslations);
