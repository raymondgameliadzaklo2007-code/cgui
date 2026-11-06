import React, { useState, useEffect, useRef } from 'react';
import {
  Folder,
  FolderOpen,
  ChevronRight,
  ChevronDown,
  Play,
  Hammer,
  Bug,
  Search,
  Settings,
  GitBranch,
  Terminal,
  Trash2,
  RotateCcw,
  X,
  Plus,
  FileCode,
  FileText,
  FileCog,
  File,
  AlertTriangle,
  Info,
  CheckCircle2,
  Maximize2,
  Minimize2,
  SplitSquareVertical,
  Sliders,
  Sparkles,
  Command,
  Sun,
  Moon,
  FolderPlus,
  FilePlus,
  Check
} from 'lucide-react';

interface FileItem {
  id: string;
  name: string;
  path: string;
  type: 'file' | 'directory';
  children?: FileItem[];
  content?: string;
  language?: string;
}

interface OpenTab {
  id: string;
  name: string;
  path: string;
  dirty: boolean;
  content: string;
  language: string;
}

interface Diagnostic {
  severity: 'error' | 'warning' | 'info';
  message: string;
  file: string;
  line: number;
  col: number;
  code: string;
}

const INITIAL_FILES: FileItem[] = [
  {
    id: 'src',
    name: 'src',
    path: 'src',
    type: 'directory',
    children: [
      {
        id: 'main_c',
        name: 'main.c',
        path: 'src/main.c',
        type: 'file',
        language: 'c',
        content: `/**
 * main.c – cgui code editor (JetBrains New UI Layout Engine)
 * Pure C23, SDL3, Vulkan, and Clay flexbox auto-layout
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "app.h"
#include "ui_layout.h"
#include "project.h"
#include "theme.h"
#include "lsp.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    App app = {0};

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL initialization failed: %s\\n", SDL_GetError());
        return 1;
    }

    app.window = SDL_CreateWindow(
        "cgui – JetBrains Edition",
        1280, 800,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    /* Initialize Clay layout engine arena */
    ui_layout_init(&app.ui_layout, 1280.0f, 800.0f);
    app.ui_layout.active_tool = TOOL_STRIPE_PROJECT;
    app.ui_layout.sidebar_open = true;

    /* Load project & starter workspace */
    project_config_detect(&app.project_cfg, ".");
    tab_add(&app, "src/main.c", NULL);

    printf("cgui editor running with Clay layout engine.\\n");
    return 0;
}
`,
      },
      {
        id: 'ui_layout_h',
        name: 'ui_layout.h',
        path: 'src/ui_layout.h',
        type: 'file',
        language: 'c',
        content: `/**
 * ui_layout.h – Clay-powered flexbox layout manager for JetBrains New UI
 */

#ifndef CGUI_UI_LAYOUT_H
#define CGUI_UI_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "clay.h"

typedef enum ToolStripeItem {
    TOOL_STRIPE_NONE = 0,
    TOOL_STRIPE_PROJECT,
    TOOL_STRIPE_SEARCH,
    TOOL_STRIPE_GIT,
    TOOL_STRIPE_DEBUG,
    TOOL_STRIPE_SETTINGS,
} ToolStripeItem;

typedef struct UiLayout {
    bool initialized;
    Clay_Arena arena;

    /* Computed bounding boxes from Clay layout */
    Clay_BoundingBox stripe_bounds;
    Clay_BoundingBox toolbar_bounds;
    Clay_BoundingBox sidebar_bounds;
    Clay_BoundingBox tabbar_bounds;
    Clay_BoundingBox editor_bounds;
    Clay_BoundingBox dock_bounds;
    Clay_BoundingBox statusbar_bounds;

    /* Toolbar interactive widgets */
    Clay_BoundingBox run_dropdown_bounds;
    Clay_BoundingBox run_btn_bounds;
    Clay_BoundingBox build_btn_bounds;
    Clay_BoundingBox debug_btn_bounds;
} UiLayout;

bool ui_layout_init(UiLayout *ui, float width, float height);
void ui_layout_update(UiLayout *ui, struct App *app, float width, float height);

#endif /* CGUI_UI_LAYOUT_H */
`,
      },
      {
        id: 'clay_h',
        name: 'clay.h',
        path: 'src/clay.h',
        type: 'file',
        language: 'c',
        content: `// VERSION: 0.14
// Clay – High performance 2D UI layout library in C99/C23
// Zero allocations during layout, Arena-based flexbox computation

#define CLAY_SIZING_GROW(...) (CLAY__INIT(Clay_SizingAxis) { .type = CLAY__SIZING_TYPE_GROW })
#define CLAY_SIZING_FIXED(px) (CLAY__INIT(Clay_SizingAxis) { .size = { .minMax = { px, px } }, .type = CLAY__SIZING_TYPE_FIXED })

CLAY_DLL_EXPORT Clay_Context* Clay_Initialize(Clay_Arena arena, Clay_Dimensions layoutDimensions, Clay_ErrorHandler errorHandler);
CLAY_DLL_EXPORT void Clay_BeginLayout(void);
CLAY_DLL_EXPORT Clay_RenderCommandArray Clay_EndLayout(float deltaTime);
CLAY_DLL_EXPORT Clay_ElementData Clay_GetElementData(Clay_ElementId id);
`,
      },
      {
        id: 'project_c',
        name: 'project.c',
        path: 'src/project.c',
        type: 'file',
        language: 'c',
        content: `/**
 * project.c – JetBrains project management, auto-detection & scaffolding
 */

#include "project.h"
#include <stdio.h>
#include <string.h>

bool project_scaffold(const char *target_dir, ProjectTemplate tmpl, const char *name)
{
    printf("Scaffolding %s with template %d into %s\\n", name, tmpl, target_dir);
    return true;
}
`,
      },
    ],
  },
  {
    id: 'meson_build',
    name: 'meson.build',
    path: 'meson.build',
    type: 'file',
    language: 'meson',
    content: `project(
  'cgui',
  'c',
  version: '0.1.0',
  default_options: [
    'warning_level=2',
    'buildtype=debug',
    'c_std=c23',
  ],
)

sdl3_dep   = dependency('sdl3', required: true)
vulkan_dep = dependency('vulkan', required: true)

sources = files(
  'src/main.c',
  'src/ui_layout.c',
  'src/tab_bar.c',
  'src/sidebar.c',
  'src/project.c',
  'src/welcome.c',
)

executable('cgui', sources, dependencies: [sdl3_dep, vulkan_dep])
`,
  },
  {
    id: 'package_json',
    name: 'package.json',
    path: 'package.json',
    type: 'file',
    language: 'json',
    content: `{
  "name": "cgui-editor",
  "version": "0.1.0",
  "description": "JetBrains-inspired C23 editor with Clay auto-layout"
}
`,
  },
];

