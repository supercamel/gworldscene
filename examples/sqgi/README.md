# SQGI Examples

These examples use `sqgi`, the Squirrel runtime bridge for GObject
Introspection, to drive the GWorldScene typelib directly.

After installing the library, run the GTK 3 or GTK 4 examples directly with
`sqgi`:

```sh
sudo meson install -C builddir
cd examples/sqgi
sqgi introspection-gtk3.nut
sqgi simple-scene-gtk3.nut
sqgi introspection-gtk4.nut
sqgi simple-scene-gtk4.nut
```

`introspection.nut` and `simple-scene.nut` default to GTK 4. The explicit
`*-gtk3.nut` and `*-gtk4.nut` launchers select the matching Gtk and
GWorldScene introspection namespaces.
