/* Xbox 360 backend (libxenon). Phase 6 — not implemented yet. This file only
 * documents the mapping so the core compiles against the same header:
 *   video  → xenos (Xe_CreateTexture with linear ARGB8, one sprite batch per frame)
 *   input  → usb/usbmain + input (controller 0)
 *   files  → libfat / libxenon mount (uda:/, sda:/)
 *   time   → mftb() ticks converted to ms
 * See notes/toolchain.md once written. */
#error "xenon platform backend is not implemented yet (Phase 6)"
