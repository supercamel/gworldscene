# Bounded asynchronous sensor capture

Continuous sensor clients should request a calibrated scaled capture and poll
completion in later event-loop turns. The renderer must use a pixel-pack buffer
and a zero-timeout GPU fence test, not synchronous CPU readback. One capture may
be pending per view. A second request while pending reports a busy error.
Successful poll transfers an immutable Frame; empty poll returns null. Dimensions,
pixels, timestamp and imagery annotations belong to the original request even
when view pose/projection changes before polling. Unrealize cancels the request
and releases its GPU resources. A mapped buffer is copied only after completion.

The synchronous capture APIs retain their existing behavior. Tests cover pending
request rejection, empty/completed polls, original timestamp and pixel dimensions,
subsequent capture reuse, and context/resource retirement. These operations run
on the GTK owning thread; asynchronous refers to GPU completion, not thread safety.

Calibrated sky rays use a double-precision inverse projection/rotation, converted
to a GPU direction matrix without translation. Sampling clip depth zero avoids
the far-plane homogeneous divide. This keeps sky directions finite when the near
plane is millimetres from the eye and the mesh origin is far away.
