#!/usr/bin/env sqgi

local GLib = import("GLib")
local Gio = import("Gio")
local Gtk = import("Gtk", "4.0")
local GWorldScene = import("GWorldScene", "0.1")

const INITIAL_LATITUDE = -16.8878
const INITIAL_LONGITUDE = 145.7048
const INITIAL_ALTITUDE_AMSL = 7800.0

local app = null
local W = {
  window = null,
  view = null,
  nodes = [],
}

function env_is_set(value) {
  return value != null && value != ""
}

function path_exists(path) {
  return Gio.File.new_for_path(path).query_exists(null)
}

function find_demo_asset_path(relative_path) {
  local prefixes = ["", "..", "../.."]
  foreach (prefix in prefixes) {
    local path = prefix == ""
      ? relative_path
      : GLib.build_filenamev([prefix, relative_path])
    if (path_exists(path))
      return path
  }
  return null
}

function configure_map_tiles(view) {
  local tile_template = GLib.getenv("GWORLD_SCENE_MAP_TILE_URL_TEMPLATE")
  if (env_is_set(tile_template)) {
    view.set_map_tile_url_template(tile_template)
    return
  }

  local google_api_key = GLib.getenv("GWORLD_SCENE_GOOGLE_MAPS_API_KEY")
  local google_session = GLib.getenv("GWORLD_SCENE_GOOGLE_MAPS_SESSION")
  if (env_is_set(google_api_key) && env_is_set(google_session)) {
    local escaped_key = GLib.uri_escape_string(google_api_key, null, false)
    local escaped_session = GLib.uri_escape_string(google_session, null, false)
    view.set_map_tile_url_template(
      "https://tile.googleapis.com/v1/2dtiles/{z}/{x}/{y}?session=" +
      escaped_session + "&key=" + escaped_key)
    return
  }

  view.set_map_tile_url_template("https://mt.google.com/vt/lyrs=s&x={x}&y={y}&z={z}")
}

function add_scene_nodes(view) {
  W.nodes = []

  local cube = view.add_cube(INITIAL_LATITUDE,
                             INITIAL_LONGITUDE,
                             650.0,
                             900.0,
                             900.0,
                             900.0)
  cube.set_color(1.0, 0.08, 0.02)
  cube.set_orientation_ned(35.0, 0.0, 12.0)
  W.nodes.append(cube)

  local sphere = view.add_sphere(-16.8780, 145.7200, 450.0, 360.0)
  sphere.set_color(0.18, 0.55, 0.95)
  W.nodes.append(sphere)

  local cylinder = view.add_cylinder(-16.9040, 145.6880, 420.0, 320.0, 700.0)
  cylinder.set_color(0.40, 0.78, 0.30)
  W.nodes.append(cylinder)

  /*
  local model_path = find_demo_asset_path("examples/assets/ToyCar/ToyCar.glb")
  if (model_path != null) {
    local model = view.add_model(model_path, -16.8910, 145.7040, 650.0)
    model.set_scale(12000.0, 12000.0, 12000.0)
    model.set_color(1.0, 1.0, 1.0)
    model.set_orientation_ned(120.0, 0.0, 0.0)
    W.nodes.append(model)
  }
  */

  local marker_path = find_demo_asset_path("examples/assets/BillboardMarker/map-symbol-location-02.png")
  if (marker_path != null) {
    local marker = view.add_billboard(marker_path, -16.8840, 145.7510, 500.0)
    marker.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
    marker.set_size_limits(28.0, 96.0)
    marker.set_reference_size(64.0, 1800.0)
    marker.set_max_visible_distance(90000.0)
    W.nodes.append(marker)
  }

  local overlay_path = find_demo_asset_path("examples/assets/GroundOverlay/aiga-heliport.png")
  if (overlay_path != null) {
    local overlay = view.add_ground_overlay(overlay_path,
                                            -16.8890, 145.7460,
                                            -16.8890, 145.7545,
                                            -16.8950, 145.7545,
                                            -16.8950, 145.7460)
    overlay.set_opacity(0.82)
    overlay.set_altitude_offset(5.0)
    W.nodes.append(overlay)
  }

  local route = view.add_polyline()
  route.set_color(1.0, 0.86, 0.18)
  route.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
  route.append_point(-16.8290, 145.6460, 90.0)
  route.append_point(-16.8500, 145.6755, 120.0)
  route.append_point(-16.8800, 145.7170, 90.0)
  route.append_point(-16.9190, 145.7780, 60.0)
  W.nodes.append(route)

  local area = view.add_polygon()
  area.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
  area.set_color(0.95, 0.25, 0.85)
  area.append_point(-16.8650, 145.6795, 1.5)
  area.append_point(-16.8505, 145.7040, 1.5)
  area.append_point(-16.8845, 145.7310, 1.5)
  area.append_point(-16.9060, 145.6950, 1.5)
  W.nodes.append(area)

  local circle = view.add_circle(-16.8280, 145.6530, 1.50, 520.0)
  circle.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
  circle.set_color(0.15, 0.95, 0.92)
  W.nodes.append(circle)

  local label = view.add_text_label("SQGI", -16.8280, 145.6530, 220.0)
  label.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
  label.set_color(1.0, 1.0, 1.0)
  label.set_reference_size(54.0, 2500.0)
  label.set_size_limits(20.0, 84.0)
  W.nodes.append(label)
}

function activate() {
  local window = Gtk.ApplicationWindow.new(app)
  window.set_title("GWorldScene SQGI")
  window.set_default_size(1100, 760)
  W.window = window

  local view = GWorldScene.SceneView.new()
  W.view = view
  configure_map_tiles(view)
  view.set_camera(INITIAL_LATITUDE, INITIAL_LONGITUDE, INITIAL_ALTITUDE_AMSL)
  view.set_camera_orientation(72.0, -66.0)
  view.set_sun_time_of_day(15.25)
  view.set_fog_range(9000.0, 90000.0)
  view.set_shadows_enabled(true)
  view.set_terrain_normal_smoothing(0.92)
  add_scene_nodes(view)

  window.set_child(view)
  window.present()
}

app = Gtk.Application.new("au.com.silvertone.GWorldScene.SQGI",
                          Gio.ApplicationFlags.flags_none)
app.connect("activate", activate)

local argv = ["gworldscene-sqgi-simple-scene"]
foreach (arg in vargv) argv.append(arg)

local status = app.run(argv.len(), argv)
print("Application exited with status " + status + "\n")
