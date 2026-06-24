/*
 * g-ir-scanner's parser does not understand glibc's AArch64 vector math
 * typedefs. Shadow the header during GIR generation only; the real build uses
 * the system header.
 */
#include <bits/libm-simd-decl-stubs.h>
