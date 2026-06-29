Installation
============

Build dependencies
------------------

GWorldScene is built with Meson and Ninja. The library can build against GTK 3,
GTK 4, or both. It also depends on GObject Introspection, epoxy, GDAL, Assimp,
libsoup 3, GLM, zlib, and gdk-pixbuf.

On Debian or Ubuntu-style systems the package names are typically:

.. code-block:: sh

   sudo apt install meson ninja-build gcc g++ valac \
     libgtk-3-dev libgtk-4-dev libgirepository1.0-dev \
     libepoxy-dev libgdal-dev libassimp-dev libsoup-3.0-dev libglm-dev zlib1g-dev \
     libgdk-pixbuf-2.0-dev

Build and install
-----------------

.. code-block:: sh

   meson setup builddir
   ninja -C builddir
   meson test -C builddir --print-errorlogs
   sudo meson install -C builddir

The install layout intentionally follows the system GObject Introspection
directories. On aarch64 Linux, the installed files include:

.. code-block:: text

   /usr/lib/aarch64-linux-gnu/libgworldscene-core-0.1.so
   /usr/lib/aarch64-linux-gnu/libgworldscene-gtk3-0.1.so
   /usr/lib/aarch64-linux-gnu/libgworldscene-gtk4-0.1.so
   /usr/lib/aarch64-linux-gnu/girepository-1.0/GWorldSceneGtk3-0.1.typelib
   /usr/lib/aarch64-linux-gnu/girepository-1.0/GWorldSceneGtk4-0.1.typelib
   /usr/share/gir-1.0/GWorldSceneGtk3-0.1.gir
   /usr/share/gir-1.0/GWorldSceneGtk4-0.1.gir
   /usr/share/vala/vapi/GWorldSceneGtk3-0.1.vapi
   /usr/share/vala/vapi/GWorldSceneGtk4-0.1.vapi
   /usr/include/gworldscene-core-0.1/gworldscene/
   /usr/include/gworldscene-gtk3-0.1/gworldscene/
   /usr/include/gworldscene-gtk4-0.1/gworldscene/
   /usr/lib/aarch64-linux-gnu/pkgconfig/gworldscene-core-0.1.pc
   /usr/lib/aarch64-linux-gnu/pkgconfig/gworldscene-gtk3-0.1.pc
   /usr/lib/aarch64-linux-gnu/pkgconfig/gworldscene-gtk4-0.1.pc

Using the library from C
------------------------

Compile with pkg-config:

.. code-block:: sh

   cc app.c -o app $(pkg-config --cflags --libs gworldscene-gtk4-0.1)

Use the umbrella header:

.. code-block:: c

   #include <gworldscene/gworldscene.h>

Using the Vala binding
----------------------

After installation, compile against the generated VAPI:

.. code-block:: sh

   valac --pkg gtk4 --pkg GWorldSceneGtk4-0.1 app.vala

The Vala namespace is ``GWorld``.

Using SQGI
----------

SQGI loads the installed typelib through GObject Introspection:

.. code-block:: text

   local Gtk = import("Gtk", "4.0")
   local GWorldScene = import("GWorldSceneGtk4", "0.1")

The example scripts live in ``examples/sqgi``:

.. code-block:: sh

   cd examples/sqgi
   sqgi introspection-gtk3.nut
   sqgi simple-scene-gtk3.nut
   sqgi introspection-gtk4.nut
   sqgi simple-scene-gtk4.nut

Running against the build tree
------------------------------

When testing before installation, point GI and the dynamic linker at the build
outputs:

.. code-block:: sh

   GI_TYPELIB_PATH=builddir/src \
   LD_LIBRARY_PATH=builddir/src \
   sqgi examples/sqgi/introspection-gtk4.nut

Tiles, terrain, and cache
-------------------------

The widget has built-in defaults for terrain and map tiles. Applications can
override them at runtime:

.. code-block:: c

   gworld_scene_view_set_terrain_server(view, "https://example.com/terrain/data/");
   gworld_scene_view_set_map_tile_url_template(view, "https://server/{z}/{x}/{y}.png");

The map tile URL template uses slippy-map placeholders ``{z}``, ``{x}``, and
``{y}``. The terrain server accepts a base URL/directory or a template using
``{tile}`` for HGT-style names such as ``S35E147``.

Disk caching is enabled by default. Set a cache directory explicitly if the
default per-user cache location is not appropriate:

.. code-block:: c

   gworld_scene_view_set_cache_directory(view, "/tmp/gworldscene-cache");
   gworld_scene_view_set_cache_enabled(view, TRUE);
