gworldscene_api_version = '0.1'

gworldscene_deps = [
  dependency('gtk4', version: '>=4.10'),
  dependency('gdk-pixbuf-2.0'),
  dependency('gio-2.0'),
  dependency('gobject-2.0'),
  dependency('epoxy'),
  dependency('gdal'),
  dependency('assimp'),
  dependency('libsoup-3.0'),
  dependency('glm'),
  dependency('zlib'),
]

gworldscene_headers = [
  'gworldscene.h',
  'gworld-scene-node.h',
  'gworld-scene-view.h',
]

gworldscene_sources = [
  'gworld-scene-camera.cpp',
  'gworld-scene-camera-private.h',
  'gworld-scene-geo.cpp',
  'gworld-scene-geo-private.h',
  'gworld-scene-light.cpp',
  'gworld-scene-light-private.h',
  'gworld-scene-lod.cpp',
  'gworld-scene-lod-private.h',
  'gworld-scene-node.cpp',
  'gworld-scene-node-private.h',
  'gworld-scene-picking.cpp',
  'gworld-scene-picking-private.h',
  'gworld-scene-view.cpp',
  'gworld-scene-view-private.h',
  'gworld-scene-view-shaders.cpp',
  'gworld-scene-view-shaders-private.h',
]

gworldscene_inc = include_directories('.')

gworldscene_lib = shared_library(
  'gworldscene-' + gworldscene_api_version,
  gworldscene_sources,
  dependencies: gworldscene_deps,
  include_directories: gworldscene_inc,
  install: true,
  version: meson.project_version(),
  soversion: '0',
)

install_headers(
  gworldscene_headers,
  subdir: 'gworldscene-' + gworldscene_api_version + '/gworldscene',
)

gworldscene_dep = declare_dependency(
  link_with: gworldscene_lib,
  include_directories: gworldscene_inc,
  dependencies: gworldscene_deps,
)

gworldscene_gir = gnome.generate_gir(
  gworldscene_lib,
  sources: gworldscene_headers,
  namespace: 'GWorldScene',
  nsversion: gworldscene_api_version,
  symbol_prefix: 'gworld',
  identifier_prefix: 'GWorld',
  includes: ['Gtk-4.0', 'GObject-2.0'],
  export_packages: 'gworldscene-' + gworldscene_api_version,
  header: 'gworldscene/gworldscene.h',
  extra_args: [
    '--c-include=gworldscene/gworldscene.h',
    '--cflags-begin',
    '-I' + meson.current_source_dir() / 'gir-compat',
    '--cflags-end',
  ],
  install: true,
)

gworldscene_vapi = gnome.generate_vapi(
  'GWorldScene-' + gworldscene_api_version,
  sources: gworldscene_gir[0],
  metadata_dirs: [meson.current_source_dir()],
  packages: ['gtk4'],
  install: true,
)

gworldscene_vapi_dep = declare_dependency(
  dependencies: gworldscene_vapi,
)

pkg.generate(
  gworldscene_lib,
  name: 'GWorldScene',
  filebase: 'gworldscene-' + gworldscene_api_version,
  description: 'GTK4 OpenGL world scene widget',
  subdirs: 'gworldscene-' + gworldscene_api_version,
  requires: ['gtk4', 'gdk-pixbuf-2.0', 'gio-2.0', 'epoxy', 'gdal', 'assimp', 'libsoup-3.0', 'zlib'],
  version: meson.project_version(),
)
