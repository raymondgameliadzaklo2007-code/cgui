project('lua', 'c', version: '5.4.7', meson_version: '>=0.60.0')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)
dl_dep = cc.find_library('dl', required: false)

lua_sources = files(
  'src/lapi.c',
  'src/lcode.c',
  'src/lctype.c',
  'src/ldebug.c',
  'src/ldo.c',
  'src/ldump.c',
  'src/lfunc.c',
  'src/lgc.c',
  'src/llex.c',
  'src/lmem.c',
  'src/lobject.c',
  'src/lopcodes.c',
  'src/lparser.c',
  'src/lstate.c',
  'src/lstring.c',
  'src/ltable.c',
  'src/ltm.c',
  'src/lundump.c',
  'src/lvm.c',
  'src/lzio.c',
  'src/lauxlib.c',
  'src/lbaselib.c',
  'src/lcorolib.c',
  'src/ldblib.c',
  'src/liolib.c',
  'src/lmathlib.c',
  'src/loadlib.c',
  'src/loslib.c',
  'src/lstrlib.c',
  'src/ltablib.c',
  'src/lutf8lib.c',
  'src/linit.c',
)

inc = include_directories('src')

lib = static_library(
  'lua',
  lua_sources,
  include_directories: inc,
  c_args: ['-DLUA_COMPAT_5_3', '-DLUA_USE_POSIX', '-DLUA_USE_DLOPEN', '-Wno-unused-parameter'],
  dependencies: [m_dep, dl_dep],
)

lua_dep = declare_dependency(
  include_directories: inc,
  link_with: lib,
  dependencies: [m_dep, dl_dep],
)

meson.override_dependency('lua', lua_dep)
meson.override_dependency('lua5.4', lua_dep)
meson.override_dependency('lua-5.4', lua_dep)
meson.override_dependency('lua54', lua_dep)
