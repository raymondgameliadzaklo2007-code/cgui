project('tree-sitter-c', 'c', version: '0.23.0', meson_version: '>=0.60.0')

ts_dep = dependency('tree-sitter')

lib = static_library(
  'tree-sitter-c',
  'src/parser.c',
  include_directories: include_directories('src'),
  c_args: ['-std=c11', '-Wno-unused-parameter'],
)

tree_sitter_c_dep = declare_dependency(
  include_directories: include_directories('src'),
  link_with: lib,
  dependencies: [ts_dep],
)

meson.override_dependency('tree-sitter-c', tree_sitter_c_dep)
