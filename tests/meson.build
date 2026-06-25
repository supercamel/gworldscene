geo_test = executable(
  'gworldscene-geo-test',
  'geo-test.cpp',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: gworldscene_deps,
)

node_test = executable(
  'gworldscene-node-test',
  'node-test.cpp',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: gworldscene_deps,
)

lod_test = executable(
  'gworldscene-lod-test',
  'lod-test.cpp',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: gworldscene_deps,
)

camera_test = executable(
  'gworldscene-camera-test',
  'camera-test.cpp',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: gworldscene_deps,
)

light_test = executable(
  'gworldscene-light-test',
  'light-test.cpp',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: gworldscene_deps,
)

view_test = executable(
  'gworldscene-view-test',
  'view-test.cpp',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: gworldscene_deps,
)

test('geo', geo_test)
test('scene-node', node_test)
test('lod', lod_test)
test('camera', camera_test)
test('light', light_test)
test('scene-view', view_test)
