#!/usr/bin/env sqgi

local GLib = import("GLib")
local Gtk = import("Gtk", "4.0")
local GWorldScene = import("GWorldScene", "0.1")

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

print("SQGI can see GWorldScene " + typeof GWorldScene.SceneView + "\n")
print("Gtk.Application binding: " + typeof Gtk.Application + "\n")
print("Monotonic time: " + GLib.get_monotonic_time() + "\n")
