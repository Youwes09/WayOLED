// TODO: implement
// Planned: periodic sub-pixel image panning to prevent OLED burn-in from
// long-static UI elements, similar to what OLED TVs do automatically.
//
// Pros:
//   Addresses a burn-in cause dimming and pixel-refresh do not: content
//   that stays in the exact same physical position for a long time.
//
// Cons:
//   No standard Wayland protocol exposes output panning to clients.
//   Would require direct DRM/KMS atomic commits adjusting SRC_X/SRC_Y,
//   which needs a framebuffer larger than the visible mode. This likely
//   needs compositor cooperation and may not be achievable cleanly as a
//   client-side daemon feature at all.
//
// Decision: deferred until mango codebase is read.