if gworldscene_gtk3_built
  executable(
    'gworldscene-demo-gtk3',
    'simple.c',
    dependencies: gworldscene_gtk3_dep,
    c_args: ['-DGWORLD_SCENE_GTK_MAJOR=3'],
    install: false,
  )
endif

if gworldscene_gtk4_built
  executable(
    'gworldscene-demo-gtk4',
    'simple.c',
    dependencies: gworldscene_gtk4_dep,
    c_args: ['-DGWORLD_SCENE_GTK_MAJOR=4'],
    install: false,
  )

  executable(
    'gworldscene-controls-gtk4',
    'control-panel.vala',
    include_directories: gworldscene_inc,
    link_with: [
      gworldscene_core_lib,
      gworldscene_gtk4_lib,
    ],
    dependencies: [
      gworldscene_gtk4_vapi_dep,
      gtk4_dep,
    ],
    install: false,
  )
endif
