project('tree-sitter', 'c', version: '0.25.3', meson_version: '>=0.60.0')

inc = include_directories('lib/include', 'lib/src')

lib = static_library(
  'tree-sitter',
  'lib/src/lib.c',
  include_directories: inc,
  c_args: ['-std=c11', '-D_GNU_SOURCE', '-Wno-unused-parameter'],
)

tree_sitter_dep = declare_dependency(
  include_directories: include_directories('lib/include'),
  link_with: lib,
)

meson.override_dependency('tree-sitter', tree_sitter_dep)
