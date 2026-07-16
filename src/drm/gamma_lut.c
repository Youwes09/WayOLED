// TODO: implement
// Planned: direct DRM gamma LUT control via /dev/dri/cardN, as a fallback
// for compositors that do not implement wlr-gamma-control-v1.
//
// Pros:
//   Works outside wlroots, including bare KMS sessions with no compositor
//   protocol support at all. Would let dimmer.c and colortemp.c keep
//   working on non-wlroots compositors like Mutter or KWin.
//
// Cons:
//   Needs libdrm and either root or membership in the video/render group.
//   Duplicates dimmer.c/colortemp.c logic at the ioctl level instead of
//   the protocol level, so the ramp-combining logic would need to be
//   shared or reimplemented here too.
//   Only useful as a fallback path when gamma_manager is null in
//   wayland_globals.c, so it should not be started unless that happens.
//
// Decision: deferred until sanity regained