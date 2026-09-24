terrain_normal_test = executable(
  'gworldscene-terrain-normal-test', 'terrain-normal-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)
test('terrain-normals', terrain_normal_test)

geo_test = executable(
  'gworldscene-geo-test',
  'geo-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)

node_test = executable(
  'gworldscene-node-test',
  'node-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)

lod_test = executable(
  'gworldscene-lod-test',
  'lod-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)

camera_test = executable(
  'gworldscene-camera-test',
  'camera-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)

light_test = executable(
  'gworldscene-light-test',
  'light-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)

picking_test = executable(
  'gworldscene-picking-test',
  'picking-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: gworldscene_core_dep,
)

test('geo', geo_test)
test('scene-node', node_test)
test('lod', lod_test)
test('camera', camera_test)
test('light', light_test)
test('picking', picking_test)

python = import('python').find_installation()
sqgi = find_program('sqgi', required: false)
bindings_env = environment()
bindings_dir = meson.project_build_root() / 'src'
bindings_env.set('GI_TYPELIB_PATH', bindings_dir)
bindings_env.prepend('LD_LIBRARY_PATH', bindings_dir)
bindings_env.prepend('DYLD_LIBRARY_PATH', bindings_dir)
bindings_env.prepend('PATH', bindings_dir)
bindings_env.set('GTK_A11Y', 'none')

if gworldscene_gtk3_built
  test('gir-gtk3', python, args: [files('gir-test.py'), gworldscene_gtk3_gir[0]], suite: 'bindings')
  if sqgi.found()
    test('bindings-sqgi-gtk3', sqgi,
      args: [files('bindings-test.nut'), '3', bindings_dir],
      depends: gworldscene_gtk3_gir, env: bindings_env, protocol: 'tap', suite: 'bindings')
  endif
  view_gtk3_test = executable(
    'gworldscene-view-gtk3-test',
    'view-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk3_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=3'],
  )
  test('scene-view-gtk3', view_gtk3_test)
  terrain_gtk3_test = executable(
    'gworldscene-terrain-gtk3-test', 'terrain-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk3_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=3'],
  )
  test('terrain-gtk3', terrain_gtk3_test)
  render_gtk3_test = executable(
    'gworldscene-render-gtk3-test',
    'render-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk3_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=3'],
  )
  test('render-gtk3-auto', render_gtk3_test, args: ['-p', '/render/automatic'], suite: 'render', timeout: 120)
  # GTK 3's X11 backend selects GLES at display initialization, via GDK_GL.
  test('render-gtk3-gles', render_gtk3_test, args: ['-p', '/render/gles'],
    env: ['GDK_GL=gles'], suite: 'render', timeout: 120)
endif

if gworldscene_gtk4_built
  test('gir-gtk4', python, args: [files('gir-test.py'), gworldscene_gtk4_gir[0]], suite: 'bindings')
  if sqgi.found()
    test('bindings-sqgi-gtk4', sqgi,
      args: [files('bindings-test.nut'), '4', bindings_dir],
      depends: gworldscene_gtk4_gir, env: bindings_env, protocol: 'tap', suite: 'bindings')
  endif
  view_gtk4_test = executable(
    'gworldscene-view-gtk4-test',
    'view-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk4_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=4'],
  )
  test('scene-view-gtk4', view_gtk4_test)
  terrain_gtk4_test = executable(
    'gworldscene-terrain-gtk4-test', 'terrain-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk4_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=4'],
  )
  test('terrain-gtk4', terrain_gtk4_test)
  render_gtk4_test = executable(
    'gworldscene-render-gtk4-test',
    'render-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk4_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=4'],
  )
  test('render-gtk4-auto', render_gtk4_test, args: ['-p', '/render/automatic'], suite: 'render', timeout: 120)
  test('render-gtk4-gles', render_gtk4_test, args: ['-p', '/render/gles'], suite: 'render', timeout: 120)
endif

water_test = executable('gworldscene-water-test', 'water-test.cpp',
  include_directories: gworldscene_inc,
  dependencies: [gworldscene_gtk4_built ? gworldscene_gtk4_dep : gworldscene_gtk3_dep, gdal_dep, zlib_dep],
)
test('water', water_test)

graphics_math_test = executable('gworldscene-graphics-math-test', 'graphics-math-test.cpp',
  include_directories: gworldscene_inc, dependencies: gworldscene_core_dep)
test('graphics-math', graphics_math_test)
