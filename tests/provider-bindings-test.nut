// Exercise provider ownership and annotations through the generated typelibs.
local major=vargv[0]
local Gtk=import("Gtk",major+".0")
local Gdk=import("Gdk",major+".0")
if(major=="3")Gtk.init_check(null)
else Gtk.init_check()
if(Gdk.Display.get_default()==null){
    print("1..0 # SKIP No GTK display available; run under xvfb-run\n")
    return
}
print("1..1\n")
local Gio=import("Gio")
local GLib=import("GLib")
local GI=import("GIRepository","2.0")
local Pb=import("GdkPixbuf","2.0")
local Scene=import("GWorldSceneGtk"+major,"0.1")
local expected=GLib.canonicalize_filename(vargv[1],null)+"/GWorldSceneGtk"+major+"-0.1.typelib"
if(GI.Repository.get_default().get_typelib_path("GWorldSceneGtk"+major)!=expected)throw "Unexpected GWorldScene typelib"
local app=Gtk.Application.new("com.supercamel.GWorldScene.ProviderTest",Gio.ApplicationFlags.non_unique)
local done=false;local failure=null
app.connect("activate",function(){
    local window=Gtk.ApplicationWindow.new(app);window.set_default_size(256,256)
    local scene=Scene.SceneView.new();scene.set_cache_enabled(false);scene.set_texture_memory_budget_mib(32)
    scene.set_water_enabled(false);scene.set_camera(0.5,0.5,1000);scene.set_camera_orientation(0,-89)
    local provider=Scene.SceneTileProvider.new(0,0,256)
    local requests=[];local released=[];local drained=false;local retry=false;local closing=false
    local annotationReleases=[];local demandRevision=0
    local wrong=Pb.Pixbuf.new(Pb.Colorspace.rgb,true,8,512,512)
    local pixels=Pb.Pixbuf.new(Pb.Colorspace.rgb,true,8,256,256);pixels.fill(0x209040ff)
    provider.connect("tile-requested",function(id,z,x,y){
        try {
            if(z!=0 || x!=0 || y!=0)throw "Parent request coordinates did not roundtrip"
            local demand=provider.dup_demand()
            if(demand.len()!=1 || demand[0]!="0/0/0" || demandRevision<1)throw "Native footprint/ordering did not roundtrip"
            requests.append(id)
            if(provider.complete_annotated(id,retry?pixels:wrong,z,"viewport-credit")!=retry)throw "Wrong-size validation failed"
            if(provider.complete_tile(id,pixels))throw "Duplicate completion accepted"
        } catch(e){failure=e;app.quit()}
    })
    provider.connect("tile-released",function(id){
        if(requests.find(id)==null || released.find(id)!=null){failure="Unknown/duplicate released identity";app.quit()}
        released.append(id)
    })
    provider.connect("drained",function(){drained=true})
    provider.connect("annotation-released",function(token){
        annotationReleases.append(token)
        if(token!="viewport-credit"){failure="Annotation ID did not roundtrip";app.quit()}
    })
    provider.connect("demand-changed",function(revision){
        if(revision<=demandRevision){failure="Demand revision did not increase";app.quit()}
        demandRevision=revision
    })
    scene.set_tile_provider(provider)
    // Runtime-selected GTK namespace: each method is exercised in its own process.
    local attach=major=="4"?"set_child":"add";window[attach](scene)
    local show=major=="4"?"present":"show_all";window[show]()
    sqgi.timeout_add(10,function(){
        try {
            if(!retry && provider.get_failed_count()==1){
                if(scene.get_imagery_ready())throw "Failed provider reported render ready"
                retry=true;provider.clear()
            }
            if(!closing && retry && scene.get_imagery_ready()){
                if(requests.len()!=2 || requests[1]<=requests[0] || released.len()!=1)throw "Request lifetime not preserved"
                if(!provider.get_reduced_detail())throw "Missing reduced-detail state"
                if(provider.get_annotation_count()!=1)throw "Ready image missing native attribution"
                closing=true;scene.set_tile_provider(null);provider.close()
            }
            if(closing && drained){
                if(provider.get_held_count()!=0 || provider.get_worker_count()!=0 || released.len()!=requests.len())throw "Native ownership did not drain"
                if(provider.complete_tile(requests.top(),pixels))throw "Closed completion accepted"
                if(provider.get_annotation_count()!=0 || annotationReleases.len()!=1 || provider.dup_demand().len()!=0)throw "Native attribution/demand did not drain"
                done=true;window.destroy();app.quit();return false
            }
        }catch(e){failure=e;app.quit();return false}
        return true
    })
    sqgi.timeout_add(20000,function(){failure="Native provider GI probe timed out";app.quit();return false})
})
app.run(0,null)
if(failure!=null)throw failure
if(!done)throw "Native probe did not finish"
print("ok 1 - GTK "+major+" native scene provider: known typelib, callbacks, footprint/revision, annotated coordinates, invalid/duplicate IDs, retry, GPU readiness and pixel/annotation drain\n")
