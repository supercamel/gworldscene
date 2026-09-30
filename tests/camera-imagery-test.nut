// Real GI demand regression: changing only projection must request finer tiles.
const PI = 3.141592653589793
local major = vargv[0]
local Gtk = import("Gtk", major + ".0")
local Gdk = import("Gdk", major + ".0")
local GLib = import("GLib")
local Scene = import("GWorldSceneGtk" + major, "0.1")
if (major == "3") Gtk.init_check(null); else Gtk.init_check()
if (Gdk.Display.get_default() == null) { print("1..0 # SKIP No display\n"); return }
local scene = Scene.SceneView.new()
scene.set_cache_enabled(false)
local terrain = Scene.SceneTerrainSource.new()
terrain.put_tile(0,0,2,GLib.Bytes.new("\x00\x00\x00\x00\x00\x00\x00\x00"))
scene.set_terrain_source(terrain)
local provider = Scene.SceneTileProvider.new(0,19,256)
scene.set_tile_provider(provider)
scene.set_offscreen_enabled(true)
// 45 degrees down, looking north. Ground focus is ~1 km north of eye.
scene.set_camera_pose(0.5,0.5,1000,cos(PI/8),0,-sin(PI/8),0)
scene.set_camera_projection(1280,720,800,800,640,360,0.01,100000)
local window = major == "4" ? Gtk.Window.new() : Gtk.Window.new(Gtk.WindowType.toplevel)
window.set_default_size(640,360)
if (major == "4") window.set_child(scene); else window.add(scene)
window.realize(); scene.realize()
local context = GLib.MainContext.default()
function finest() {
  local result = {z = -1, tiles = []}
  foreach (key in provider.dup_demand()) {
    local parts = split(key,"/"); local z = parts[0].tointeger()
    if (z > result.z) {result.z = z; result.tiles.clear()}
    if (z == result.z) result.tiles.append([parts[1].tointeger(),parts[2].tointeger()])
  }
  return result
}
function wait_zoom(level) {
  local end = GLib.get_monotonic_time() + 10000000
  while (GLib.get_monotonic_time() < end) {
    for (local i = 0; i < 100 && context.pending(); i++) context.iteration(false)
    local result = finest()
    if (result.z >= level) return result
    GLib.usleep(1000)
  }
  throw "Calibrated projection did not update imagery demand"
}
local wide = wait_zoom(17)
scene.set_camera_projection(1280,720,6400,6400,640,360,0.01,100000)
local zoom = wait_zoom(19)
if (zoom.z <= wide.z) throw "Zoom enlarged existing imagery instead of requesting detail"
// The finest band follows the viewed ground, not the point beneath the camera.
local eyeY = (1-log(tan(PI/4 + 0.5*PI/360))/PI)*0.5*pow(2,zoom.z)
local northOfEye = false
foreach (tile in zoom.tiles) if (tile[1]+1 < eyeY) northOfEye = true
if (!northOfEye) throw "Fine imagery did not follow the oblique ground footprint"
window.destroy()
print("1..1\nok 1 - calibrated zoom updates ground-focused imagery demand\n")