const TEMPLATES = [
  { id: 'c_meson', tag: 'C23', title: 'C23 Executable (Meson)', desc: 'Standard C23 binary with Meson build system and Ninja', entry: 'src/main.c', toolchain: 'Meson + GCC/Clang' },
  { id: 'c_cmake', tag: 'CMake', title: 'C/C++ Project (CMake)', desc: 'Cross-platform CMake 3.25+ workspace with C23 standard', entry: 'src/main.c', toolchain: 'CMake + Ninja' },
  { id: 'c_make', tag: 'Make', title: 'C Native (Makefile)', desc: 'Lightweight C23 project with POSIX Makefile and pkg-config', entry: 'main.c', toolchain: 'GNU Make' },
  { id: 'rust_cargo', tag: 'Rust', title: 'Rust (Cargo)', desc: 'Rust binary with Cargo.toml 2024 edition and clippy', entry: 'src/main.rs', toolchain: 'Cargo + Rustc' },
  { id: 'go_module', tag: 'Go', title: 'Go Executable', desc: 'Go module with main package and go.mod configuration', entry: 'main.go', toolchain: 'Go Toolchain' },
  { id: 'zig_build', tag: 'Zig', title: 'Zig Project (build.zig)', desc: 'Modern Zig executable using 0.13.0 build system', entry: 'src/main.zig', toolchain: 'Zig Compiler' },
  { id: 'python_pkg', tag: 'Python', title: 'Python Package', desc: 'Python 3.12+ project with pyproject.toml and src layout', entry: 'main.py', toolchain: 'Python / UV' },
  { id: 'custom_dir', tag: 'Custom', title: 'Empty Workspace', desc: 'Blank directory configured for manual build setups', entry: 'README.md', toolchain: 'User Defined' },
];

