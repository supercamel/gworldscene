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

if gworldscene_gtk3_built
  view_gtk3_test = executable(
    'gworldscene-view-gtk3-test',
    'view-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk3_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=3'],
  )
  test('scene-view-gtk3', view_gtk3_test)
endif

if gworldscene_gtk4_built
  view_gtk4_test = executable(
    'gworldscene-view-gtk4-test',
    'view-test.cpp',
    include_directories: gworldscene_inc,
    dependencies: gworldscene_gtk4_dep,
    cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=4'],
  )
  test('scene-view-gtk4', view_gtk4_test)
endif
