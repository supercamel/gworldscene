# SQGI Examples

These examples use `sqgi`, the Squirrel runtime bridge for GObject
Introspection, to drive the GWorldScene typelib directly.

After installing the library, run directly with `sqgi`:

```sh
sudo meson install -C builddir
cd examples/sqgi
sqgi introspection.nut
sqgi simple-scene.nut
```
