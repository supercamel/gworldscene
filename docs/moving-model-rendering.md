# GPU-resident moving models

Cache indexed model geometry in GPU buffers per loaded model asset and GL context.
Moving, rotating, scaling or recoloring instances must update draw uniforms;
it must not rebuild the terrain mesh or re-expand and upload model triangles.
The renderer must retain arbitrary model geometry, texture/material appearance,
nonuniform-scale normal correctness, world-relative depth, shadow participation,
CPU picking and framebuffer capture. Recreate buffers after GL context teardown.

Validate actual pose changes, not only repeated captures of a stationary scene.
Measure upload/rebuild counts alongside frame timings and compare pixel results
for pose, scale, material, shadows and capture occlusion. No application-specific
model names or camera-device semantics belong in this renderer.

`capture_frame_scaled` retains the sensor-resolution render and projection but
performs a centered crop and linear resize on the GPU before readback. The output
is an owned RGBA image with the same timestamp/attribution lifecycle as the
original capture API. Validate both dimensions, finite crop >=1 and at least one
remaining sensor pixel; retain/recreate both framebuffers with context lifetime.

Continuous pose/projection updates must not postpone tile or mesh work indefinitely.
Scene refresh scheduling retains the earliest pending deadline, while urgent changes
may bring it forward. Test tile readiness while submitting camera poses at 25 Hz.

Close camera/model geometry must remain stable even before an asynchronous
terrain mesh rebases after a large geographic camera relocation. Compose the
model-to-clip matrix in double precision on the CPU, then upload the combined
matrix; do not round large world translations to float before subtracting the
camera position. Use the model location's local NED frame for orientation,
consistent with camera pose and CPU picking. Keep the world transform for
lighting/shadows. Regression: a 2 cm model 5 cm from the lens must retain its
projected footprint and center when camera/model move together across continents,
without waiting for mesh rebuild callbacks, in GTK3 and GTK4 capture.
