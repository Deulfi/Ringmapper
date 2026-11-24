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

- Android 8.0 or newer, 64-bit ARM (practically every phone from the last years)
- One of these:
  - **Shizuku**: no root. You start it with Wireless debugging (Android 11+) or once from a PC over adb.
  - **Root**: Magisk, APatch or KernelSU. The app uses `su`.
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
4. Done. You can close the app. A tiny background process (about 0.5 MB RAM,
   0% CPU while idle) keeps handling the ring, even when it disconnects and
   reconnects.

### After a reboot

The background process does not survive a reboot.

- **Root:** tick **Start at boot**. It starts on its own about a minute after
  boot, once Android reports boot complete.
- **Shizuku:** Shizuku itself has to be started again first. Then open Ring
  Mapper and tap **Apply & start**. If your Shizuku starts at boot, **Start at
  boot** works too.

## Actions

| Action | Typical use |
|---|---|
| Scroll up / down | Scroll web pages, feeds, readers |
| Touch swipe | Fake finger swipe; the multiplier sets the distance (negative = reverse), the speed field sets the fling |
| D-pad up / down / left / right | Next or previous page/chapter in many readers, TV-style navigation |
| Page up / down | Page-wise scrolling |
| Volume up / down | Page turning in readers that support volume keys (Moon+ Reader, Librera, ReadEra…) |
| Media previous / next / play-pause | Audiobooks, podcasts, music |
| Space, Enter, Back | Self-explanatory |
| Custom keycode | Any [Android keycode](https://developer.android.com/reference/android/view/KeyEvent#constants_1), e.g. `22` = D-pad right |

The **multiplier** repeats the action (for example 3 = three scroll steps per swipe).

## Troubleshooting

| Problem | Fix |
|---|---|
| Ring not in the device list | Make sure it's connected over Bluetooth. Close the picker and open it again. |
| Scrolling stopped after visiting another screen | Scroll goes to the window under the (hidden) mouse pointer. Tap once into the app. |
| A mouse cursor shows up | Android always needs a pointer for scroll-wheel events. It gets parked at the screen edge. Use key actions if it bothers you. |
| "Another ring_mapper is already running" | The Magisk/APatch module (or an earlier start) is already running it. Stop that one first. |
| Nothing happens with Shizuku on your phone | Some vendor ROMs (Samsung, Xiaomi stock…) may block the shell user from input devices. Use root, and please open an issue. |

Tested on a Xiaomi Mi 11 Lite 5G with LineageOS 23.2 (Android 16), both with
Shizuku and with root.

## Alternative: Magisk / APatch / KernelSU module

If you prefer a root module over the app, [`module/`](module) contains one. It
runs the same background process, starts at boot, and has a WebUI with the same
settings. It has no real advantage over the app on a rooted phone. To install,
zip the **contents** of `module/` (not the folder itself), or take the ZIP from
Releases, and install it in your root manager. Don't run the module and the
app at the same time: only one of them will get the ring.

## How it works

A small C program (`daemon/ring_mapper.c`, 17 KB):

1. finds the ring by name among `/dev/input/event*` and grabs it exclusively,
   so Android no longer sees its broken gestures;
2. decodes each swipe's direction from the raw touch coordinates;
3. sends the chosen action through its own virtual input devices
   (`ring-keys` for keys, `ring-scroll-wheel` for scrolling). Touch swipes
   and custom keycodes use Android's `input` command.

It sleeps in the kernel while idle and waits for the ring with inotify, so it
uses no CPU. The app only copies the program to `/data/local/tmp/ringmapper/`,
writes the config, starts it, and then gets out of the way.

**Why no root is needed:** the shell user (which Shizuku and `adb shell` run
as) is allowed by stock Android to read input devices and create virtual ones
(`input`/`uhid` groups, standard SELinux policy). Only that user, not normal
apps, can do this, which is why remapper apps can't see the ring.

Command line, for tinkering over `adb shell`:

```
ring_mapper [-d] [-l logfile] [config]   # -d = run detached in the background
ring_mapper --list                       # list input devices
kill -HUP <pid>                          # reload the config
```

## Building from source

```
daemon/build.sh                 # NDK -> module/ring_mapper   (NDK=/path/to/ndk)
cd android && ./gradlew assembleDebug   # JDK 17 + Android SDK; bundles module/ring_mapper
```

The repo layout:
- `daemon/`: the C program;
- `android/`: the app;
- `module/`: the root module (with the prebuilt program);
- `docs/`: the icon in Lawnicons format.

## License

See [LICENSE](LICENSE).
