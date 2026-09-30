#!/usr/bin/env sqgi
// GTK 4 example: exact pose + pinhole intrinsics over real maps and terrain.
local GLib = import("GLib")
local Gio = import("Gio")
local Gtk = import("Gtk", "4.0")
local Scene = import("GWorldSceneGtk4", "0.1")

local smoke = vargv.find("--smoke-test") != null
local offline = smoke || vargv.find("--offline") != null
local site = offline ? [0.498, 0.5] : [-16.8878, 145.7048]
local app = Gtk.Application.new("com.supercamel.GWorldScene.CalibratedCamera",
                               Gio.ApplicationFlags.non_unique)
local ui = { view = null, fields = {}, status = null, changing = false,
             nodes = [], controllers = [], failed = false }
local defaults = { heading = 0, pitch = -25, roll = 0, altitude = 140,
                   east = 0, north = 0, zoom = 1, shift = 0 }
if (!offline) {
  defaults.heading = -70; defaults.pitch = -35; defaults.altitude = 2500
}
const PI = 3.141592653589793

// Intrinsic Z-Y-X rotation: heading about Down, pitch about Right, roll
// about Forward. The quaternion maps optical forward/right/down into NED.
function pose_quaternion(heading, pitch, roll) {
  local h = heading * PI / 360, p = pitch * PI / 360, r = roll * PI / 360
  local ch = cos(h), sh = sin(h), cp = cos(p), sp = sin(p), cr = cos(r), sr = sin(r)
  return [ch*cp*cr + sh*sp*sr, ch*cp*sr - sh*sp*cr,
          ch*sp*cr + sh*cp*sr, sh*cp*cr - ch*sp*sr]
}

function update_camera() {
  if (ui.changing) return
  local values = {}
  foreach (name, control in ui.fields) values[name] <- control.get_value()
  local q = pose_quaternion(values.heading, values.pitch, values.roll)
  // Approximate local metre offsets around the selected demonstration site.
  local lat = site[0] + values.north / 111320.0
  local lon = site[1] + values.east / (111320.0 * cos(lat * PI / 180))
  ui.view.set_camera_pose(lat, lon, values.altitude, q[0], q[1], q[2], q[3])
  // 1280 x 720 virtual sensor; scroll changes focal length, not eye position.
  local focal = 800.0 * values.zoom
  ui.view.set_camera_projection(1280, 720, focal, focal,
                               640.0 + values.shift, 360.0, 0.01, 100000.0)
  ui.status.set_text(format("Zoom %.2fx  |  Vertical FOV %.1f°\nEye %.5f°, %.5f° · %.0f m AMSL",
                           values.zoom, 2 * atan(360.0 / focal) * 180 / PI,
                           lat, lon, values.altitude))
}

function reset_camera() {
  ui.changing = true
  foreach (name, value in defaults) ui.fields[name].set_value(value)
  ui.changing = false
  update_camera()
}

function zoom_by(dy) {
  local control = ui.fields.zoom
  local value = control.get_value() * pow(1.15, -dy)
  control.set_value(value < 0.5 ? 0.5 : (value > 12 ? 12 : value))
}

function slider(panel, name, title, low, high, step, hint) {
  local label = Gtk.Label.new(title)
  label.set_xalign(0)
  panel.append(label)
  local scale = Gtk.Scale.new_with_range(Gtk.Orientation.horizontal, low, high, step)
  scale.set_digits(step < 1 ? 2 : 0)
  scale.set_draw_value(true)
  scale.set_value_pos(Gtk.PositionType.right)
  scale.set_value(defaults[name])
  scale.set_hexpand(true)
  scale.set_tooltip_text(hint)
  panel.append(scale)
  ui.fields[name] <- scale
  scale.connect("value-changed", function(...) { update_camera() })
}

