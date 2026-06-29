#!/usr/bin/env sqgi

local GLib = import("GLib")
local gtk_major = ("GWORLD_SCENE_SQGI_GTK_MAJOR" in getroottable())
  ? GWORLD_SCENE_SQGI_GTK_MAJOR
  : 4
if (gtk_major != 3 && gtk_major != 4)
  throw "GWORLD_SCENE_SQGI_GTK_MAJOR must be 3 or 4"

local gtk_version = gtk_major + ".0"
local gworldscene_namespace = "GWorldSceneGtk" + gtk_major
local Gtk = import("Gtk", gtk_version)
local GWorldScene = import(gworldscene_namespace, "0.1")

function require_member(table, name) {
  if (!(name in table))
    throw "missing binding: " + name
}

require_member(GWorldScene, "SceneView")
require_member(GWorldScene.SceneView, "new")
require_member(GWorldScene.SceneView, "set_camera")
require_member(GWorldScene.SceneView, "add_cube")
require_member(GWorldScene.SceneView, "add_model")
require_member(GWorldScene, "SceneNode")
require_member(GWorldScene.SceneNode, "set_position")
require_member(GWorldScene.SceneNode, "set_orientation_ned")
require_member(GWorldScene.SceneNode, "set_scale")
require_member(GWorldScene, "ScenePolylineNode")
require_member(GWorldScene.ScenePolylineNode, "set_dashed")

print("SQGI can see " + gworldscene_namespace + " " + typeof GWorldScene.SceneView + "\n")
print("Gtk " + gtk_version + " Application binding: " + typeof Gtk.Application + "\n")
print("Monotonic time: " + GLib.get_monotonic_time() + "\n")
