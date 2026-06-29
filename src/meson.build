gworldscene_api_version = '0.1'

gobject_introspection_dep = dependency('gobject-introspection-1.0')
gobject_introspection_girdir = gobject_introspection_dep.get_variable(pkgconfig: 'girdir')
gobject_introspection_typelibdir = gobject_introspection_dep.get_variable(pkgconfig: 'typelibdir')
gobject_introspection_libdir = gobject_introspection_dep.get_variable(pkgconfig: 'libdir')
gobject_introspection_includedir = gobject_introspection_dep.get_variable(pkgconfig: 'includedir')
gobject_introspection_datadir = gobject_introspection_dep.get_variable(pkgconfig: 'datadir')
gworldscene_pkgconfigdir = gobject_introspection_libdir / 'pkgconfig'
gworldscene_vapidir = gobject_introspection_datadir / 'vala' / 'vapi'
gworldscene_core_headerdir = gobject_introspection_includedir / ('gworldscene-core-' + gworldscene_api_version) / 'gworldscene'
gworldscene_gtk3_headerdir = gobject_introspection_includedir / ('gworldscene-gtk3-' + gworldscene_api_version) / 'gworldscene'
gworldscene_gtk4_headerdir = gobject_introspection_includedir / ('gworldscene-gtk4-' + gworldscene_api_version) / 'gworldscene'

gtk3_dep = dependency('gtk+-3.0', version: '>=3.24', required: get_option('gtk3'))
gtk4_dep = dependency('gtk4', version: '>=4.10', required: get_option('gtk4'))

if not gtk3_dep.found() and not gtk4_dep.found()
  error('Neither gtk+-3.0 nor gtk4 development files were found; enable at least one GTK variant.')
endif

gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
gio_dep = dependency('gio-2.0')
gobject_dep = dependency('gobject-2.0')
epoxy_dep = dependency('epoxy')
gdal_dep = dependency('gdal')
assimp_dep = dependency('assimp')
libsoup_dep = dependency('libsoup-3.0')
glm_dep = dependency('glm')
zlib_dep = dependency('zlib')

gworldscene_core_deps = [
  gobject_dep,
  glm_dep,
]

gworldscene_inc = include_directories('.')

gworldscene_core_headers = [
  'gworld-scene-node.h',
]

gworldscene_headers = [
  'gworldscene.h',
  'gworld-scene-node.h',
  'gworld-scene-view.h',
]

gworldscene_core_sources = [
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
]

gworldscene_view_sources = [
  'gworld-scene-view.cpp',
  'gworld-scene-view-private.h',
  'gworld-scene-view-shaders.cpp',
  'gworld-scene-view-shaders-private.h',
]

gworldscene_core_lib = shared_library(
  'gworldscene-core-' + gworldscene_api_version,
  gworldscene_core_sources,
  dependencies: gworldscene_core_deps,
  include_directories: gworldscene_inc,
  install_dir: gobject_introspection_libdir,
  install: true,
  version: meson.project_version(),
  soversion: '0',
)

install_headers(
  gworldscene_core_headers,
  install_dir: gworldscene_core_headerdir,
)

gworldscene_core_dep = declare_dependency(
  link_with: gworldscene_core_lib,
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_deps,
)

pkg.generate(
  gworldscene_core_lib,
  name: 'GWorldScene Core',
  filebase: 'gworldscene-core-' + gworldscene_api_version,
  description: 'Toolkit-independent GWorldScene scene nodes and math',
  subdirs: [],
  extra_cflags: ['-I' + (gobject_introspection_includedir / ('gworldscene-core-' + gworldscene_api_version))],
  install_dir: gworldscene_pkgconfigdir,
  requires: ['gobject-2.0'],
  version: meson.project_version(),
)

gworldscene_gtk3_built = false
gworldscene_gtk4_built = false

if gtk3_dep.found()
  gworldscene_gtk3_built = true
  gworldscene_gtk3_lib = shared_library(
    'gworldscene-gtk3-' + gworldscene_api_version,
    gworldscene_view_sources,
    dependencies: [
      gworldscene_core_dep,
      gdk_pixbuf_dep,
      gio_dep,
      gtk3_dep,
      epoxy_dep,
      gdal_dep,
      assimp_dep,
      libsoup_dep,
      zlib_dep,
    ],
    include_directories: gworldscene_inc,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=3'],
    install_dir: gobject_introspection_libdir,
    install: true,
    version: meson.project_version(),
    soversion: '0',
  )

  install_headers(
    gworldscene_headers,
    install_dir: gworldscene_gtk3_headerdir,
  )

  gworldscene_gtk3_dep = declare_dependency(
    link_with: gworldscene_gtk3_lib,
    include_directories: gworldscene_inc,
    dependencies: [
      gworldscene_core_dep,
      gdk_pixbuf_dep,
      gio_dep,
      gtk3_dep,
      epoxy_dep,
      gdal_dep,
      assimp_dep,
      libsoup_dep,
      zlib_dep,
    ],
  )

  gworldscene_gtk3_gir = gnome.generate_gir(
    gworldscene_gtk3_lib,
    sources: gworldscene_headers,
    namespace: 'GWorldSceneGtk3',
    nsversion: gworldscene_api_version,
    symbol_prefix: 'gworld',
    identifier_prefix: 'GWorld',
    includes: ['Gtk-3.0', 'GObject-2.0'],
    export_packages: 'gworldscene-gtk3-' + gworldscene_api_version,
    header: 'gworldscene/gworldscene.h',
    extra_args: [
      '--c-include=gworldscene/gworldscene.h',
      '--extra-library=gworldscene-core-' + gworldscene_api_version,
      '--cflags-begin',
      '-I' + meson.current_source_dir() / 'gir-compat',
      '--cflags-end',
    ],
    install_dir_gir: gobject_introspection_girdir,
    install_dir_typelib: gobject_introspection_typelibdir,
    install: true,
  )

  gworldscene_gtk3_vapi = gnome.generate_vapi(
    'GWorldSceneGtk3-' + gworldscene_api_version,
    sources: gworldscene_gtk3_gir[0],
    metadata_dirs: [meson.current_source_dir()],
    packages: ['gtk+-3.0'],
    install_dir: gworldscene_vapidir,
    install: true,
  )

  gworldscene_gtk3_vapi_dep = declare_dependency(
    dependencies: gworldscene_gtk3_vapi,
  )

  pkg.generate(
    gworldscene_gtk3_lib,
    name: 'GWorldScene GTK3',
    filebase: 'gworldscene-gtk3-' + gworldscene_api_version,
    description: 'GTK3 OpenGL world scene widget',
    subdirs: [],
    extra_cflags: ['-I' + (gobject_introspection_includedir / ('gworldscene-gtk3-' + gworldscene_api_version))],
    install_dir: gworldscene_pkgconfigdir,
    requires: [
      'gworldscene-core-' + gworldscene_api_version,
      'gdk-pixbuf-2.0',
      'gio-2.0',
      'gtk+-3.0',
      'epoxy',
      'gdal',
      'assimp',
      'libsoup-3.0',
      'zlib',
    ],
    version: meson.project_version(),
  )