function add_test_scene(view) {
  // A shared, supplied flat terrain tile avoids HTTP and makes the demo repeatable.
  local terrain = Scene.SceneTerrainSource.new()
  terrain.put_tile(0, 0, 2, GLib.Bytes.new("\x00\x00\x00\x00\x00\x00\x00\x00"))
  view.set_terrain_source(terrain)
  // An empty application provider disables native imagery downloads too.
  view.set_tile_provider(Scene.SceneTileProvider.new(0, 0, 256))
  view.set_cache_enabled(false)
  view.set_shadows_enabled(false)
  view.set_sun_time_of_day(12)
  // Ground grid, 55 m spacing, and differently sized landmarks for depth cues.
  for (local i = -5; i <= 5; i++) {
    foreach (vertical in [false, true]) {
      local line = view.add_polyline()
      line.set_color(0.35, 0.48, 0.52)
      line.set_width(0.8)
      if (vertical) {
        line.append_point(0.4975, 0.5 + i*0.0005, 0.3)
        line.append_point(0.5025, 0.5 + i*0.0005, 0.3)
      } else {
        line.append_point(0.5 + i*0.0005, 0.4975, 0.3)
        line.append_point(0.5 + i*0.0005, 0.5025, 0.3)
      }
      ui.nodes.append(line)
    }
  }
  foreach (item in [[0.500, 0.4993, 25, 0.95, 0.25, 0.12],
                    [0.501, 0.5000, 50, 0.12, 0.55, 0.95],
                    [0.500, 0.5007, 15, 0.95, 0.75, 0.12]]) {
    local node = view.add_cube(item[0], item[1], item[2]/2.0, 30, 30, item[2])
    node.set_color(item[3], item[4], item[5])
    ui.nodes.append(node)
  }
}

