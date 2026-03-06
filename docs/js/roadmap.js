document.addEventListener("DOMContentLoaded", () => {
    const BRANCHES = [
      {
        id: "rendering", en: "Rendering", zh: "渲染", icon: "⬡", col: "rendering",
        children: [
          { id: "r_vulkan",   en: "Vulkan Core",         zh: "Vulkan 核心",      s: "done"    },
          { id: "r_pipeline", en: "GPU Pipeline",        zh: "GPU 管线",         s: "done"    },
          { id: "r_srp",      en: "Scriptable RP",       zh: "可编程渲染管线",   s: "done"    },
          { id: "r_rg",       en: "Render Graph",        zh: "渲染图",           s: "done"    },
          { id: "r_shadow",   en: "Shadows",             zh: "阴影",             s: "partial" },
          { id: "r_post",     en: "Post Processing",     zh: "后处理",           s: "partial" },
          { id: "r_pbr",      en: "PBR Materials",       zh: "PBR 材质",         s: "partial" },
          { id: "r_gi",       en: "Global Illumination", zh: "全局光照",         s: "planned" },
          { id: "r_adv",      en: "Advanced Rendering",  zh: "高级渲染",         s: "planned" },
        ],
      },
      {
        id: "gameplay", en: "Gameplay", zh: "游戏逻辑", icon: "◈", col: "gameplay",
        children: [
          { id: "g_core",    en: "Core Architecture",   zh: "核心架构",         s: "done"    },
          { id: "g_scene",   en: "Scene System",        zh: "场景系统",         s: "done"    },
          { id: "g_play",    en: "Play Mode",           zh: "Play 模式",        s: "done"    },
          { id: "g_builtin", en: "Built-in Components", zh: "内置组件",         s: "done"    },
          { id: "g_physics", en: "Physics (Jolt)",      zh: "物理 (Jolt)",      s: "planned" },
          { id: "g_input",   en: "Input System",        zh: "输入系统",         s: "done"    },
        ],
      },
      {
        id: "toolchain", en: "Toolchain", zh: "工具链", icon: "◫", col: "toolchain",
        children: [
          { id: "t_editor",   en: "Editor Core",        zh: "编辑器核心",       s: "done"    },
          { id: "t_panels",   en: "Editor Panels",      zh: "编辑器面板",       s: "done"    },
          { id: "t_assets",   en: "Asset Pipeline",     zh: "资产管线",         s: "done"    },
          { id: "t_launcher", en: "Launcher",           zh: "启动器",           s: "done"    },
          { id: "t_tools",    en: "Advanced Tools",     zh: "高级工具",         s: "planned" },
          { id: "t_build",    en: "Build & Export",     zh: "构建与导出",       s: "planned" },
        ],
      },
      {
        id: "infra", en: "Infrastructure", zh: "基础设施", icon: "○", col: "infra",
        children: [
          { id: "inf_py",   en: "Python Binding",   zh: "Python 绑定",      s: "done"    },
          { id: "inf_ser",  en: "Serialization",    zh: "序列化",           s: "partial" },
          { id: "inf_core", en: "Core Systems",     zh: "核心系统",         s: "done"    },
          { id: "inf_plat", en: "Platform Layer",   zh: "平台层",           s: "partial" },
        ],
      },
      {
        id: "animation", en: "Animation", zh: "动画", icon: "◌", col: "animation",
        children: [
          { id: "a_skel",  en: "Skeletal Anim",    zh: "骨骼动画",         s: "planned" },
          { id: "a_blend", en: "Blending",         zh: "动画混合",         s: "planned" },
          { id: "a_ctrl",  en: "State Machine",    zh: "状态机",           s: "planned" },
          { id: "a_proc",  en: "Procedural Anim",  zh: "程序化动画",       s: "planned" },
        ],
      },
      {
        id: "audio", en: "Audio", zh: "音频", icon: "◎", col: "audio",
        children: [
          { id: "au_core", en: "Audio Backend",    zh: "音频后端",         s: "planned" },
          { id: "au_3d",   en: "Spatial Audio",    zh: "空间音效",         s: "planned" },
          { id: "au_mix",  en: "Mixing & FX",      zh: "混音与特效",       s: "planned" },
        ],
      },
      {
        id: "dop", en: "Performance", zh: "性能", icon: "◇", col: "dop",
        children: [
          { id: "d_ecs",  en: "ECS Architecture",  zh: "ECS 架构",         s: "planned" },
          { id: "d_job",  en: "Job System",        zh: "任务系统",         s: "planned" },
          { id: "d_mem",  en: "Memory Alloc",      zh: "内存分配器",       s: "planned" },
          { id: "d_simd", en: "SIMD / SoA",        zh: "SIMD / 数组",     s: "planned" },
        ],
      },
      {
        id: "network", en: "Networking", zh: "网络", icon: "◉", col: "network",
        children: [
          { id: "n_core", en: "Transport Layer",   zh: "传输层",           s: "planned" },
          { id: "n_rep",  en: "State Replication", zh: "状态同步",         s: "planned" },
          { id: "n_game", en: "Rollback / Predict",zh: "回滚预测",         s: "planned" },
        ],
      },
      {
        id: "ai", en: "AI & ML", zh: "AI 与机器学习", icon: "◐", col: "ai",
        children: [
          { id: "ai_game", en: "Game AI (BT/FSM)", zh: "游戏 AI (BT/FSM)", s: "planned" },
          { id: "ai_ml",   en: "ML Integration",  zh: "ML 集成",          s: "planned" },
          { id: "ai_llm",  en: "LLM / Vibe Code", zh: "LLM / Vibe 编程",  s: "planned" },
          { id: "ai_proc", en: "Procedural Gen",  zh: "程序化生成",        s: "planned" },
        ],
      },
    ];
  
    const container = document.getElementById("roadmap-timeline");
    if (!container) return;
  
    function getLabel(item) {
      const lang = document.documentElement.lang || "en";
      return lang === "zh" ? item.zh : item.en;
    }
  
    function renderTimeline() {
      container.innerHTML = "";
      
      BRANCHES.forEach((branch, index) => {
        const node = document.createElement("div");
        node.className = "rm-node";
        node.style.animationDelay = `${index * 0.15}s`;
  
        // Marker
        const marker = document.createElement("div");
        marker.className = "rm-marker";
        marker.innerText = branch.icon;
        
        // Content Block
        const contentBlock = document.createElement("div");
        contentBlock.className = "rm-content";
        
        // Header
        const title = document.createElement("div");
        title.className = "rm-title";
        title.innerHTML = `<span class="rm-title-icon">${branch.icon}</span> <span>${getLabel(branch)}</span>`;
  
        // Tasks grid
        const tasksGrid = document.createElement("div");
        tasksGrid.className = "rm-tasks";
        
        branch.children.forEach(child => {
          const tItem = document.createElement("div");
          tItem.className = "rm-task-item";
          tItem.innerHTML = `
            <div class="rm-task-status ${child.s}"></div>
            <div class="rm-task-name">${getLabel(child)}</div>
          `;
          tasksGrid.appendChild(tItem);
        });
  
        contentBlock.appendChild(title);
        contentBlock.appendChild(tasksGrid);
        
        node.appendChild(marker);
        node.appendChild(contentBlock);
        
        container.appendChild(node);
      });
    }
  
    renderTimeline();
  
    // Listen for language changes
    const observer = new MutationObserver((mutations) => {
      mutations.forEach((mutation) => {
        if (mutation.attributeName === "lang") {
          renderTimeline();
        }
      });
    });
    
    observer.observe(document.documentElement, { attributes: true });
  });