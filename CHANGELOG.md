# Changelog

## 0.2.1 — 2026-09-28

- Anchor text cards above their geographic point with a three-pixel gap, fixing
  their lower half intersecting flat terrain at oblique camera angles. Terrain
  occlusion remains enabled; image billboards remain centered.
- Support multiline text labels and match picking bounds to the rendered font,
  padding, dimensions and anchor.
- Add native GL regression coverage for ground-level multiline labels.

The API namespace and pkg-config suffix remain `0.1`.

## 0.2.0 — 2026-09-25

First tagged release of GWorldScene.

- GTK 3 and GTK 4 widget libraries with a shared scene graph and geospatial core,
  automatic desktop OpenGL / OpenGL ES selection, and C, Vala, and SQGI examples.
- Application-supplied imagery providers with regional fallback and attribution
  lifetime tracking.
- Atmospheric scattering, terrain lighting, roughness/metallic materials,
  optional cascaded shadows, and optional reflective water.
- Improved terrain loading, imagery detail bands and filtering, source-isolated
  caches, and rendering across the dateline.
- Corrected introspection annotations, Vala headers, and terrain-following
  overlays, with regression coverage for rendering, bindings, and imagery.
- Updated documentation and screenshot gallery.

The API namespace and pkg-config suffix remain `0.1` (for example,
`GWorldSceneGtk4-0.1` and `gworldscene-gtk4-0.1`). The library release version is
`0.2.0`.

Known limits: reflective water follows the elevation mesh and reflects the sky
and sun; it does not flatten lakes or reflect scene objects. Imported normal and
packed material textures are not sampled. See `docs/concepts.rst` for details.
