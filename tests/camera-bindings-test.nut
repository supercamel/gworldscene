// Actual introspection consumer: no dependency on a particular payload/application.
local major=vargv[0]
local Gtk=import("Gtk",major+".0");local Gdk=import("Gdk",major+".0")
local GLib=import("GLib");local GI=import("GIRepository","2.0")
if(major=="3")Gtk.init_check(null);else Gtk.init_check()
if(Gdk.Display.get_default()==null){print("1..0 # SKIP No display\n");return}
local Scene=import("GWorldSceneGtk"+major,"0.1")
local expected=GLib.canonicalize_filename(vargv[1],null)+"/GWorldSceneGtk"+major+"-0.1.typelib"
if(GI.Repository.get_default().get_typelib_path("GWorldSceneGtk"+major)!=expected)throw "Wrong typelib"
local view=Scene.SceneView.new();view.set_cache_enabled(false)
local terrain=Scene.SceneTerrainSource.new();view.set_terrain_source(terrain)
local tile=GLib.Bytes.new("\x00\x64\x00\x64\x00\x64\x00\x64")
if(!terrain.put_tile(0,0,2,tile) || terrain.get_altitude(0.5,0.5)!=100)throw "Terrain sample round trip"
local missing=false;try{terrain.get_altitude(2.5,2.5)}catch(e){missing=true}
if(!missing)throw "Missing terrain accepted as zero"
local imagery=Scene.SceneTileProvider.new(0,0,256);view.set_tile_provider(imagery)
view.set_offscreen_enabled(true)
if(!view.get_offscreen_enabled())throw "Offscreen round trip"
if(!view.set_camera_pose(0.5,0.5,-5,2,0,0,0))throw "Pose rejected"
local q=view.get_camera_quaternion()
if(typeof q!="array" || q.len()!=4 || q[0]!=1 || q[1]!=0 || q[2]!=0 || q[3]!=0 || !view.get_camera_pose_enabled())throw "Quaternion output shape"
if(view.get_camera()[2]!=-5)throw "Exact altitude clamped"
if(!view.set_camera_projection(320,240,200,180,140,100,0.01,100000))throw "Projection rejected"
local rejected=false;try{view.set_camera_pose(0,0,0,0,0,0,0)}catch(e){rejected=true}
if(!rejected)throw "Zero quaternion accepted"
local window=major=="4"?Gtk.Window.new():Gtk.Window.new(Gtk.WindowType.toplevel)
if(major=="4")window.set_child(view);else window.add(view)
window.realize();view.realize()
local frame=view.capture_frame(320,240,123456)
if(frame==null || frame.get_sequence()!=1 || frame.get_timestamp()!=123456 || frame.get_completed_time()<=0)throw "Frame metadata"
local image=frame.get_image()
if(image.get_width()!=320 || image.get_height()!=240 || image.get_n_channels()!=4)throw "Frame pixels"
local next=view.capture_frame(160,120,234567)
if(next.get_sequence()!=2 || frame.get_timestamp()!=123456 || image.get_width()!=320)throw "Frame lifetime"
view.reset_camera_projection();view.set_free_camera_orientation(0,0)
if(view.get_camera_pose_enabled())throw "Legacy camera mode retained exact orientation"
window.destroy()
if(frame.get_image().get_width()!=320)throw "View destruction invalidated frame"
print("1..1\nok 1 - calibrated camera, offscreen frames and shared terrain through SQGI\n")