app.connect("activate", function(...) {
  local window = Gtk.ApplicationWindow.new(app)
  window.set_title("GWorldScene · Calibrated camera")
  window.set_default_size(1200, 800)
  local layout = Gtk.Box.new(Gtk.Orientation.horizontal, 12)
  window.set_child(layout)
  local scroll_panel = Gtk.ScrolledWindow.new()
  scroll_panel.set_policy(Gtk.PolicyType.never, Gtk.PolicyType.automatic)
  scroll_panel.set_size_request(305, -1)
  scroll_panel.set_hexpand(false)
  layout.append(scroll_panel)
  local panel = Gtk.Box.new(Gtk.Orientation.vertical, 5)
  panel.set_margin_start(16); panel.set_margin_end(8)
  panel.set_margin_top(16); panel.set_margin_bottom(16)
  scroll_panel.set_child(panel)
  local title = Gtk.Label.new("Camera pose & optics")
  title.set_xalign(0); title.add_css_class("title-3"); panel.append(title)
  local help = Gtk.Label.new("Scroll over the scene to zoom.\nUse the sliders to move and rotate.\nBlack bars preserve the sensor aspect.")
  help.set_xalign(0); help.set_wrap(true); panel.append(help)
  ui.view = Scene.SceneView.new()
  ui.view.set_hexpand(true); ui.view.set_vexpand(true)
  // Keep the renderer passive. Its built-in observer gestures would replace
  // exact pose with orbit controls. The enclosing widget owns camera input.
  ui.view.set_can_target(false)
  local surface = Gtk.Overlay.new()
  surface.set_hexpand(true); surface.set_vexpand(true)
  surface.set_child(ui.view); layout.append(surface)
  local input = Gtk.DrawingArea.new()
  input.set_hexpand(true); input.set_vexpand(true)
  surface.add_overlay(input)
  if (offline) add_test_scene(ui.view)
  else {
    // Leave the native terrain source and disk cache enabled. The library
    // fetches elevation from its default server (currently ooblerg.xyz).
    local tiles = GLib.getenv("GWORLD_SCENE_MAP_TILE_URL_TEMPLATE")
    if (tiles != null && tiles != "") ui.view.set_map_tile_url_template(tiles)
    else {
      // Match simple-scene.nut: optional Google Map Tiles session/key, then
      // the same legacy satellite template used by the other examples.
      local key = GLib.getenv("GWORLD_SCENE_GOOGLE_MAPS_API_KEY")
      local session = GLib.getenv("GWORLD_SCENE_GOOGLE_MAPS_SESSION")
      if (key != null && key != "" && session != null && session != "")
        ui.view.set_map_tile_url_template("https://tile.googleapis.com/v1/2dtiles/{z}/{x}/{y}?session=" +
          GLib.uri_escape_string(session, null, false) + "&key=" + GLib.uri_escape_string(key, null, false))
      else ui.view.set_map_tile_url_template("https://mt.google.com/vt/lyrs=s&x={x}&y={y}&z={z}")
    }
    ui.view.set_sun_time_of_day(14)
    ui.view.set_terrain_normal_smoothing(0.92)
    local credit = GLib.getenv("GWORLD_SCENE_MAP_ATTRIBUTION")
    if (credit == null || credit == "")
      credit = (tiles == null || tiles == "") ? "Imagery © Google" : "Custom imagery provider"
    local credits = Gtk.Label.new("")
    credits.set_markup("<span foreground=\"#ffffff\" background=\"#202020\">" +
      GLib.markup_escape_text(credit + " · SRTM terrain / ooblerg.xyz", -1) + "</span>")
    credits.set_halign(Gtk.Align.end); credits.set_valign(Gtk.Align.end)
    credits.set_margin_end(8); credits.set_margin_bottom(8)
    credits.add_css_class("card")
    surface.add_overlay(credits)
    local location = Gtk.Label.new("Cairns foothills, Australia\nMaps and elevation load automatically.")
    location.set_xalign(0); location.set_wrap(true); panel.append(location)
  }
  slider(panel, "heading", "Heading (°)", -180, 180, 1, "Positive heading turns east from north.")
  slider(panel, "pitch", "Pitch (°)", -90, 90, 1, "Negative pitch looks down. Nadir is −90°.")
  slider(panel, "roll", "Roll (°)", -180, 180, 1, "Rotate around the optical forward axis.")
  slider(panel, "altitude", "Camera altitude (m AMSL)", offline ? 2 : 1500, offline ? 500 : 6000, 1, "Absolute altitude above mean sea level, not height above terrain.")
  slider(panel, "east", "Move east / west (m)", offline ? -250 : -3000, offline ? 250 : 3000, 1, "Translate the eye without changing orientation.")
  slider(panel, "north", "Move north / south (m)", offline ? -250 : -3000, offline ? 250 : 3000, 1, "Translate the eye without changing orientation.")
  slider(panel, "zoom", "Optical zoom", 0.5, 12, 0.05, "Change fx and fy together; this is not digital cropping.")
  slider(panel, "shift", "Principal point X offset (pixels)", -400, 400, 1, "Shift cx on the 1280 × 720 sensor without rotating the camera.")
  local reset = Gtk.Button.new_with_label("Reset camera")
  reset.connect("clicked", function(...) { reset_camera() }); panel.append(reset)
  ui.status = Gtk.Label.new("")
  ui.status.set_xalign(0); ui.status.set_wrap(true); panel.append(ui.status)
  local scroll = Gtk.EventControllerScroll.new(Gtk.EventControllerScrollFlags.vertical | Gtk.EventControllerScrollFlags.discrete)
  scroll.connect("scroll", function(dx, dy) { zoom_by(dy); return true })
  input.add_controller(scroll); ui.controllers.append(scroll)
  reset_camera()
  window.present()
  if (smoke) GLib.timeout_add(GLib.PRIORITY_DEFAULT, 400, function(...) {
    try {
      ui.fields.roll.set_value(35)
      ui.fields.pitch.set_value(-40)
      ui.fields.east.set_value(20)
      zoom_by(-2)
      ui.fields.shift.set_value(100)
      if (!ui.view.get_camera_pose_enabled() || ui.fields.zoom.get_value() <= 1)
        throw "Camera controls did not update pose/zoom"
      local frame = ui.view.capture_frame(640, 360, 1)
      if (frame.get_image().get_width() != 640) throw "Capture failed"
      local output = GLib.getenv("GWORLD_SCENE_DEMO_SCREENSHOT")
      if (output != null && output != "") frame.get_image().savev(output, "png", [], [])
      reset_camera()
      if (ui.fields.roll.get_value() != 0 || ui.fields.zoom.get_value() != 1)
        throw "Reset failed"
      print("Camera example: pose, roll, zoom, projection and capture passed\n")
    } catch (e) { ui.failed = true; print("Camera example failed: " + e + "\n") }
    app.quit()
    return false
  })
})
app.run(1, ["gworldscene-calibrated-camera"])
if (ui.failed) throw "Camera example smoke test failed"
