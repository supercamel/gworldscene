# Atlas upload pacing

A client trace on an NVIDIA RTX 3090 showed approximately 90 ms foreground
callbacks when uploading a 32 MiB chunk of a changing high-detail imagery atlas.
The upload limit is reduced while retaining staged, atomic atlas publication
and the existing mipmap/attribution behavior. This affects all scene clients;
it does not introduce application-specific payload or camera types.

Acceptance requires both responsive rendering and successful publication of
high-detail atlases during camera motion. A small chunk size can continually
cancel pending atlas uploads when the requested geographic range changes before
completion; higher FPS alone is insufficient evidence. Trace `tex_upload`,
`deferred`, `active_tex` and final publication, as well as frame time.

The initial 4 MiB experiment improved client pacing but published no high-detail
atlases during the test flight, so it was rejected. The final implementation uses
8 MiB chunks and background mipmap preparation. A 60-second moving-camera test
on the same desktop delivered 24.917 frames/s with a 42.26 ms p95 interval and
confirmed high-detail atlas publication. This measures delivery, not display
presentation, and does not establish embedded-device performance.

The 8 MiB experiment restored high-detail publication, but final whole-atlas
`glGenerateMipmap` still took approximately 80 ms. The implemented fix prepares
mips in the existing atlas worker and uploads at most 8 MiB per frame across all
levels. RGB averaging is performed in linear light, alpha linearly, with complete
coverage of odd-size edges. Source base pixels remain unchanged. Cancellation is
checked once per output row. Peak staging memory grows by approximately one third;
no partial atlas is exposed and rendering remains on the GPU.

Tests: `mipmaps` covers sRGB/alpha, constant-color round trips, odd edges and
cancellation. `imagery-gtk4`, `imagery-gtk4-gles`, `camera-capture-gtk4` and
`camera-capture-gtk4-gles` pass under Xvfb with `GTK_A11Y=none`. Tests on an inactive
desktop can miss GTK paint callbacks; these are run on a dedicated test display.

Camera range changes now leave an in-progress upload intact until publication.
The slot retains its own geographic range and annotations, so it remains correctly
registered even when the camera moves. A later scene update requests the newer
range. Provider/source invalidation and invalid ranges still clear obsolete
slots immediately. This prevents continuous range changes from starving a bounded
multi-frame upload. Moving-client qualification checks completed high-detail
uploads as well as frame pacing.
