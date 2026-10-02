# Cross-compilation installation paths

Install libraries, headers, pkg-config files, GIR/typelibs and Vala interfaces
under the project's Meson prefix/libdir/includedir/datadir. Do not derive project
installation paths from dependency pkg-config variables: in a cross SDK those
may contain a temporary absolute sysroot prefix.

SQTheia's aarch64 sqgipkg build exposed this with gobject-introspection's libdir.
The install plan must use `/usr/lib/aarch64-linux-gnu`, `/usr/include`, and
`/usr/share` when configured with prefix `/usr` and libdir `lib/aarch64-linux-gnu`.
Inspect with `meson introspect --installed BUILD_DIR`; no destination may contain
the build machine's SDK directory. This changes installation layout only.