export default function App() {
  // Navigation & Tool Window state
  const [activeToolStripe, setActiveToolStripe] = useState<'project' | 'search' | 'git' | 'debug' | 'none'>('project');
  const [sidebarWidth, setSidebarWidth] = useState(250);
  const [isSidebarResizing, setIsSidebarResizing] = useState(false);

  // Bottom dock state
  const [dockOpen, setDockOpen] = useState(true);
  const [dockTab, setDockTab] = useState<'terminal' | 'problems' | 'build'>('terminal');
  const [dockHeight, setDockHeight] = useState(210);
  const [isDockResizing, setIsDockResizing] = useState(false);

  // Tabs & File tree
  const [expandedFolders, setExpandedFolders] = useState<Record<string, boolean>>({ src: true });
  const [openTabs, setOpenTabs] = useState<OpenTab[]>([
    {
      id: 'main_c',
      name: 'main.c',
      path: 'src/main.c',
      dirty: false,
      content: INITIAL_FILES[0].children![0].content!,
      language: 'c',
    },
    {
      id: 'ui_layout_h',
      name: 'ui_layout.h',
      path: 'src/ui_layout.h',
      dirty: false,
      content: INITIAL_FILES[0].children![1].content!,
      language: 'c',
    },
    {
      id: 'clay_h',
      name: 'clay.h',
      path: 'src/clay.h',
      dirty: false,
      content: INITIAL_FILES[0].children![2].content!,
      language: 'c',
    },
  ]);
  const [activeTabId, setActiveTabId] = useState('main_c');

  // Run Configurations (JetBrains Run Widget)
  const [runConfigs] = useState([
    { id: 'meson_build', name: 'Meson: Build cgui', type: 'build' },
    { id: 'cgui_debug', name: 'cgui (Debug)', type: 'run' },
    { id: 'cgui_release', name: 'cgui (Release)', type: 'run' },
    { id: 'clean', name: 'ninja clean', type: 'tool' },
  ]);
  const [activeConfig, setActiveConfig] = useState(runConfigs[0]);
  const [configDropdownOpen, setConfigDropdownOpen] = useState(false);
  const [isBuilding, setIsBuilding] = useState(false);
  const [buildStatus, setBuildStatus] = useState<string>('Ready');

  // Terminal state
  const [terminalHistory, setTerminalHistory] = useState<string[]>([
    '\x1b[36m[cgui 0.1.0]\x1b[0m Welcome to cgui JetBrains IDE shell',
    '\x1b[32m[Clay Layout Engine]\x1b[0m Arena initialized: 2048 KB',
    '\x1b[33m[Tree-Sitter]\x1b[0m Grammar loaded: C23, Meson, JSON, Lua',
    '\x1b[34m[LSP Client]\x1b[0m Connected to clangd (C23 language server)',
    '$ meson compile -C build',
    'ninja: Entering directory `build`',
    '[1/6] Compiling C object cgui.p/src_ui_layout.c.o (Clay layout flexbox)',
    '[2/6] Compiling C object cgui.p/src_main.c.o',
    '[3/6] Compiling C object cgui.p/src_sidebar.c.o',
    '[4/6] Compiling C object cgui.p/src_tab_bar.c.o',
    '[5/6] Compiling C object cgui.p/src_project.c.o',
    '[6/6] Linking target cgui (Vulkan + SDL3 + Clay)',
    '\x1b[32mBuild succeeded\x1b[0m: build/cgui (0 errors, 1 warning)',
  ]);
  const [terminalInput, setTerminalInput] = useState('');
  const terminalEndRef = useRef<HTMLDivElement>(null);

  // Diagnostics
  const [diagnostics] = useState<Diagnostic[]>([
    {
      severity: 'warning',
      message: "Variable 'unused_delta' set but not used in ui_layout_update",
      file: 'src/ui_layout.c',
      line: 42,
      col: 12,
      code: '-Wunused-variable',
    },
  ]);

  // Modals & Overlays
  const [welcomeModalOpen, setWelcomeModalOpen] = useState(false);
  const [searchModalOpen, setSearchModalOpen] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedTemplate, setSelectedTemplate] = useState(TEMPLATES[0]);
  const [newProjectName, setNewProjectName] = useState('cgui-project');
  const [newProjectLocation, setNewProjectLocation] = useState('~/Projects');

  // Editor cursor tracking
  const [cursorPos, setCursorPos] = useState({ line: 18, col: 5 });

  const activeTab = openTabs.find((t) => t.id === activeTabId) || openTabs[0];

  // Auto-scroll terminal
  useEffect(() => {
    terminalEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [terminalHistory]);

  // Handle building simulation
  const handleRunBuild = () => {
    if (isBuilding) return;
    setIsBuilding(true);
    setBuildStatus('Building…');
    setDockOpen(true);
    setDockTab('terminal');

    setTerminalHistory((prev) => [
      ...prev,
      `$ ninja -C build ${activeConfig.name.split(' ')[0]}`,
      `[1/2] Compiling src/ui_layout.c with Clay 0.14 Flexbox...`,
      `[2/2] Linking cgui...`,
      `\x1b[32m✓ Build succeeded!\x1b[0m Target: build/cgui`,
    ]);

    setTimeout(() => {
      setIsBuilding(false);
      setBuildStatus('Build succeeded');
    }, 800);
  };

  const handleTerminalSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!terminalInput.trim()) return;

    const cmd = terminalInput.trim();
    setTerminalInput('');

    let output = '';
    if (cmd === 'clear') {
      setTerminalHistory([]);
      return;
    } else if (cmd === 'help') {
      output = 'Available commands: build, run, clean, ls, git status, clay-status, clear';
    } else if (cmd === 'clay-status') {
      output = 'Clay Layout Engine: active, Arena: 2048 KB, Nodes: 48, Layout FPS: 60';
    } else if (cmd === 'git status') {
      output = 'On branch main\nYour branch is up to date with origin/main.\nNothing to commit, working tree clean';
    } else if (cmd === 'build' || cmd === 'ninja') {
      handleRunBuild();
      return;
    } else if (cmd === 'ls') {
      output = 'assets/  meson.build  package.json  src/  build/';
    } else {
      output = `cgui: executed: ${cmd}`;
    }

    setTerminalHistory((prev) => [...prev, `$ ${cmd}`, output]);
  };

  // Close tab
  const closeTab = (tabId: string, e: React.MouseEvent) => {
    e.stopPropagation();
    const newTabs = openTabs.filter((t) => t.id !== tabId);
    if (newTabs.length === 0) {
      newTabs.push({
        id: 'untitled',
        name: 'Untitled',
        path: '',
        dirty: false,
        content: '// New buffer\n',
        language: 'c',
      });
    }
    setOpenTabs(newTabs);
    if (activeTabId === tabId) {
      setActiveTabId(newTabs[0].id);
    }
  };

  // Open file from tree
  const openFile = (file: FileItem) => {
    if (file.type === 'directory') {
      setExpandedFolders((prev) => ({ ...prev, [file.id]: !prev[file.id] }));
      return;
    }
    const existing = openTabs.find((t) => t.id === file.id);
    if (existing) {
      setActiveTabId(file.id);
    } else {
      const newTab: OpenTab = {
        id: file.id,
        name: file.name,
        path: file.path,
        dirty: false,
        content: file.content || `// ${file.name}\n`,
        language: file.language || 'c',
      };
      setOpenTabs([...openTabs, newTab]);
      setActiveTabId(file.id);
    }
  };

  // Icon selector helper
  const getFileIcon = (name: string) => {
    if (name.endsWith('.c') || name.endsWith('.h')) {
      return <FileCode className="w-4 h-4 text-[#59A869]" />;
    }
    if (name === 'meson.build' || name.endsWith('.json') || name.endsWith('.toml')) {
      return <FileCog className="w-4 h-4 text-[#E5C07B]" />;
    }
    if (name.endsWith('.md') || name.endsWith('.txt')) {
      return <FileText className="w-4 h-4 text-[#61AFEF]" />;
    }
    return <File className="w-4 h-4 text-[#8C92A4]" />;
  };

  // Create project from wizard
  const handleCreateProject = () => {
    const entryName = selectedTemplate.entry;
    const newTab: OpenTab = {
      id: `${newProjectName}_entry`,
      name: entryName.split('/').pop() || 'main.c',
      path: `${newProjectName}/${entryName}`,
      dirty: false,
      content: `/**\n * Starter for ${newProjectName} (${selectedTemplate.tag})\n * Toolchain: ${selectedTemplate.toolchain}\n */\n\n#include <stdio.h>\n\nint main(void) {\n    printf("Hello from ${newProjectName}!\\\\n");\n    return 0;\n}\n`,
      language: 'c',
    };
    setOpenTabs([...openTabs, newTab]);
    setActiveTabId(newTab.id);
    setWelcomeModalOpen(false);
    setTerminalHistory((prev) => [
      ...prev,
      `\x1b[32m[Scaffold]\x1b[0m Generated new ${selectedTemplate.tag} project: ${newProjectLocation}/${newProjectName}`,
    ]);
  };

  return (
    <div className="flex flex-col h-screen w-screen bg-[#1E1F22] text-[#BCBEC4] overflow-hidden select-none">
      {/* =========================================================================
       * 1. TOP MAIN TOOLBAR (JetBrains New UI Header Bar)
       * ========================================================================= */}
      <header className="h-10 bg-[#1E1F22] border-b border-[#2B2D30] flex items-center justify-between px-3 shrink-0 z-20">
        {/* Left: Branding & Project Navigator Breadcrumb */}
        <div className="flex items-center space-x-2">
          {/* JetBrains Icon Pill */}
          <div className="flex items-center space-x-1.5 px-2 py-1 rounded bg-[#2B2D30] text-xs font-semibold text-[#DFE1E5]">
            <span className="w-2.5 h-2.5 rounded-full bg-[#3574F0] inline-block animate-pulse"></span>
            <span>cgui</span>
          </div>

          <span className="text-xs text-[#5F6368]">/</span>

          {/* Project Title */}
          <div className="flex items-center space-x-1 text-xs text-[#DFE1E5] font-medium hover:bg-[#2B2D30] px-2 py-1 rounded cursor-pointer transition">
            <span>cgui-editor</span>
            <ChevronDown className="w-3 h-3 text-[#7C808B]" />
          </div>

          {/* Git Branch Widget */}
          <button
            onClick={() => {
              setDockOpen(true);
              setDockTab('terminal');
              setTerminalHistory((p) => [...p, '$ git status', 'On branch main. All synced.']);
            }}
            className="flex items-center space-x-1.5 text-xs text-[#8C92A4] hover:text-[#DFE1E5] hover:bg-[#2B2D30] px-2 py-1 rounded transition"
            title="Current Git Branch: main"
          >
            <GitBranch className="w-3.5 h-3.5 text-[#59A869]" />
            <span>main</span>
          </button>
        </div>

        {/* Center: JETBRAINS RUN CONFIGURATION WIDGET */}
        <div className="flex items-center space-x-1 bg-[#2B2D30] p-0.5 rounded-md border border-[#393B40] shadow-sm">
          {/* Configuration Dropdown Selector */}
          <div className="relative">
            <button
              onClick={() => setConfigDropdownOpen(!configDropdownOpen)}
              className="flex items-center space-x-2 px-2.5 py-1 text-xs text-[#DFE1E5] hover:bg-[#36383D] rounded transition font-medium"
            >
              <Hammer className="w-3.5 h-3.5 text-[#61AFEF]" />
              <span className="max-w-[130px] truncate">{activeConfig.name}</span>
              <ChevronDown className="w-3 h-3 text-[#7C808B]" />
            </button>

            {/* Dropdown Menu */}
            {configDropdownOpen && (
              <div className="absolute top-8 left-0 w-56 bg-[#2B2D30] border border-[#393B40] rounded shadow-2xl py-1 z-50 text-xs">
                <div className="px-2 py-1 text-[10px] uppercase font-semibold text-[#7C808B] tracking-wider">
                  Run Configurations
                </div>
                {runConfigs.map((cfg) => (
                  <button
                    key={cfg.id}
                    onClick={() => {
                      setActiveConfig(cfg);
                      setConfigDropdownOpen(false);
                    }}
                    className={`w-full flex items-center justify-between px-3 py-1.5 hover:bg-[#3574F0] hover:text-white transition ${
                      activeConfig.id === cfg.id ? 'bg-[#36383D] text-[#DFE1E5]' : 'text-[#BCBEC4]'
                    }`}
                  >
                    <div className="flex items-center space-x-2">
                      {cfg.type === 'build' ? (
                        <Hammer className="w-3.5 h-3.5 text-[#61AFEF]" />
                      ) : (
                        <Play className="w-3.5 h-3.5 text-[#59A869]" />
                      )}
                      <span>{cfg.name}</span>
                    </div>
                    {activeConfig.id === cfg.id && <Check className="w-3.5 h-3.5" />}
                  </button>
                ))}
              </div>
            )}
          </div>

          <div className="w-[1px] h-4 bg-[#393B40]"></div>

          {/* Run Button [ ▶ ] */}
          <button
            onClick={handleRunBuild}
            disabled={isBuilding}
            className="p-1.5 hover:bg-[#369963]/20 text-[#59A869] hover:text-[#68BB7B] rounded transition flex items-center justify-center disabled:opacity-50"
            title="Run active target (Shift+F10)"
          >
            <Play className={`w-3.5 h-3.5 fill-current ${isBuilding ? 'animate-spin' : ''}`} />
          </button>

          {/* Build Hammer [ 🔨 ] */}
          <button
            onClick={handleRunBuild}
            disabled={isBuilding}
            className="p-1.5 hover:bg-[#3574F0]/20 text-[#61AFEF] hover:text-[#82C3FF] rounded transition flex items-center justify-center disabled:opacity-50"
            title="Build target with Meson/Ninja (Ctrl+F9)"
          >
            <Hammer className="w-3.5 h-3.5" />
          </button>

          {/* Debug Button [ 🐞 ] */}
          <button
            onClick={() => {
              setDockOpen(true);
              setDockTab('terminal');
              setTerminalHistory((p) => [
                ...p,
                '$ gdb build/cgui',
                'Reading symbols from build/cgui...done.',
                '(gdb) run',
                'Starting program: build/cgui',
                '[Thread 1: main loaded with Clay layout arena]',
              ]);
            }}
            className="p-1.5 hover:bg-[#E5C07B]/20 text-[#E5C07B] rounded transition flex items-center justify-center"
            title="Debug (Shift+F9)"
          >
            <Bug className="w-3.5 h-3.5" />
          </button>

          {/* Stop / Rerun [ ⟳ ] */}
          <button
            onClick={handleRunBuild}
            className="p-1.5 hover:bg-[#4E5157] text-[#8C92A4] hover:text-[#DFE1E5] rounded transition flex items-center justify-center"
            title="Rerun (Ctrl+F5)"
          >
            <RotateCcw className="w-3.5 h-3.5" />
          </button>
        </div>

        {/* Right: Search Everywhere, New Project Wizard, Settings */}
        <div className="flex items-center space-x-1.5">
          {/* Search Everywhere Pill (Double Shift in JetBrains) */}
          <button
            onClick={() => setSearchModalOpen(true)}
            className="flex items-center space-x-2 px-2.5 py-1 text-xs text-[#8C92A4] hover:text-[#DFE1E5] hover:bg-[#2B2D30] border border-[#2B2D30] hover:border-[#393B40] rounded transition"
            title="Search Everywhere (Double Shift)"
          >
            <Search className="w-3.5 h-3.5" />
            <span className="hidden sm:inline">Search</span>
            <kbd className="px-1 text-[10px] bg-[#2B2D30] border border-[#393B40] rounded text-[#7C808B]">⇧⇧</kbd>
          </button>

          {/* New Project Button */}
          <button
            onClick={() => setWelcomeModalOpen(true)}
            className="flex items-center space-x-1.5 px-2.5 py-1 text-xs font-medium bg-[#3574F0] hover:bg-[#3068D9] text-white rounded shadow-sm transition"
            title="New Project Generator (Xcode / JetBrains Template Hub)"
          >
            <Plus className="w-3.5 h-3.5" />
            <span>New Project</span>
          </button>

          {/* Settings */}
          <button
            onClick={() => {
              setTerminalHistory((p) => [...p, 'Settings: Clay layout enabled, dark theme active']);
            }}
            className="p-1.5 text-[#8C92A4] hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded transition"
            title="Settings (Ctrl+Alt+S)"
          >
            <Settings className="w-4 h-4" />
          </button>
        </div>
      </header>

      {/* =========================================================================
       * 2. MAIN BODY (Tool Stripe + Sidebar + Main Workspace)
       * ========================================================================= */}
      <div className="flex flex-1 overflow-hidden">
        {/* LEFT TOOL WINDOW STRIPE (JetBrains 40px Icon Rail) */}
        <aside className="w-10 bg-[#1E1F22] border-r border-[#2B2D30] flex flex-col justify-between py-2 items-center shrink-0 z-10">
          {/* Top Tool Window Buttons */}
          <div className="flex flex-col space-y-1 w-full items-center">
            {/* Project (Files) */}
            <button
              onClick={() => {
                if (activeToolStripe === 'project') {
                  setActiveToolStripe('none');
                } else {
                  setActiveToolStripe('project');
                }
              }}
              className={`relative w-8 h-8 flex items-center justify-center rounded transition ${
                activeToolStripe === 'project'
                  ? 'bg-[#2B2D30] text-[#DFE1E5]'
                  : 'text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60'
              }`}
              title="Project Tool Window (Alt+1)"
            >
              {activeToolStripe === 'project' && (
                <div className="absolute left-[-4px] top-1.5 bottom-1.5 w-1 bg-[#3574F0] rounded-r"></div>
              )}
              <Folder className="w-4 h-4" />
            </button>

            {/* Search Everywhere / Find in Files */}
            <button
              onClick={() => setSearchModalOpen(true)}
              className="w-8 h-8 flex items-center justify-center rounded text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60 transition"
              title="Find in Files (Ctrl+Shift+F)"
            >
              <Search className="w-4 h-4" />
            </button>

            {/* Git / Version Control */}
            <button
              onClick={() => {
                setDockOpen(true);
                setDockTab('terminal');
                setTerminalHistory((p) => [...p, '$ git log -n 3 --oneline', '3f9a21d Overhaul UI layout engine to Clay flexbox', '8c22bb1 Add Lucide stroke-based icon set', '140e9f0 Initial C23 Vulkan cgui base']);
              }}
              className="w-8 h-8 flex items-center justify-center rounded text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60 transition"
              title="Git / Version Control (Alt+9)"
            >
              <GitBranch className="w-4 h-4" />
            </button>

            {/* Debug & Run */}
            <button
              onClick={() => {
                setDockOpen(true);
                setDockTab('terminal');
              }}
              className="w-8 h-8 flex items-center justify-center rounded text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60 transition"
              title="Run & Debug Panel"
            >
              <Bug className="w-4 h-4" />
            </button>
          </div>

          {/* Bottom Tool Stripe Items */}
          <div className="flex flex-col space-y-1 w-full items-center">
            {/* Problems indicator button */}
            <button
              onClick={() => {
                setDockOpen(true);
                setDockTab('problems');
              }}
              className="relative w-8 h-8 flex items-center justify-center rounded text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60 transition"
              title="Problems & Diagnostics (0 errors, 1 warning)"
            >
              <AlertTriangle className="w-4 h-4 text-[#F2C94C]" />
              <span className="absolute top-1 right-1 w-2 h-2 rounded-full bg-[#F2C94C]"></span>
            </button>

            {/* Terminal toggle button */}
            <button
              onClick={() => {
                setDockOpen(!dockOpen);
                setDockTab('terminal');
              }}
              className={`w-8 h-8 flex items-center justify-center rounded transition ${
                dockOpen && dockTab === 'terminal'
                  ? 'bg-[#2B2D30] text-[#DFE1E5]'
                  : 'text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60'
              }`}
              title="Toggle Terminal (Alt+F12)"
            >
              <Terminal className="w-4 h-4" />
            </button>

            {/* Settings */}
            <button
              onClick={() => setWelcomeModalOpen(true)}
              className="w-8 h-8 flex items-center justify-center rounded text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]/60 transition"
              title="New Project & Templates Hub"
            >
              <Settings className="w-4 h-4" />
            </button>
          </div>
        </aside>

        {/* LEFT PROJECT DRAWER (File Tree Tool Window) */}
        {activeToolStripe === 'project' && (
          <div
            style={{ width: `${sidebarWidth}px` }}
            className="bg-[#1E1F22] border-r border-[#2B2D30] flex flex-col shrink-0 select-none overflow-hidden"
          >
            {/* Tool Window Header */}
            <div className="h-8 px-3 border-b border-[#2B2D30] flex items-center justify-between text-xs font-semibold tracking-wider text-[#7C808B]">
              <span className="uppercase text-[11px]">Project</span>
              <div className="flex items-center space-x-1">
                <button
                  onClick={() => setWelcomeModalOpen(true)}
                  className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded"
                  title="New File / Template"
                >
                  <Plus className="w-3.5 h-3.5" />
                </button>
                <button
                  onClick={() => setActiveToolStripe('none')}
                  className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded"
                  title="Hide Tool Window"
                >
                  <Minimize2 className="w-3.5 h-3.5" />
                </button>
              </div>
            </div>

            {/* Project Tree Content */}
            <div className="flex-1 overflow-y-auto py-2 text-xs">
              {/* Project Root Item */}
              <div
                onClick={() => setExpandedFolders((p) => ({ ...p, root: !p.root }))}
                className="flex items-center space-x-1.5 px-3 py-1 text-[#DFE1E5] font-medium hover:bg-[#2B2D30] cursor-pointer"
              >
                <ChevronDown className="w-3.5 h-3.5 text-[#7C808B]" />
                <FolderOpen className="w-4 h-4 text-[#61AFEF]" />
                <span>cgui</span>
                <span className="text-[10px] text-[#7C808B] ml-1">[main]</span>
              </div>

              {/* Tree Nodes */}
              <div className="ml-4 pl-1 border-l border-[#2B2D30]/40 mt-0.5 space-y-0.5">
                {INITIAL_FILES.map((item) => (
                  <div key={item.id}>
                    {item.type === 'directory' ? (
                      <div>
                        <div
                          onClick={() => openFile(item)}
                          className="flex items-center space-x-1.5 px-2 py-1 text-[#DFE1E5] hover:bg-[#2B2D30] rounded cursor-pointer"
                        >
                          {expandedFolders[item.id] ? (
                            <ChevronDown className="w-3.5 h-3.5 text-[#7C808B]" />
                          ) : (
                            <ChevronRight className="w-3.5 h-3.5 text-[#7C808B]" />
                          )}
                          {expandedFolders[item.id] ? (
                            <FolderOpen className="w-4 h-4 text-[#61AFEF]" />
                          ) : (
                            <Folder className="w-4 h-4 text-[#61AFEF]" />
                          )}
                          <span>{item.name}</span>
                        </div>

                        {/* Directory children */}
                        {expandedFolders[item.id] && item.children && (
                          <div className="ml-4 pl-1 border-l border-[#2B2D30]/40 space-y-0.5 mt-0.5">
                            {item.children.map((child) => (
                              <div
                                key={child.id}
                                onClick={() => openFile(child)}
                                className={`flex items-center space-x-2 px-2 py-1 rounded cursor-pointer transition ${
                                  activeTabId === child.id
                                    ? 'bg-[#2B2D30] text-[#DFE1E5] font-medium'
                                    : 'text-[#BCBEC4] hover:bg-[#2B2D30]/50 hover:text-[#DFE1E5]'
                                }`}
                              >
                                {getFileIcon(child.name)}
                                <span>{child.name}</span>
                              </div>
                            ))}
                          </div>
                        )}
                      </div>
                    ) : (
                      <div
                        onClick={() => openFile(item)}
                        className={`flex items-center space-x-2 px-2 py-1 rounded cursor-pointer transition ${
                          activeTabId === item.id
                            ? 'bg-[#2B2D30] text-[#DFE1E5] font-medium'
                            : 'text-[#BCBEC4] hover:bg-[#2B2D30]/50 hover:text-[#DFE1E5]'
                        }`}
                      >
                        {getFileIcon(item.name)}
                        <span>{item.name}</span>
                      </div>
                    )}
                  </div>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* WORKSPACE (Tabs + Editor Buffer + Bottom Dock) */}
        <main className="flex-1 flex flex-col bg-[#1E1F22] overflow-hidden min-w-0">
          {/* FLUSH EDITOR TAB BAR (JetBrains Style) */}
          <div className="h-9 bg-[#1E1F22] border-b border-[#2B2D30] flex items-center justify-between shrink-0 overflow-x-auto select-none">
            {/* Tabs List */}
            <div className="flex items-center h-full">
              {openTabs.map((tab) => {
                const isActive = tab.id === activeTabId;
                return (
                  <div
                    key={tab.id}
                    onClick={() => setActiveTabId(tab.id)}
                    className={`group relative h-full flex items-center space-x-2 px-3 border-r border-[#2B2D30] cursor-pointer text-xs transition ${
                      isActive
                        ? 'bg-[#1E1F22] text-[#DFE1E5] font-medium'
                        : 'bg-[#18191B] text-[#7C808B] hover:text-[#BCBEC4] hover:bg-[#1E1F22]/60'
                    }`}
                  >
                    {/* Active Tab Top Blue Accent Line (JetBrains New UI signature) */}
                    {isActive && (
                      <div className="absolute top-0 left-0 right-0 h-[2px] bg-[#3574F0]"></div>
                    )}

                    {getFileIcon(tab.name)}
                    <span className="truncate max-w-[140px]">{tab.name}</span>
                    {tab.dirty && <span className="text-[#3574F0] text-sm leading-none">•</span>}

                    {/* Close Tab Button */}
                    <button
                      onClick={(e) => closeTab(tab.id, e)}
                      className={`p-0.5 rounded hover:bg-[#393B40] text-[#7C808B] hover:text-[#DFE1E5] transition ${
                        isActive ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'
                      }`}
                    >
                      <X className="w-3 h-3" />
                    </button>
                  </div>
                );
              })}

              {/* Add New Tab Button */}
              <button
                onClick={() => {
                  const newId = `untitled_${Date.now()}`;
                  const newTab: OpenTab = {
                    id: newId,
                    name: 'Untitled.c',
                    path: 'Untitled.c',
                    dirty: false,
                    content: '// New buffer\n\n#include <stdio.h>\n\nint main(void) {\n    return 0;\n}\n',
                    language: 'c',
                  };
                  setOpenTabs([...openTabs, newTab]);
                  setActiveTabId(newId);
                }}
                className="px-2.5 h-full flex items-center justify-center text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30] transition"
                title="New Buffer"
              >
                <Plus className="w-3.5 h-3.5" />
              </button>
            </div>

            {/* Split & Tab Controls on Right */}
            <div className="flex items-center space-x-1 px-2 text-[#7C808B]">
              <button
                onClick={() => setWelcomeModalOpen(true)}
                className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded"
                title="Templates Hub"
              >
                <Sparkles className="w-3.5 h-3.5 text-[#61AFEF]" />
              </button>
              <button
                onClick={() => {
                  setTerminalHistory((p) => [...p, 'Split editor horizontally']);
                }}
                className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded"
                title="Split Right"
              >
                <SplitSquareVertical className="w-3.5 h-3.5" />
              </button>
            </div>
          </div>

          {/* EDITOR BUFFER (Gutter + Code Area with Syntax Highlighting) */}
          <div className="flex-1 flex overflow-hidden bg-[#1E1F22] text-[#DFE1E5] font-mono text-[13px] leading-5">
            {/* Gutter (Line Numbers & Diagnostic Markers) */}
            <div className="w-14 bg-[#1E1F22] border-r border-[#2B2D30]/60 text-right pr-3 select-none text-[#5A5D63] py-2 shrink-0">
              {activeTab.content.split('\n').map((_, index) => {
                const lineNum = index + 1;
                const isCurrent = lineNum === cursorPos.line;
                const hasWarning = activeTab.id === 'ui_layout_h' && lineNum === 25;
                return (
                  <div
                    key={lineNum}
                    className={`h-5 flex items-center justify-end space-x-1.5 ${
                      isCurrent ? 'text-[#DFE1E5] font-semibold' : ''
                    }`}
                  >
                    {hasWarning && <span className="w-1.5 h-1.5 rounded-full bg-[#F2C94C]"></span>}
                    <span>{lineNum}</span>
                  </div>
                );
              })}
            </div>

            {/* Code Content Area */}
            <div className="flex-1 overflow-auto p-2 pl-3 focus:outline-none">
              <pre className="m-0 font-mono text-[13px] leading-5 text-[#DFE1E5]">
                {activeTab.content.split('\n').map((line, idx) => {
                  const lineNum = idx + 1;
                  const isCurrent = lineNum === cursorPos.line;

                  // Highlighting rules (C / C23 JetBrains Palette)
                  let renderedLine: React.ReactNode = line;

                  if (line.trim().startsWith('//') || line.trim().startsWith('/*') || line.trim().startsWith('*')) {
                    renderedLine = <span className="text-[#7A7E85] italic">{line}</span>;
                  } else if (line.trim().startsWith('#include') || line.trim().startsWith('#define') || line.trim().startsWith('#ifndef') || line.trim().startsWith('#endif')) {
                    renderedLine = <span className="text-[#BBB529] font-medium">{line}</span>;
                  } else {
                    // Highlight keywords
                    const parts = line.split(/(\b(?:int|void|bool|true|false|return|if|else|struct|typedef|enum|const|sizeof|while|for)\b)/g);
                    renderedLine = parts.map((part, pIdx) => {
                      if (/^(int|void|bool|true|false|return|if|else|struct|typedef|enum|const|sizeof|while|for)$/.test(part)) {
                        return <span key={pIdx} className="text-[#CC7832] font-semibold">{part}</span>;
                      }
                      if (/".*?"/.test(part)) {
                        return <span key={pIdx} className="text-[#6A8759]">{part}</span>;
                      }
                      return part;
                    });
                  }

                  return (
                    <div
                      key={idx}
                      onClick={() => setCursorPos({ line: lineNum, col: 1 })}
                      className={`h-5 flex items-center px-1 rounded-sm ${
                        isCurrent ? 'bg-[#26282E]' : 'hover:bg-[#26282E]/40'
                      }`}
                    >
                      {renderedLine}
                    </div>
                  );
                })}
              </pre>
            </div>
          </div>

          {/* =========================================================================
           * 3. BOTTOM DOCKABLE TOOL WINDOW (Terminal & Problems)
           * ========================================================================= */}
          {dockOpen && (
            <div
              style={{ height: `${dockHeight}px` }}
              className="bg-[#1E1F22] border-t border-[#2B2D30] flex flex-col shrink-0 select-none overflow-hidden"
            >
              {/* Dock Tab Header Strip */}
              <div className="h-8 bg-[#18191B] border-b border-[#2B2D30] flex items-center justify-between px-2 shrink-0">
                <div className="flex items-center space-x-1 h-full">
                  {/* Terminal Tab */}
                  <button
                    onClick={() => setDockTab('terminal')}
                    className={`flex items-center space-x-1.5 px-3 h-full text-xs font-medium border-b-2 transition ${
                      dockTab === 'terminal'
                        ? 'border-[#3574F0] text-[#DFE1E5] bg-[#1E1F22]'
                        : 'border-transparent text-[#7C808B] hover:text-[#BCBEC4]'
                    }`}
                  >
                    <Terminal className="w-3.5 h-3.5" />
                    <span>Terminal</span>
                  </button>

                  {/* Problems Tab */}
                  <button
                    onClick={() => setDockTab('problems')}
                    className={`flex items-center space-x-1.5 px-3 h-full text-xs font-medium border-b-2 transition ${
                      dockTab === 'problems'
                        ? 'border-[#3574F0] text-[#DFE1E5] bg-[#1E1F22]'
                        : 'border-transparent text-[#7C808B] hover:text-[#BCBEC4]'
                    }`}
                  >
                    <AlertTriangle className="w-3.5 h-3.5 text-[#F2C94C]" />
                    <span>Problems</span>
                    <span className="px-1.5 py-0.2 rounded-full text-[10px] bg-[#393B40] text-[#BCBEC4]">1</span>
                  </button>

                  {/* Build Output Tab */}
                  <button
                    onClick={() => setDockTab('build')}
                    className={`flex items-center space-x-1.5 px-3 h-full text-xs font-medium border-b-2 transition ${
                      dockTab === 'build'
                        ? 'border-[#3574F0] text-[#DFE1E5] bg-[#1E1F22]'
                        : 'border-transparent text-[#7C808B] hover:text-[#BCBEC4]'
                    }`}
                  >
                    <Hammer className="w-3.5 h-3.5 text-[#61AFEF]" />
                    <span>Build</span>
                  </button>
                </div>

                {/* Dock Actions on Right */}
                <div className="flex items-center space-x-1 text-[#7C808B]">
                  <button
                    onClick={() => setTerminalHistory([])}
                    className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded transition"
                    title="Clear Terminal"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                  </button>
                  <button
                    onClick={handleRunBuild}
                    className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded transition"
                    title="Rerun Target"
                  >
                    <RotateCcw className="w-3.5 h-3.5" />
                  </button>
                  <button
                    onClick={() => setDockOpen(false)}
                    className="p-1 hover:text-[#DFE1E5] hover:bg-[#2B2D30] rounded transition"
                    title="Close Dock"
                  >
                    <X className="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>

              {/* Dock Body Content */}
              <div className="flex-1 overflow-auto p-2 bg-[#1E1F22]">
                {dockTab === 'terminal' ? (
                  <div className="h-full flex flex-col font-mono text-xs">
                    <div className="flex-1 overflow-y-auto space-y-1">
                      {terminalHistory.map((line, idx) => (
                        <div key={idx} className="whitespace-pre-wrap leading-relaxed text-[#BCBEC4]">
                          {line}
                        </div>
                      ))}
                      <div ref={terminalEndRef} />
                    </div>

                    {/* Terminal Input Prompt */}
                    <form onSubmit={handleTerminalSubmit} className="flex items-center space-x-2 pt-2 border-t border-[#2B2D30]/60">
                      <span className="text-[#59A869] font-bold">$</span>
                      <input
                        type="text"
                        value={terminalInput}
                        onChange={(e) => setTerminalInput(e.target.value)}
                        placeholder="Type a command (build, run, clay-status, git status, help)..."
                        className="flex-1 bg-transparent border-none text-[#DFE1E5] focus:outline-none font-mono text-xs"
                      />
                    </form>
                  </div>
                ) : dockTab === 'problems' ? (
                  <div className="text-xs space-y-2">
                    <div className="text-[11px] font-semibold text-[#7C808B] uppercase tracking-wider mb-2">
                      src/ui_layout.c (1 issue found)
                    </div>
                    {diagnostics.map((diag, i) => (
                      <div
                        key={i}
                        onClick={() => {
                          const tab = openTabs.find((t) => t.path === diag.file);
                          if (tab) setActiveTabId(tab.id);
                          setCursorPos({ line: diag.line, col: diag.col });
                        }}
                        className="flex items-start space-x-2.5 p-2 rounded bg-[#2B2D30]/40 hover:bg-[#2B2D30] cursor-pointer transition border border-transparent hover:border-[#393B40]"
                      >
                        <AlertTriangle className="w-4 h-4 text-[#F2C94C] shrink-0 mt-0.5" />
                        <div className="flex-1">
                          <div className="text-[#DFE1E5] font-medium">{diag.message}</div>
                          <div className="text-[#7C808B] text-[11px] mt-0.5">
                            {diag.file}:{diag.line}:{diag.col} • {diag.code}
                          </div>
                        </div>
                      </div>
                    ))}
                  </div>
                ) : (
                  <div className="font-mono text-xs text-[#BCBEC4] space-y-1">
                    <div>[Meson 1.3.2] Found ninja-1.11.1 at /usr/bin/ninja</div>
                    <div>Compiler: GCC 14.2.0 (C23 standard supported)</div>
                    <div>Libraries: SDL3 3.1.8, Vulkan 1.3.283, HarfBuzz 8.3, FreeType 2.13</div>
                    <div className="text-[#59A869]">Target: build/cgui up-to-date.</div>
                  </div>
                )}
              </div>
            </div>
          )}
        </main>
      </div>

      {/* =========================================================================
       * 4. COMPACT STATUS BAR (JetBrains Bottom Bar)
       * ========================================================================= */}
      <footer className="h-6 bg-[#1E1F22] border-t border-[#2B2D30] flex items-center justify-between px-3 text-[11px] text-[#7C808B] shrink-0 z-20">
        {/* Left Status info */}
        <div className="flex items-center space-x-3">
          <div className="flex items-center space-x-1.5 text-[#DFE1E5]">
            <GitBranch className="w-3 h-3 text-[#59A869]" />
            <span>main</span>
          </div>

          <span className="text-[#393B40]">|</span>

          <div className="flex items-center space-x-1 text-[#8C92A4]">
            <CheckCircle2 className="w-3 h-3 text-[#59A869]" />
            <span>Clay Flexbox Layout: Active</span>
          </div>

          <span className="text-[#393B40]">|</span>

          <span className="text-[#BCBEC4] truncate max-w-[280px]">
            {isBuilding ? '⚙ Building cgui...' : '✓ ' + buildStatus}
          </span>
        </div>

        {/* Right Encoding, Cursor & Toolchain info */}
        <div className="flex items-center space-x-3">
          <span>UTF-8</span>
          <span className="text-[#393B40]">|</span>
          <span>4 spaces</span>
          <span className="text-[#393B40]">|</span>
          <span className="text-[#DFE1E5]">
            Ln {cursorPos.line}, Col {cursorPos.col}
          </span>
          <span className="text-[#393B40]">|</span>
          <div className="flex items-center space-x-1 text-[#59A869]">
            <span className="w-1.5 h-1.5 rounded-full bg-[#59A869]"></span>
            <span>C23 / clangd</span>
          </div>
        </div>
      </footer>

      {/* =========================================================================
       * 5. JETBRAINS NEW PROJECT WIZARD MODAL
       * ========================================================================= */}
      {welcomeModalOpen && (
        <div className="fixed inset-0 bg-black/60 backdrop-blur-[2px] flex items-center justify-center p-4 z-50 animate-in fade-in duration-150">
          <div className="bg-[#1E1F22] border border-[#393B40] rounded-xl shadow-2xl w-full max-w-4xl h-[560px] flex flex-col overflow-hidden">
            {/* Modal Header */}
            <div className="h-12 px-6 border-b border-[#2B2D30] flex items-center justify-between shrink-0">
              <div className="flex items-center space-x-2">
                <span className="w-3 h-3 rounded bg-[#3574F0]"></span>
                <h2 className="text-sm font-semibold text-[#DFE1E5]">New Project Generator</h2>
              </div>
              <button
                onClick={() => setWelcomeModalOpen(false)}
                className="p-1 rounded text-[#7C808B] hover:text-[#DFE1E5] hover:bg-[#2B2D30]"
              >
                <X className="w-4 h-4" />
              </button>
            </div>

            {/* Modal Body: Left Project Types, Right Project Settings */}
            <div className="flex-1 flex overflow-hidden">
              {/* Left Template List */}
              <div className="w-72 bg-[#18191B] border-r border-[#2B2D30] p-3 space-y-1.5 overflow-y-auto">
                <div className="text-[10px] font-bold uppercase text-[#7C808B] px-2 mb-2 tracking-wider">
                  Project Templates
                </div>
                {TEMPLATES.map((tmpl) => {
                  const isSel = selectedTemplate.id === tmpl.id;
                  return (
                    <div
                      key={tmpl.id}
                      onClick={() => setSelectedTemplate(tmpl)}
                      className={`flex items-center justify-between p-2 rounded-lg cursor-pointer transition ${
                        isSel
                          ? 'bg-[#3574F0] text-white shadow-sm'
                          : 'text-[#BCBEC4] hover:bg-[#2B2D30] hover:text-[#DFE1E5]'
                      }`}
                    >
                      <div className="flex items-center space-x-2.5">
                        <span className={`text-[10px] font-bold px-1.5 py-0.5 rounded ${isSel ? 'bg-white/20 text-white' : 'bg-[#2B2D30] text-[#61AFEF]'}`}>
                          {tmpl.tag}
                        </span>
                        <span className="text-xs font-medium truncate max-w-[150px]">{tmpl.title}</span>
                      </div>
                    </div>
                  );
                })}
              </div>

              {/* Right Settings Form */}
              <div className="flex-1 p-6 flex flex-col justify-between overflow-y-auto">
                <div className="space-y-5">
                  <div>
                    <h3 className="text-base font-semibold text-[#DFE1E5]">{selectedTemplate.title}</h3>
                    <p className="text-xs text-[#7C808B] mt-1">{selectedTemplate.desc}</p>
                  </div>

                  <div className="space-y-4 pt-2">
                    {/* Project Name */}
                    <div>
                      <label className="block text-xs font-medium text-[#DFE1E5] mb-1.5">Project Name</label>
                      <input
                        type="text"
                        value={newProjectName}
                        onChange={(e) => setNewProjectName(e.target.value)}
                        className="w-full bg-[#2B2D30] border border-[#393B40] rounded-md px-3 py-2 text-xs text-[#DFE1E5] focus:outline-none focus:border-[#3574F0]"
                      />
                    </div>

                    {/* Location */}
                    <div>
                      <label className="block text-xs font-medium text-[#DFE1E5] mb-1.5">Location</label>
                      <div className="flex space-x-2">
                        <input
                          type="text"
                          value={newProjectLocation}
                          onChange={(e) => setNewProjectLocation(e.target.value)}
                          className="flex-1 bg-[#2B2D30] border border-[#393B40] rounded-md px-3 py-2 text-xs text-[#DFE1E5] focus:outline-none focus:border-[#3574F0]"
                        />
                        <button className="px-3 py-2 bg-[#2B2D30] border border-[#393B40] hover:bg-[#393B40] text-xs text-[#DFE1E5] rounded-md transition">
                          Browse…
                        </button>
                      </div>
                    </div>

                    {/* Specifications */}
                    <div className="grid grid-cols-2 gap-3 pt-2">
                      <div className="p-3 bg-[#2B2D30]/50 rounded-lg border border-[#2B2D30]">
                        <span className="text-[10px] text-[#7C808B] uppercase font-semibold">Build Toolchain</span>
                        <div className="text-xs font-medium text-[#DFE1E5] mt-1">{selectedTemplate.toolchain}</div>
                      </div>
                      <div className="p-3 bg-[#2B2D30]/50 rounded-lg border border-[#2B2D30]">
                        <span className="text-[10px] text-[#7C808B] uppercase font-semibold">Entrypoint</span>
                        <div className="text-xs font-medium text-[#DFE1E5] mt-1">{selectedTemplate.entry}</div>
                      </div>
                    </div>

                    <div className="flex items-center space-x-2 pt-2">
                      <input type="checkbox" id="git_init" defaultChecked className="rounded accent-[#3574F0]" />
                      <label htmlFor="git_init" className="text-xs text-[#DFE1E5]">
                        Initialize Git repository
                      </label>
                    </div>
                  </div>
                </div>

                {/* Footer Buttons */}
                <div className="flex items-center justify-end space-x-3 pt-4 border-t border-[#2B2D30]">
                  <button
                    onClick={() => setWelcomeModalOpen(false)}
                    className="px-4 py-2 text-xs text-[#DFE1E5] hover:bg-[#2B2D30] rounded-md transition"
                  >
                    Cancel
                  </button>
                  <button
                    onClick={handleCreateProject}
                    className="px-5 py-2 text-xs font-medium text-white bg-[#3574F0] hover:bg-[#3068D9] rounded-md shadow-sm transition"
                  >
                    Create Project
                  </button>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* =========================================================================
       * 6. SEARCH EVERYWHERE MODAL (Double Shift)
       * ========================================================================= */}
      {searchModalOpen && (
        <div
          onClick={() => setSearchModalOpen(false)}
          className="fixed inset-0 bg-black/50 backdrop-blur-[2px] flex items-start justify-center pt-20 p-4 z-50"
        >
          <div
            onClick={(e) => e.stopPropagation()}
            className="bg-[#2B2D30] border border-[#393B40] rounded-xl shadow-2xl w-full max-w-xl overflow-hidden"
          >
            <div className="p-3 border-b border-[#393B40] flex items-center space-x-2.5">
              <Search className="w-4 h-4 text-[#7C808B]" />
              <input
                type="text"
                autoFocus
                placeholder="Search files, actions, symbols, settings..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                className="flex-1 bg-transparent border-none text-xs text-[#DFE1E5] focus:outline-none"
              />
              <kbd className="px-1.5 py-0.5 text-[10px] bg-[#1E1F22] border border-[#393B40] rounded text-[#7C808B]">ESC</kbd>
            </div>
            <div className="p-2 max-h-72 overflow-y-auto space-y-1 text-xs">
              <div className="text-[10px] uppercase font-bold text-[#7C808B] px-2 py-1">Recent Files & Actions</div>
              {openTabs.map((tab) => (
                <div
                  key={tab.id}
                  onClick={() => {
                    setActiveTabId(tab.id);
                    setSearchModalOpen(false);
                  }}
                  className="flex items-center justify-between p-2 rounded hover:bg-[#3574F0] hover:text-white cursor-pointer transition text-[#DFE1E5]"
                >
                  <div className="flex items-center space-x-2">
                    {getFileIcon(tab.name)}
                    <span>{tab.name}</span>
                  </div>
                  <span className="text-[10px] opacity-60">{tab.path}</span>
                </div>
              ))}
              <div
                onClick={() => {
                  setWelcomeModalOpen(true);
                  setSearchModalOpen(false);
                }}
                className="flex items-center space-x-2 p-2 rounded hover:bg-[#3574F0] hover:text-white cursor-pointer transition text-[#DFE1E5]"
              >
                <Plus className="w-4 h-4 text-[#59A869]" />
                <span>Action: Create New Project...</span>
              </div>
              <div
                onClick={() => {
                  handleRunBuild();
                  setSearchModalOpen(false);
                }}
                className="flex items-center space-x-2 p-2 rounded hover:bg-[#3574F0] hover:text-white cursor-pointer transition text-[#DFE1E5]"
              >
                <Hammer className="w-4 h-4 text-[#61AFEF]" />
                <span>Action: Meson Build (Ninja)</span>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
