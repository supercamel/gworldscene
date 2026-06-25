executable(
  'gworldscene-demo',
  'simple.c',
  dependencies: gworldscene_dep,
  install: false,
)

executable(
  'gworldscene-controls',
  'control-panel.vala',
  include_directories: gworldscene_inc,
  link_with: gworldscene_lib,
  dependencies: [
    gworldscene_vapi_dep,
    dependency('gtk4'),
  ],
  install: false,
)
