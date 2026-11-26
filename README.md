# Ring Mapper

Turns a cheap AliExpress Bluetooth "smart ring" clicker (JX-11 and look-alikes)
into a proper remote for Android: swipe on the ring to **scroll**, **turn pages**,
**skip chapters**, send media keys, or any key you like.

**No root needed.** Works with [Shizuku](https://shizuku.rikka.app/), or with root
if you have it.

<img width="240" height="240" alt="JX-11 ring" src="https://github.com/user-attachments/assets/48cfa59f-4ef1-4b41-a216-cbb417a7c481" />

## Why

Out of the box these rings behave badly. Scrolling on them throws you to the
end of the chapter or web page. They send raw touchpad coordinates instead of
key presses, so remapper apps like Key Mapper or Button Mapper can't see them.
Ring Mapper reads the ring directly, works out which way you swiped, and sends
the action you picked.

## Requirements

- Android 8.0 or newer, 64-bit ARM
- One of these:
  - **Shizuku**: no root. You start it with Wireless debugging (Android 11+) or once from a PC over adb.
  - **Root**: Magisk, APatch or KernelSU.
- The ring paired over Bluetooth.

## Install & set up

1. Install **Ring Mapper** (APK from [Releases](../../releases)).
2. Without root: install and start **Shizuku** (follow the Shizuku app's own instructions).
3. Open Ring Mapper:
   - **Privilege backend**: leave on *Auto*, or pick *Shizuku* or *Root*.
   - Shizuku only: tap **Grant Shizuku permission**.
   - **Ring device name**: tap **Pick…** and choose your ring (`JX-11` by default).
   - Choose an action for each swipe direction.
   - Tap **Apply & start**.
4. Done. You can close the app. A tiny background process keeps handling the
   ring, even when it disconnects and reconnects.

### After a reboot

The background process does not survive a reboot.

- **Root:** tick **Start at boot**.
- **Shizuku:** Shizuku itself has to be started again first. Then open Ring
  Mapper and tap **Apply & start**. If your Shizuku starts at boot, **Start at
  boot** works too.

## Actions

| Action | Typical use |
|---|---|
| Scroll up / down | Scroll web pages, feeds, readers |
| Touch swipe / Ring swipe / Alt swipe | Fake finger swipe, see [Swipes](#swipes) below |
| D-pad up / down / left / right | Next or previous page/chapter in many readers, TV-style navigation |
| Page up / down | Page-wise scrolling |
| Volume up / down | Page turning in readers that support volume keys (Moon+ Reader, Librera, ReadEra…) |
| Media previous / next / play-pause | Audiobooks, podcasts, music |
| Space, Enter, Back | Self-explanatory |
| Custom keycode | Any [Android keycode](https://developer.android.com/reference/android/view/KeyEvent#constants_1), e.g. `22` = D-pad right |

The **multiplier** repeats the action (for example 3 = three scroll steps per swipe).

### Swipes

There are three swipe actions because they feel different, and one is a
fallback. For all three, the **multiplier** sets the distance (negative =
reverse) and the **speed** field sets how long the swipe takes. Shorter means
a harder fling.

| Swipe | What it does | Cost |
|---|---|---|
| **Touch swipe** | A straight swipe at constant speed through Ring Mapper's own virtual touchscreen. Use this one first. | Cheap: no extra process per swipe |
| **Ring swipe** | A short, accelerating flick like the ring's own gesture, but with a sensible length. In landscape it's half as long, so one setting suits both orientations. | Cheap, same as Touch swipe |
| **Alt swipe** | The same straight swipe as Touch swipe, but sent with Android's `input` command, without a virtual touchscreen. A fallback if the virtual touchscreen misbehaves on your phone. | Expensive: starts a Java process per swipe, which delays each swipe (about 0.9 s on an older phone) |

Touch swipe and Alt swipe are 25% longer in landscape, so the same setting
feels similar in both orientations.

## Troubleshooting

| Problem | Fix |
|---|---|
| Ring not in the device list | Make sure it's connected over Bluetooth. Close the picker and open it again. |
| Scrolling stopped after visiting another screen | Scroll goes to the window under the (hidden) mouse pointer. Tap once into the app. |
| A mouse cursor shows up | Android always needs a pointer for scroll-wheel events. It gets parked at the screen edge. Use key actions if it bothers you. |
| "Another ring_mapper is already running" | The Magisk/APatch module (or an earlier start) is already running it. Stop that one first. |
| Nothing happens with Shizuku on your phone | Some vendor ROMs (Samsung, Xiaomi stock…) may block the shell user from input devices. Use root, and please open an issue. |

## Alternative: Magisk / APatch / KernelSU module

If you prefer a root module over the app, [`module/`](module) contains one. It
runs the same background process, starts at boot, and has a WebUI with the same
settings. It has no real advantage over the app on a rooted phone. To install,
zip the **contents** of `module/` (not the folder itself), or take the ZIP from
Releases, and install it in your root manager. Don't run the module and the
app at the same time: only one of them will get the ring.

## How it works

A small C program reads the ring's raw input, works out the swipe direction,
and sends the chosen action through its own virtual input devices. The app
just configures and starts it.

## Building from source

```
daemon/build.sh                          # needs the Android NDK (NDK=/path/to/ndk)
cd android && ./gradlew assembleRelease  # needs JDK 17 + Android SDK
```

## License

See [LICENSE](LICENSE).