endif

if gtk4_dep.found()
  gworldscene_gtk4_built = true
  gworldscene_gtk4_lib = shared_library(
    'gworldscene-gtk4-' + gworldscene_api_version,
    gworldscene_view_sources,
    dependencies: [
      gworldscene_core_dep,
      gdk_pixbuf_dep,
      gio_dep,
      gtk4_dep,
      epoxy_dep,
      gdal_dep,
      assimp_dep,
      libsoup_dep,
      zlib_dep,
    ],
    include_directories: gworldscene_inc,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=4'],
    install_dir: gobject_introspection_libdir,
    install: true,
    version: meson.project_version(),
    soversion: '0',
  )

  install_headers(
    gworldscene_headers,
    install_dir: gworldscene_gtk4_headerdir,
  )

  gworldscene_gtk4_dep = declare_dependency(
    link_with: gworldscene_gtk4_lib,
    include_directories: gworldscene_inc,
    dependencies: [
      gworldscene_core_dep,
      gdk_pixbuf_dep,
      gio_dep,
      gtk4_dep,
      epoxy_dep,
      gdal_dep,
      assimp_dep,
      libsoup_dep,
      zlib_dep,
    ],
  )

  gworldscene_gtk4_gir = gnome.generate_gir(
    gworldscene_gtk4_lib,
    sources: gworldscene_headers,
    namespace: 'GWorldSceneGtk4',
    nsversion: gworldscene_api_version,
    symbol_prefix: 'gworld',
    identifier_prefix: 'GWorld',
    includes: ['Gtk-4.0', 'GObject-2.0'],
    export_packages: 'gworldscene-gtk4-' + gworldscene_api_version,
    header: 'gworldscene/gworldscene.h',
    extra_args: [
      '--c-include=gworldscene/gworldscene.h',
      '--extra-library=gworldscene-core-' + gworldscene_api_version,
      '--cflags-begin',
      '-I' + meson.current_source_dir() / 'gir-compat',
      '--cflags-end',
    ],
    install_dir_gir: gobject_introspection_girdir,
    install_dir_typelib: gobject_introspection_typelibdir,
    install: true,
  )

  gworldscene_gtk4_vapi = gnome.generate_vapi(
    'GWorldSceneGtk4-' + gworldscene_api_version,
    sources: gworldscene_gtk4_gir[0],
    metadata_dirs: [meson.current_source_dir()],
    packages: ['gtk4'],
    install_dir: gworldscene_vapidir,
    install: true,
  )

  gworldscene_gtk4_vapi_dep = declare_dependency(
    dependencies: gworldscene_gtk4_vapi,
  )

  pkg.generate(
    gworldscene_gtk4_lib,
    name: 'GWorldScene GTK4',
    filebase: 'gworldscene-gtk4-' + gworldscene_api_version,
    description: 'GTK4 OpenGL world scene widget',
    subdirs: [],
    extra_cflags: ['-I' + (gobject_introspection_includedir / ('gworldscene-gtk4-' + gworldscene_api_version))],
    install_dir: gworldscene_pkgconfigdir,
    requires: [
      'gworldscene-core-' + gworldscene_api_version,
      'gdk-pixbuf-2.0',
      'gio-2.0',
      'gtk4',
      'epoxy',
      'gdal',
      'assimp',
      'libsoup-3.0',
      'zlib',
    ],
    version: meson.project_version(),
  )
endif

if gworldscene_gtk4_built
  gworldscene_lib = gworldscene_gtk4_lib
  gworldscene_dep = gworldscene_gtk4_dep
  gworldscene_vapi_dep = gworldscene_gtk4_vapi_dep
elif gworldscene_gtk3_built
  gworldscene_lib = gworldscene_gtk3_lib
  gworldscene_dep = gworldscene_gtk3_dep
  gworldscene_vapi_dep = gworldscene_gtk3_vapi_dep
endif
