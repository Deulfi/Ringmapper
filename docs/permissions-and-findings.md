# Permissions and tested findings

What was actually tested, on which phone, and what it showed. Anything not
tested is marked as such.

Test phones:

| Phone | Android | How Ring Mapper runs |
|---|---|---|
| Huawei/Honor BKL-L09 | 10 (EMUI) | Shizuku (adb shell user) |
| Xiaomi Mi 11 Lite 5G (renoir) | 16 (LineageOS 23.2, APatch) | root (`su`) |

Tests ran on 2026-11-26.

## What needs more than a normal app

The daemon needs two things that a normal app can't do:

1. **Open the ring's input device** (`/dev/input/eventN`). This is needed to
   read the ring's touches and to grab it.
2. **Open `/dev/uinput`** to create the virtual devices: the scroll wheel,
   the keys, and the touchscreen used by Alt swipe and Ring swipe.

The exclusive grab (`EVIOCGRAB`) is **not** a separate permission. It's an
`ioctl` on a device you have already opened. Whoever can open the device can
grab it.

### Test: the same probe in three contexts

A small native probe tries to open every `/dev/input/eventN`, grab it (and
release it right away), and create a device through `/dev/uinput`. It ran:

- as the adb shell user: `adb shell`, which is what Shizuku provides;
- as root: `su`, Mi only;
- as an ordinary app: a throwaway test APK that runs the probe from its own
  native library directory, as SELinux domain `untrusted_app`.

| Context | `/dev/input/eventN` open | grab | `/dev/uinput` create | Huawei | Mi |
|---|---|---|---|---|---|
| Ordinary app (`untrusted_app`) | Permission denied (every eventN, and listing the directory) | impossible (not opened) | Permission denied | ✔ | ✔ |
| adb shell (`shell`, Shizuku) | ok | ok | ok | ✔ | ✔ |
| root (`su`) | ok | ok | ok | not tested | ✔ |

Why the shell user can do this: the device nodes are
`root:input 0660 input_device` (`/dev/input/*`) and
`uhid:uhid 0660 uhid_device` (`/dev/uinput`), and the shell user is in both
groups: `input` (1004) and `uhid` (3011). Same on both phones. An app has
neither group.

### Test: Android's `input` command from an app

"Touch swipe" and custom keycodes (`key:N`) use Android's `input` command
instead of `/dev/uinput`. Run from the test app, it fails on both phones with
a `SecurityException`: injecting events needs the `INJECT_EVENTS`
permission. From `adb shell` it works. So those actions also need the shell
user or root.

### Grab is exclusive (Mi)

With the root daemon running, the probe as shell and as root got
`Device or resource busy` when grabbing the ring (`JX-11`). The other devices
grabbed fine. Only one process can hold the grab.

## Why the grab is needed

Android itself opens the ring as a touchscreen: on the Mi, `dumpsys input`
lists it under EventHub as `Classes: TOUCH | TOUCH_MT | EXTERNAL`. Without
the grab, Android would act on the ring's own touches as well as Ring Mapper.
That's the native behavior you see without Ring Mapper, when the ring
performs its own swipes on the screen.

**Not tested here:** a side-by-side check that apps receive the ring's touches
when the grab is released. In one attempt on the Mi (daemon stopped for a few
seconds), the ring didn't appear in Android's device list at all, probably
because the ring had just gone to sleep or was reconnecting. That attempt
proved nothing either way.

## The ring's own gesture (Mi)

Recorded with `getevent` with the daemon stopped (15 swipes). The ring sends
the same pattern every time, whether swiped fast or slow:

- Ring up: Y goes 350 → 530 in steps of 30 every ~10 ms, then jumps to
  1200 (outside its own 0–1000 range) and lifts. About 45 ms in total.
- Ring down: Y goes 620 → 470, then jumps to 0.
- It never sends X, so X stays at its last value (500, the middle).

## Things found by testing the virtual touchscreen

These are Alt swipe and Ring swipe, tested on both phones.

- **Repeated values are dropped.** The kernel drops an `ABS_*` value equal to
  the device's current one. Since Android clears a touch slot when the finger
  lifts, a coordinate missing from a new touch's first frame reads as 0, the
  screen edge. Seen as: sideways swipes going vertical, the status bar
  opening, and (with gesture navigation, Mi) a back gesture when Alt swipe
  and Ring swipe were mixed. Fixed by never repeating the last value sent on
  that device (±1 px). Checked with `getevent`: every touch's first frame has
  both X and Y, for mixed sequences in portrait and both landscape
  rotations. On the Mi the old code left Settings through the back gesture,
  and the fixed code stays.
- **Rotation.** Android rotates the virtual touchscreen like a built-in one,
  so coordinates are converted from screen to panel orientation. Checked
  with the pointer-location overlay in 0°, 90° and 270° on both phones.
- **Fling estimate.** Android's fling speed is fitted over the last touch
  points. Replaying the ring's "creep, then one big jump" at a sensible
  length was erratic: on the Huawei, a native list gave 867..1277 px for the
  same setting, sometimes in the wrong direction. A pause of more than 40 ms
  before lifting counts as a stopped finger, so there is no fling. Ring
  swipe therefore uses a smooth accelerating (t²) flick with a touch point
  every 8 ms and an immediate lift. It's consistent within a few percent per
  setting from speed 100 on, about ±12% at speed 50. Sending points every 4
  or 2 ms was not steadier.
- **Fast repeated swipes.** In Chrome, flings stack when the next swipe comes
  within about 100 ms (×1/50: 4 swipes ≈ 12,600 px instead of ≈ 3,500). In a
  native list there was only slight stacking. That's app behavior and
  happens with a finger too.
- **Speed of `input touchscreen swipe`** (Touch swipe): on the Huawei it took
  1.1 s for a 200 ms swipe, about 0.9 s of which is starting the command.
  Alt swipe and Ring swipe avoid that.
