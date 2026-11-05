project(
  'cgui',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'warning_level=2',
    'werror=false',
    'buildtype=debug',
  ],
)

cc = meson.get_compiler('c')

sdl3_dep       = dependency('sdl3', required: true, fallback: ['sdl3', 'sdl3_dep'])
vulkan_dep     = dependency('vulkan', required: true)
m_dep          = cc.find_library('m', required: true)
fontconfig_dep = dependency('fontconfig', required: false)
freetype_dep   = dependency('freetype2', required: true)
harfbuzz_dep   = dependency('harfbuzz', required: true)
librsvg_dep    = dependency('librsvg-2.0', required: true)

# Tree-sitter via Meson wraps (subprojects/*.wrap) — not vendored in third_party/
tree_sitter_dep   = dependency('tree-sitter', required: true,
                               fallback: ['tree-sitter', 'tree_sitter_dep'])
tree_sitter_c_dep = dependency('tree-sitter-c', required: true,
                               fallback: ['tree-sitter-c', 'tree_sitter_c_dep'])

# Lua 5.4 for configuration, user scripts, and NvChad themes
lua_dep = dependency('lua54', 'lua-5.4', 'lua5.4', 'lua', required: false)
if not lua_dep.found()
  lua_sub = subproject('lua')
  lua_dep = lua_sub.get_variable('lua_dep')
endif

# Default themes and queries paths
themes_dir = meson.current_source_dir() / 'assets' / 'themes'
add_project_arguments('-DCGUI_DEFAULT_THEMES_PATH="@0@"'.format(themes_dir), language: 'c')
queries_dir = meson.current_source_dir() / 'assets' / 'queries'
add_project_arguments('-DCGUI_DEFAULT_QUERIES_PATH="@0@"'.format(queries_dir), language: 'c')

glslang = find_program('glslangValidator', 'glslang', required: true)

# Meson does not expose c23 as a built-in standard option yet.
if not cc.has_argument('-std=c23')
  error('A C compiler with C23 support is required')
endif
add_project_arguments('-std=c23', '-D_POSIX_C_SOURCE=200809L', '-D_DEFAULT_SOURCE', language: 'c')

shaders = [
  ['rounded_rect.vert', 'vert'],
  ['rounded_rect.frag', 'frag'],
  ['blur.vert', 'vert'],
  ['blur.frag', 'frag'],
  ['blit.vert', 'vert'],
  ['blit.frag', 'frag'],
  ['text.vert', 'vert'],
  ['text.frag', 'frag'],
  ['icon.vert', 'vert'],
  ['icon.frag', 'frag'],
]

spv_targets = []
foreach s : shaders
  name = s[0]
  stage = s[1]
  spv_targets += custom_target(
    name.underscorify() + '_spv',
    input:  'src/shaders' / name,
    output: name + '.spv',
    command: [glslang, '-V', '-S', stage, '@INPUT@', '-o', '@OUTPUT@'],
  )
endforeach

sources = files(
  'src/main.c',
  'src/tab_bar.c',
  'src/sidebar.c',
  'src/find_bar.c',
  'src/palette.c',
  'src/gui.c',
  'src/style.c',
  'src/panel.c',
  'src/button.c',
  'src/vk_context.c',
  'src/vk_draw.c',
  'src/spv_loader.c',
  'src/font.c',
  'src/fonts.c',
  'src/editor.c',
  'src/highlight.c',
  'src/icon.c',
  'src/theme.c',
  'src/lua_bridge.c',
  'src/lsp.c',
)

executable(
  'cgui',
  sources,
  spv_targets,
  include_directories: include_directories('src'),
  dependencies: [
    sdl3_dep,
    vulkan_dep,
    m_dep,
    fontconfig_dep,
    freetype_dep,
    harfbuzz_dep,
    librsvg_dep,
    tree_sitter_dep,
    tree_sitter_c_dep,
    lua_dep,
  ],
  install: false,
)
