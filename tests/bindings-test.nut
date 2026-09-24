// Exercise the generated typelibs directly, in a separate process per GTK version.
local major = vargv[0]
local GLib = import("GLib", "2.0")
local Gtk = import("Gtk", major + ".0")
local Gdk = import("Gdk", major + ".0")
if (major == "3") Gtk.init_check(null)
else Gtk.init_check()
if (Gdk.Display.get_default() == null) {
  print("1..0 # SKIP No GTK display available; run under xvfb-run\n")
  return
}

local namespace = "GWorldSceneGtk" + major
local Scene = import(namespace, "0.1")
local GI = import("GIRepository", "2.0")
local typelib = GI.Repository.get_default().get_typelib_path(namespace)
local expected_typelib = GLib.build_filenamev([vargv[1], namespace + "-0.1.typelib"])
if (typelib != expected_typelib) throw "Loaded unexpected typelib: " + typelib
print("# Typelib: " + typelib + "\n")

local view = Scene.SceneView.new()
view.set_cache_enabled(false)
view.set_free_camera_position(-35, 149, 600)
view.set_free_camera_orientation(25, -10)
view.set_sun_position(213, 37)
view.set_fog_range(120, 3400)
view.set_fog_color(0.13, 0.47, 0.81)
view.set_atmosphere_density(0.6)
view.set_atmosphere_haze(1.5)
view.set_water_wave_strength(0.25)
view.set_water_tile_url_template(null)

local cube = view.add_cube(-35, 149, 600, 1, 2, 3)
cube.set_position(-34, 148, 725)
cube.set_orientation_ned(450, 12, -7)
cube.set_scale(1.25, 2.5, 3.75)
cube.set_color(-0.2, 0.35, 1.2)
cube.set_dimensions(11, 23, 37)
cube.set_roughness(0.2)
cube.set_metallic(0.8)
local cylinder = view.add_cylinder(-35, 149, 600, 1, 2)
cylinder.set_size(19, 43)
local billboard = view.add_billboard("unused.png", -35, 149, 600)
billboard.set_size_limits(17, 129)
billboard.set_reference_size(53, 2700)
local overlay = view.add_ground_overlay("unused.png", -30, 140, -31, 141, -32, 142, -33, 143)
overlay.set_corners(-34, 144, -35, 145, -36, 146, -37, 147)
local polygon = view.add_polygon()
polygon.set_fill_color(0.11, 0.22, 0.33, 0.44)
polygon.set_outline_color(0.55, 0.66, 0.77, 0.88)
local circle = view.add_circle(-35, 149, 600, 100)
circle.set_fill_color(0.14, 0.28, 0.42, 0.56)
circle.set_outline_color(0.65, 0.51, 0.37, 0.23)
local label = view.add_text_label("Binding test", -35, 149, 600)
label.set_text_color(0.12, 0.34, 0.56, 0.78)
label.set_background_color(0.87, 0.65, 0.43, 0.21)
label.set_size_limits(21, 153)
label.set_reference_size(67, 3900)

local cases = [
  ["atmosphere density", @() [view.get_atmosphere_density()], [0.6]],
  ["atmosphere haze", @() [view.get_atmosphere_haze()], [1.5]],
  ["water waves", @() [view.get_water_wave_strength()], [0.25]],
  ["material roughness", @() [cube.get_roughness()], [0.2]],
  ["material metallic", @() [cube.get_metallic()], [0.8]],
  ["view camera", @() view.get_camera(), [-35, 149, 600]],
  ["view camera orientation", @() view.get_camera_orientation(), [25, -10]],
  ["view free position", @() view.get_free_camera_position(), [-35, 149, 600]],
  ["view free orientation", @() view.get_free_camera_orientation(), [25, -10]],
  ["node position", @() cube.get_position(), [-34, 148, 725]],
  ["node orientation", @() cube.get_orientation_ned(), [90, 12, -7]],
  ["node scale", @() cube.get_scale(), [1.25, 2.5, 3.75]],
  ["node clamped color", @() cube.get_color(), [0, 0.35, 1]],
  // Explicitly invoke the base method as well as the cube's override.
  ["node dimensions", @() Scene.SceneNode.get_dimensions.call(cube), [11, 23, 37]],
  ["cube dimensions", @() cube.get_dimensions(), [11, 23, 37]],
  ["cylinder size", @() cylinder.get_size(), [19, 43]],
  ["billboard limits", @() billboard.get_size_limits(), [17, 129]],
  ["billboard reference", @() billboard.get_reference_size(), [53, 2700]],
  ["overlay corners", @() overlay.get_corners(), [-34, 144, -35, 145, -36, 146, -37, 147]],
  ["polygon fill", @() polygon.get_fill_color(), [0.11, 0.22, 0.33, 0.44]],
  ["polygon outline", @() polygon.get_outline_color(), [0.55, 0.66, 0.77, 0.88]],
  ["circle fill", @() circle.get_fill_color(), [0.14, 0.28, 0.42, 0.56]],
  ["circle outline", @() circle.get_outline_color(), [0.65, 0.51, 0.37, 0.23]],
  ["label text", @() label.get_text_color(), [0.12, 0.34, 0.56, 0.78]],
  ["label background", @() label.get_background_color(), [0.87, 0.65, 0.43, 0.21]],
  ["label limits", @() label.get_size_limits(), [21, 153]],
  ["label reference", @() label.get_reference_size(), [67, 3900]],
  ["sun position", @() view.get_sun_position(), [213, 37]],
  ["fog range", @() view.get_fog_range(), [120, 3400]],
  ["fog color", @() view.get_fog_color(), [0.13, 0.47, 0.81]]
]

print("1.." + cases.len() + "\n")
foreach (index, test in cases) {
  local values = test[1]()
  if (typeof values != "array" || values.len() != test[2].len())
    throw test[0] + ": wrong return shape"
  foreach (i, expected in test[2]) {
    // Fog colors and sun direction use float storage internally.
    if (typeof values[i] != "float" || fabs(values[i] - expected) > 0.0001)
      throw test[0] + ": wrong output at index " + i
  }
  print("ok " + (index + 1) + " - " + test[0] + "\n")
}

// Never enter a main loop: these round trips need no GL context or downloads.
view.clear_nodes()
