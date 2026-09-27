# Ring Mapper

Root/Magisk/APatch module that remaps a cheap AliExpress BLE "smart ring" 
clicker (JX-11 and similar OEM variants) into proper Android input events — 
scroll wheel, D-pad, page-turn keys, or a synthetic touch swipe — instead of 
its default broken chapter-skip/gesture behavior.

## Why

Because when you scroll on those rings you land at the ass-end of the chapter or webpage.
These rings send raw touchpad-style swipe coordinates rather than key events, 
so remapper apps (Key Mapper, Button Mapper, etc.) can't see them at all. 
This module reads the ring's raw input device directly, decodes swipe 
direction, and re-emits a real, configurable action.

## Requirements

- Root via Magisk or APatch
- Your ring must show up in `getevent -l` (tested with JX-11; other 
  identically-designed AliExpress ring clickers likely share the same 
  protocol — check the device name matches or update it in the WebUI)

## Install

1. Download the ZIP from release (or build one yourself with the file from the repo)
2. Flash/install the ZIP directly through Magisk Manager or APatch's 
   module installer ("Install from storage")
3. Reboot

## Configure

Open the module's page in Magisk/APatch's WebUI (or tap it in the module 
list). You can set:
- **Device name** — must match the name shown for your ring in 
  `getevent -l` (default: `JX-11`)
- **Action per swipe direction** (up/down/left/right): scroll wheel, D-pad, 
  page-turn keys, chapter prev/next, a synthetic swipe, or none
- **Multiplier per direction** — repeat count for key-based actions, or 
  swipe distance/direction for the `swipe` action (negative values reverse 
  swipe direction)
- **Swipe speed/intensity (ms)** — affects fling behavior of the `swipe` 
  action; lower = faster/flingier, higher = slower/shorter

Hit **Apply** to save and restart the daemon with new settings.

## How it works

A small C binary finds the ring's input 
device by name, grabs it exclusively, decodes swipe 
direction/distance, and re-emits either:
- a virtual mouse device (`REL_WHEEL` for scroll, or cursor movement for 
  the `swipe` action — Android requires pointer capability for either, so 
  a cursor is unavoidable in both cases), or
- a standard `input keyevent`/`input touchscreen swipe` call for 
  D-pad/page/chapter actions

A supervising loop (`service.sh`) starts the binary at boot and relaunches 
it if the ring disconnects and reconnects later.

## Known limitations

- A cursor icon appears while `scroll_up`/`scroll_down`/`swipe` is active 
  on any direction — this is a hard Android input-classification 
  requirement, not a bug (the cursor should be moved to the left bottom
  corner to 'hide' it, but it may be still on the screen on big displays)
- `swipe` uses Android's own fling physics rather than raw distance-only
  motion, so travel distance also depends on the configured speed/intensity
- Rapid repeated swipes in quick succession may fire as separate, slightly 
  overlapping touch events rather than one merged motion
## Picture of a JX-11 ring
  <img width="360" height="360" alt="{C729AD66-909D-4715-BBBB-DB6F716C0E51}" src="https://github.com/user-attachments/assets/48cfa59f-4ef1-4b41-a216-cbb417a7c481" />
