#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define CONFIG_PATH "/data/adb/modules/ring_mapper/config.txt" /* default; override with argv[1] */

int SCREEN_W = 1080, SCREEN_H = 2400; /* fallback if detection fails */

typedef struct {
    char name[64], up[32], down[32], left[32], right[32];
    int up_mult, down_mult, left_mult, right_mult;
    int swipe_duration_ms;
} config_t;

int load_config(config_t *cfg, const char *path) {
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->name, "JX-11");
    strcpy(cfg->up, "none"); strcpy(cfg->down, "none");
    strcpy(cfg->left, "none"); strcpy(cfg->right, "none");
    cfg->up_mult = cfg->down_mult = cfg->left_mult = cfg->right_mult = 1;
    cfg->swipe_duration_ms = 200;

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;
        if (!strcmp(key, "name")) strncpy(cfg->name, val, sizeof(cfg->name)-1);
        else if (!strcmp(key, "up")) strncpy(cfg->up, val, sizeof(cfg->up)-1);
        else if (!strcmp(key, "down")) strncpy(cfg->down, val, sizeof(cfg->down)-1);
        else if (!strcmp(key, "left")) strncpy(cfg->left, val, sizeof(cfg->left)-1);
        else if (!strcmp(key, "right")) strncpy(cfg->right, val, sizeof(cfg->right)-1);
        else if (!strcmp(key, "up_mult")) cfg->up_mult = atoi(val);
        else if (!strcmp(key, "down_mult")) cfg->down_mult = atoi(val);
        else if (!strcmp(key, "left_mult")) cfg->left_mult = atoi(val);
        else if (!strcmp(key, "right_mult")) cfg->right_mult = atoi(val);
        else if (!strcmp(key, "swipe_duration_ms")) cfg->swipe_duration_ms = atoi(val);
    }
    fclose(f);
    return 0;
}

/* Scan /dev/input/event* and match the device name via EVIOCGNAME.
   Works as root and as the adb/Shizuku shell user (input group), unlike
   /proc/bus/input/devices, which shell may not read on newer Android. */
int find_device(const char *name, char *out_path, size_t out_len) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int rc = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64], devname[256] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        int ok = ioctl(fd, EVIOCGNAME(sizeof(devname) - 1), devname) >= 0;
        close(fd);
        if (ok && !strcmp(devname, name)) {
            snprintf(out_path, out_len, "%s", path);
            rc = 0;
            break;
        }
    }
    closedir(d);
    return rc;
}

void detect_screen_size(void) {
    FILE *fp = popen("wm size", "r");
    if (!fp) return;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        int w, h;
        if (sscanf(line, "Physical size: %dx%d", &w, &h) == 2) {
            SCREEN_W = w; SCREEN_H = h;
        }
    }
    pclose(fp);
}

/* returns 1 if currently landscape, 0 if portrait */
int is_landscape(void) {
    /* "displays" section only (small, fast); no grep -m1, so dumpsys never hits a
       closed pipe and never prints "Broken pipe" into our log */
    FILE *fp = popen("dumpsys window displays 2>/dev/null | grep mCurrentRotation", "r");
    if (!fp) return 0;
    char line[128]; int landscape = 0;
    if (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "90") || strstr(line, "270")) landscape = 1;
    }
    pclose(fp);
    return landscape;
}

int needs_pointer(config_t *cfg) {
    const char *vals[4] = {cfg->up, cfg->down, cfg->left, cfg->right};
    for (int i = 0; i < 4; i++)
        if (!strcmp(vals[i], "scroll_up") || !strcmp(vals[i], "scroll_down")) return 1;
    return 0;
}

int setup_uinput(void) {
    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (ui < 0) return -1;
    ioctl(ui, UI_SET_EVBIT, EV_KEY);
    ioctl(ui, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(ui, UI_SET_EVBIT, EV_REL);
    ioctl(ui, UI_SET_RELBIT, REL_X);
    ioctl(ui, UI_SET_RELBIT, REL_Y);
    ioctl(ui, UI_SET_RELBIT, REL_WHEEL);

    struct uinput_setup us; memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_USB; us.id.vendor = 0x1234; us.id.product = 0x5678; us.id.version = 1;
    strcpy(us.name, "ring-scroll-wheel");
    ioctl(ui, UI_DEV_SETUP, &us);
    ioctl(ui, UI_DEV_CREATE);
    return ui;
}

void emit(int ui, int type, int code, int value) {
    struct input_event ev = {0};
    ev.type = type; ev.code = code; ev.value = value;
    write(ui, &ev, sizeof(ev));
    struct input_event syn = {0};
    syn.type = EV_SYN; syn.code = SYN_REPORT;
    write(ui, &syn, sizeof(syn));
}

/* action name -> Linux key code for the virtual "ring-keys" device.
   Android maps these through Generic.kl to the listed KEYCODE_*.
   Only non-letter keys are declared, so Android does NOT treat the device
   as a hardware keyboard (the on-screen keyboard keeps working). */
static const struct { const char *name; int linux_key; } KEY_ACTIONS[] = {
    { "dpad_up",          KEY_UP },           /* DPAD_UP */
    { "dpad_down",        KEY_DOWN },         /* DPAD_DOWN */
    { "dpad_left",        KEY_LEFT },         /* DPAD_LEFT  - prev chapter/page in many readers */
    { "dpad_right",       KEY_RIGHT },        /* DPAD_RIGHT - next chapter/page in many readers */
    { "page_prev",        KEY_LEFT },         /* legacy alias of dpad_left */
    { "page_next",        KEY_RIGHT },        /* legacy alias of dpad_right */
    { "page_up",          KEY_PAGEUP },       /* PAGE_UP */
    { "page_down",        KEY_PAGEDOWN },     /* PAGE_DOWN */
    { "volume_up",        KEY_VOLUMEUP },     /* page turn in Moon+/Librera/ReadEra etc. */
    { "volume_down",      KEY_VOLUMEDOWN },
    { "media_prev",       KEY_PREVIOUSSONG }, /* MEDIA_PREVIOUS - audiobook prev chapter */
    { "media_next",       KEY_NEXTSONG },     /* MEDIA_NEXT     - audiobook next chapter */
    { "media_play_pause", KEY_PLAYPAUSE },
    { "space",            KEY_SPACE },
    { "enter",            KEY_ENTER },
    { "back",             KEY_BACK },
    { NULL, 0 }
};

static int linux_key_for(const char *action) {
    for (int i = 0; KEY_ACTIONS[i].name; i++)
        if (!strcmp(action, KEY_ACTIONS[i].name)) return KEY_ACTIONS[i].linux_key;
    return 0;
}

int needs_keys(config_t *cfg) {
    const char *vals[4] = {cfg->up, cfg->down, cfg->left, cfg->right};
    for (int i = 0; i < 4; i++) if (linux_key_for(vals[i])) return 1;
    return 0;
}

int setup_uinput_keys(void) {
    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (ui < 0) return -1;
    ioctl(ui, UI_SET_EVBIT, EV_KEY);
    for (int i = 0; KEY_ACTIONS[i].name; i++) ioctl(ui, UI_SET_KEYBIT, KEY_ACTIONS[i].linux_key);

    struct uinput_setup us; memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_VIRTUAL; us.id.vendor = 0x1234; us.id.product = 0x5679; us.id.version = 1;
    strcpy(us.name, "ring-keys");
    ioctl(ui, UI_DEV_SETUP, &us);
    ioctl(ui, UI_DEV_CREATE);
    return ui;
}

/* custom Android keycodes have no fixed Linux key, so they still use `input` */
static void send_android_keycode(int keycode) {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "input keyevent %d", keycode);
    system(cmd);
}

static void push_cursor_right(int ui, int steps);

void do_action(int ui_wheel, int ui_keys, const char *action) {
    int wheel = !strcmp(action, "scroll_up") ? 1 : !strcmp(action, "scroll_down") ? -1 : 0;
    if (wheel) {
        if (ui_wheel < 0) return;
        push_cursor_right(ui_wheel, 6);          /* keep it parked, e.g. after a rotation */
        emit(ui_wheel, EV_REL, REL_WHEEL, wheel);
        return;
    }

    /* custom keycode: "key:<android keycode>", e.g. key:93 */
    if (!strncmp(action, "key:", 4)) {
        int kc = atoi(action + 4);
        if (kc > 0 && kc < 1000) send_android_keycode(kc);
        return;
    }

    int k = linux_key_for(action);
    if (k && ui_keys >= 0) {
        emit(ui_keys, EV_KEY, k, 1);
        emit(ui_keys, EV_KEY, k, 0);
    }
}

void do_touch_swipe(const char *direction, int amount, int duration_ms) {
    if (!direction) return;
    int w = SCREEN_W, h = SCREEN_H;
    if (is_landscape()) { int t = w; w = h; h = t; }

    int cx = w / 2, cy = h / 2;
    /* swipe length as a % of the relevant dimension, so it scales with orientation */
    int delta = (amount * h) / 12; /* ~8% of screen height per multiplier unit, vertical */
    int deltax = (amount * w) / 12;
    int x0 = cx, y0 = cy, x1 = cx, y1 = cy;

    if (!strcmp(direction, "up"))         { y0 = cy + delta / 2; y1 = cy - delta / 2; }
    else if (!strcmp(direction, "down"))  { y0 = cy - delta / 2; y1 = cy + delta / 2; }
    else if (!strcmp(direction, "left"))  { x0 = cx + deltax / 2; x1 = cx - deltax / 2; }
    else if (!strcmp(direction, "right")) { x0 = cx - deltax / 2; x1 = cx + deltax / 2; }

    if (y0 < 0) y0 = 0; if (y0 > h) y0 = h;
    if (y1 < 0) y1 = 0; if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0; if (x0 > w) x0 = w;
    if (x1 < 0) x1 = 0; if (x1 > w) x1 = w;

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "input touchscreen swipe %d %d %d %d %d", x0, y0, x1, y1, duration_ms);
    system(cmd);
}

/* ---- "alt_swipe": native touch swipe through a virtual touchscreen ----
   Same gesture as "swipe", but injected by our own uinput touchscreen instead of
   `input touchscreen swipe` (which starts a Java VM each time, slow on old phones). */

int needs_touch(config_t *cfg) {
    const char *vals[4] = {cfg->up, cfg->down, cfg->left, cfg->right};
    for (int i = 0; i < 4; i++)
        if (!strcmp(vals[i], "alt_swipe") || !strcmp(vals[i], "ring_swipe")) return 1;
    return 0;
}

/* Legacy uinput_user_dev setup (works on old kernels too). The axes cover the
   panel in its natural orientation, like a real built-in touchscreen; Android
   rotates the coordinates itself, so we feed it natural-orientation values. */
int setup_uinput_touch(void) {
    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (ui < 0) return -1;
    ioctl(ui, UI_SET_EVBIT, EV_KEY);
    ioctl(ui, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(ui, UI_SET_KEYBIT, BTN_TOOL_FINGER);
    ioctl(ui, UI_SET_EVBIT, EV_ABS);
    ioctl(ui, UI_SET_ABSBIT, ABS_X);
    ioctl(ui, UI_SET_ABSBIT, ABS_Y);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(ui, UI_SET_PROPBIT, INPUT_PROP_DIRECT); /* touchscreen, not touchpad */

    struct uinput_user_dev ud; memset(&ud, 0, sizeof(ud));
    strcpy(ud.name, "ring-touch");
    ud.id.bustype = BUS_VIRTUAL; ud.id.vendor = 0x1234; ud.id.product = 0x567a; ud.id.version = 1;
    ud.absmax[ABS_X] = ud.absmax[ABS_MT_POSITION_X] = SCREEN_W - 1;
    ud.absmax[ABS_Y] = ud.absmax[ABS_MT_POSITION_Y] = SCREEN_H - 1;
    ud.absmax[ABS_MT_SLOT] = 0;
    ud.absmax[ABS_MT_TRACKING_ID] = 65535;
    if (write(ui, &ud, sizeof(ud)) != (ssize_t)sizeof(ud) || ioctl(ui, UI_DEV_CREATE) < 0) { close(ui); return -1; }
    usleep(300000);                              /* let Android register the device */
    return ui;
}

/* Display rotation 0..3. Started with popen at finger-down so dumpsys runs while
   the finger is still on the ring; read (and drained) at lift. */
#define ROT_CMD "dumpsys window displays 2>/dev/null | grep -E 'mCurrentRotation=|mRotation=[0-3]'"
static FILE *g_rot_fp = NULL;

static void rotation_prefetch(void) { if (!g_rot_fp) g_rot_fp = popen(ROT_CMD, "r"); }

static int rotation_read(void) {
    FILE *fp = g_rot_fp ? g_rot_fp : popen(ROT_CMD, "r");
    g_rot_fp = NULL;
    if (!fp) return 0;
    char line[160]; int rot = -1;
    while (fgets(line, sizeof(line), fp)) {      /* read all: dumpsys never hits a closed pipe */
        if (rot >= 0) continue;
        char *p;
        if ((p = strstr(line, "ROTATION_"))) {   /* Android 10+: mCurrentRotation=ROTATION_90 */
            int deg = atoi(p + 9);
            rot = deg == 90 ? 1 : deg == 180 ? 2 : deg == 270 ? 3 : 0;
        } else if ((p = strstr(line, "mRotation=")) && p[10] >= '0' && p[10] <= '3') {
            rot = p[10] - '0';                   /* older: mRotation=1 */
        }
    }
    pclose(fp);
    return rot < 0 ? 0 : rot;
}

static void rotation_drop(void) { if (g_rot_fp) rotation_read(); }

/* display coords (current rotation) -> raw panel coords (natural orientation) */
static void display_to_raw(int rot, int dx, int dy, int *rx, int *ry) {
    int W = SCREEN_W - 1, H = SCREEN_H - 1;
    switch (rot) {
    case 1:  *rx = W - dy; *ry = dx;     break;
    case 2:  *rx = W - dx; *ry = H - dy; break;
    case 3:  *rx = dy;     *ry = H - dx; break;
    default: *rx = dx;     *ry = dy;     break;
    }
}

static void ev_raw(int ui, int type, int code, int value) {
    struct input_event ev = {0};
    ev.type = type; ev.code = code; ev.value = value;
    write(ui, &ev, sizeof(ev));
}

void do_alt_swipe(int ui, int rot, const char *direction, int amount, int duration_ms) {
    if (!direction || ui < 0) return;
    int w = SCREEN_W, h = SCREEN_H;
    if (rot & 1) { int t = w; w = h; h = t; }

    /* same geometry as do_touch_swipe */
    int cx = w / 2, cy = h / 2;
    int delta = (amount * h) / 12, deltax = (amount * w) / 12;
    int x0 = cx, y0 = cy, x1 = cx, y1 = cy;
    if (!strcmp(direction, "up"))         { y0 = cy + delta / 2; y1 = cy - delta / 2; }
    else if (!strcmp(direction, "down"))  { y0 = cy - delta / 2; y1 = cy + delta / 2; }
    else if (!strcmp(direction, "left"))  { x0 = cx + deltax / 2; x1 = cx - deltax / 2; }
    else if (!strcmp(direction, "right")) { x0 = cx - deltax / 2; x1 = cx + deltax / 2; }
    #define CLAMP(v, hi) ((v) < 0 ? 0 : (v) > (hi) ? (hi) : (v))
    x0 = CLAMP(x0, w - 1); x1 = CLAMP(x1, w - 1);
    y0 = CLAMP(y0, h - 1); y1 = CLAMP(y1, h - 1);

    if (duration_ms < 10) duration_ms = 10;
    int steps = duration_ms / 8; if (steps < 2) steps = 2;
    static int tid = 0; tid = (tid + 1) & 0xffff;

    for (int i = 0; i <= steps; i++) {
        int rx, ry;
        display_to_raw(rot, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, &rx, &ry);
        /* The kernel drops an ABS value equal to the previous one (also across
           gestures), and Android then reads the missing axis as 0, i.e. the screen
           edge. So never repeat the last value: shift it by 1 px instead. */
        static int last_rx = -1, last_ry = -1;
        if (rx == last_rx) rx += rx < SCREEN_W - 1 ? 1 : -1;
        if (ry == last_ry) ry += ry < SCREEN_H - 1 ? 1 : -1;
        last_rx = rx; last_ry = ry;
        if (i == 0) {
            ev_raw(ui, EV_ABS, ABS_MT_SLOT, 0);
            ev_raw(ui, EV_ABS, ABS_MT_TRACKING_ID, tid);
        }
        ev_raw(ui, EV_ABS, ABS_MT_POSITION_X, rx);
        ev_raw(ui, EV_ABS, ABS_MT_POSITION_Y, ry);
        ev_raw(ui, EV_ABS, ABS_X, rx);
        ev_raw(ui, EV_ABS, ABS_Y, ry);
        if (i == 0) {
            ev_raw(ui, EV_KEY, BTN_TOUCH, 1);
            ev_raw(ui, EV_KEY, BTN_TOOL_FINGER, 1);
        }
        ev_raw(ui, EV_SYN, SYN_REPORT, 0);
        if (i < steps) usleep(duration_ms * 1000 / steps);
    }
    ev_raw(ui, EV_ABS, ABS_MT_TRACKING_ID, -1);
    ev_raw(ui, EV_KEY, BTN_TOUCH, 0);
    ev_raw(ui, EV_KEY, BTN_TOOL_FINGER, 0);
    ev_raw(ui, EV_SYN, SYN_REPORT, 0);
}

/* ---- "ring_swipe": a quick flick like the ring's own gesture ----
   The JX-11 (itself a tiny touchscreen) sends a short accelerating flick: slow
   at first, fast at the end, ~50 ms. Replaying its exact pattern (creep, then
   one big jump) was erratic once scaled down: Android's fling-velocity fit
   swings wildly on a single jump. So this keeps the idea, an accelerating flick,
   as a smooth curve (distance ~ t^2) sampled every 8 ms, lifted right after
   the last sample. Measured: same result every time, and more distance or a
   lower speed value always means a longer fling.
   Length: multiplier x 1/24 of the long screen side (75% of that in landscape).
   Speed setting: the flick's duration.
   Direction as with the bare ring: the finger on screen moves the way it
   moved on the ring. */
static long now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

void do_ring_swipe(int ui, int rot, const char *direction, int amount, int duration_ms) {
    if (!direction || ui < 0) return;
    int w = SCREEN_W, h = SCREEN_H;
    if (rot & 1) { int t = w; w = h; h = t; }

    /* Length from the long screen side, so it doesn't depend on orientation,
       then 75% in landscape: the visible page is much shorter there, so the
       same scroll would feel too far. One setting then suits both. */
    int cx = w / 2, cy = h / 2;
    int delta = (amount * (SCREEN_W > SCREEN_H ? SCREEN_W : SCREEN_H)) / 24;
    if (rot & 1) delta = delta * 3 / 4;
    int deltax = delta;
    int x0 = cx, y0 = cy, x1 = cx, y1 = cy;
    if (!strcmp(direction, "up"))         { y0 = cy - delta / 2; y1 = cy + delta / 2; }
    else if (!strcmp(direction, "down"))  { y0 = cy + delta / 2; y1 = cy - delta / 2; }
    else if (!strcmp(direction, "left"))  { x0 = cx - deltax / 2; x1 = cx + deltax / 2; }
    else if (!strcmp(direction, "right")) { x0 = cx + deltax / 2; x1 = cx - deltax / 2; }
    x0 = CLAMP(x0, w - 1); x1 = CLAMP(x1, w - 1);
    y0 = CLAMP(y0, h - 1); y1 = CLAMP(y1, h - 1);

    if (duration_ms < 16) duration_ms = 16;
    int n = duration_ms / 8 + 1;                 /* one sample per 8 ms (4 or 2 ms measured no steadier) */
    static int tid = 0; tid = (tid + 1) & 0xffff;
    long t0 = now_us();

    for (int i = 0; i < n; i++) {
        long at = t0 + (long)i * duration_ms * 1000 / (n - 1);
        long wait = at - now_us();
        if (wait > 0) usleep(wait);              /* fixed schedule: no drift */
        long f = (long)i * i * 1000 / ((long)(n - 1) * (n - 1));   /* permille, t^2 */
        int rx, ry;
        display_to_raw(rot, x0 + (int)((x1 - x0) * f / 1000), y0 + (int)((y1 - y0) * f / 1000), &rx, &ry);
        static int last_rx = -1, last_ry = -1;   /* see do_alt_swipe */
        if (rx == last_rx) rx += rx < SCREEN_W - 1 ? 1 : -1;
        if (ry == last_ry) ry += ry < SCREEN_H - 1 ? 1 : -1;
        last_rx = rx; last_ry = ry;
        if (i == 0) {
            ev_raw(ui, EV_ABS, ABS_MT_SLOT, 0);
            ev_raw(ui, EV_ABS, ABS_MT_TRACKING_ID, tid);
        }
        ev_raw(ui, EV_ABS, ABS_MT_POSITION_X, rx);
        ev_raw(ui, EV_ABS, ABS_MT_POSITION_Y, ry);
        ev_raw(ui, EV_ABS, ABS_X, rx);
        ev_raw(ui, EV_ABS, ABS_Y, ry);
        if (i == 0) {
            ev_raw(ui, EV_KEY, BTN_TOUCH, 1);
            ev_raw(ui, EV_KEY, BTN_TOOL_FINGER, 1);
        }
        ev_raw(ui, EV_SYN, SYN_REPORT, 0);
    }
    usleep(2000);                                /* lift right away: a pause reads as "stopped" */
    ev_raw(ui, EV_ABS, ABS_MT_TRACKING_ID, -1);
    ev_raw(ui, EV_KEY, BTN_TOUCH, 0);
    ev_raw(ui, EV_KEY, BTN_TOOL_FINGER, 0);
    ev_raw(ui, EV_SYN, SYN_REPORT, 0);
}

/* Push the (unavoidable) pointer against the right screen edge.
   The arrow is drawn right of its hotspot, so at the right edge it is almost
   entirely off screen, while the hotspot stays over the app (the bottom edge
   would put it over the nav bar and scrolling would stop working).
   Android scales mouse deltas by the pointer speed setting (down to ~1/8 on
   older versions), so push far in several steps; it clamps at the edge. */
static void push_cursor_right(int ui, int steps) {
    for (int i = 0; i < steps; i++) emit(ui, EV_REL, REL_X, 500);
}

/* the short wait lets Android register the new uinput device first */
void park_cursor(int ui) {
    usleep(300000);
    push_cursor_right(ui, 40);
}

static void destroy_uinput(int *ui) {
    if (*ui >= 0) { ioctl(*ui, UI_DEV_DESTROY, 0); close(*ui); *ui = -1; }
}

static volatile sig_atomic_t g_reload = 0, g_quit = 0;
static void on_signal(int sig) { if (sig == SIGHUP) g_reload = 1; else g_quit = 1; }

/* Single instance across root and shell: abstract unix socket, released
   automatically by the kernel when the process dies. */
static int take_instance_lock(void) {
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return -1;
    struct sockaddr_un a; memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    const char *nm = "ring_mapper.lock";
    memcpy(a.sun_path + 1, nm, strlen(nm));      /* sun_path[0] = 0 -> abstract namespace */
    if (bind(s, (struct sockaddr *)&a, sizeof(sa_family_t) + 1 + strlen(nm)) < 0) { close(s); return -1; }
    return s;
}

/* Block (0% CPU) until something changes in /dev/input, or a signal arrives. */
static void wait_for_input_change(void) {
    int in = inotify_init1(IN_CLOEXEC);
    if (in < 0) { sleep(5); return; }
    if (inotify_add_watch(in, "/dev/input", IN_CREATE | IN_ATTRIB) < 0) { close(in); sleep(5); return; }
    char buf[512];
    read(in, buf, sizeof(buf));                  /* returns on event or EINTR */
    close(in);
    usleep(300000);                              /* let ueventd set owner + SELinux label */
}

static void list_devices(void) {
    DIR *d = opendir("/dev/input");
    if (!d) { perror("/dev/input"); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64], nm[256] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGNAME(sizeof(nm) - 1), nm) >= 0) printf("%s\t%s\n", path, nm);
        close(fd);
    }
    closedir(d);
}

static void usage(void) {
    fprintf(stderr,
        "usage: ring_mapper [-d] [-l logfile] [config]\n"
        "       ring_mapper --list          list input devices (path<TAB>name)\n"
        "  -d   detach: run in background in its own session\n"
        "  signals: SIGHUP = reload config, SIGTERM = quit\n"
        "  exit codes: 1 config unreadable, 4 already running\n");
}

int main(int argc, char **argv) {
    const char *config_path = CONFIG_PATH, *log_path = NULL;
    int detach = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) { list_devices(); return 0; }
        else if (!strcmp(argv[i], "-d")) detach = 1;
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) log_path = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else config_path = argv[i];
    }

    config_t cfg;
    if (load_config(&cfg, config_path) < 0) { fprintf(stderr, "ring_mapper: cannot read config %s\n", config_path); return 1; }

    int lock = take_instance_lock();
    if (lock < 0) { fprintf(stderr, "ring_mapper: already running\n"); return 4; }

    if (detach) {
        pid_t pid = fork();
        if (pid < 0) return 5;
        if (pid > 0) _exit(0);                   /* caller returns immediately */
        setsid();
        signal(SIGHUP, SIG_IGN);
        if (fork() > 0) _exit(0);                /* not a session leader -> no controlling tty */
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) {
            dup2(nul, 0);
            if (!log_path) { dup2(nul, 1); dup2(nul, 2); }
            if (nul > 2) close(nul);
        }
        chdir("/");
    }
    if (log_path) {
        int lf = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (lf >= 0) { dup2(lf, 1); dup2(lf, 2); close(lf); }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* no SA_RESTART: a blocking read() returns EINTR so we can reload/quit */
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    detect_screen_size();
    fprintf(stderr, "ring_mapper: started (pid %d, uid %d, config %s)\n", getpid(), getuid(), config_path);

    int ui_wheel = -1, ui_keys = -1, ui_touch = -1, last_rot = -1;
    int need_setup = 1, waiting_logged = 0;

    while (!g_quit) {
        if (g_reload) {
            g_reload = 0;
            if (load_config(&cfg, config_path) < 0) fprintf(stderr, "ring_mapper: reload failed, keeping old config\n");
            else fprintf(stderr, "ring_mapper: config reloaded\n");
            destroy_uinput(&ui_wheel);
            destroy_uinput(&ui_keys);
            destroy_uinput(&ui_touch);
            need_setup = 1;
        }
        if (need_setup) {
            if (needs_pointer(&cfg)) {
                ui_wheel = setup_uinput();
                if (ui_wheel >= 0) park_cursor(ui_wheel);
                else fprintf(stderr, "ring_mapper: cannot create scroll device: %s\n", strerror(errno));
            }
            if (needs_keys(&cfg)) {
                ui_keys = setup_uinput_keys();
                if (ui_keys < 0) fprintf(stderr, "ring_mapper: cannot create key device: %s\n", strerror(errno));
            }
            if (needs_touch(&cfg)) {
                ui_touch = setup_uinput_touch();
                if (ui_touch < 0) fprintf(stderr, "ring_mapper: cannot create touch device: %s\n", strerror(errno));
            }
            need_setup = 0;
        }

        char dev_path[64];
        if (find_device(cfg.name, dev_path, sizeof(dev_path)) < 0) {
            if (!waiting_logged) { fprintf(stderr, "ring_mapper: waiting for \"%s\"\n", cfg.name); waiting_logged = 1; }
            wait_for_input_change();
            continue;
        }
        waiting_logged = 0;

        int fd = open(dev_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            fprintf(stderr, "ring_mapper: cannot open %s: %s\n", dev_path, strerror(errno));
            wait_for_input_change();
            continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0)
            fprintf(stderr, "ring_mapper: warning: cannot grab %s: %s\n", dev_path, strerror(errno));
        fprintf(stderr, "ring_mapper: connected %s\n", dev_path);

        int start_x = -1, last_x = -1, start_y = -1, last_y = -1;
        struct input_event ev;
        for (;;) {
            ssize_t n = read(fd, &ev, sizeof(ev));
            if (n != (ssize_t)sizeof(ev)) {
                if (n < 0 && errno == EINTR && !g_quit && !g_reload) continue;
                break;                           /* ENODEV = ring gone, or reload/quit */
            }
            if (ui_touch >= 0 && (ev.type == EV_ABS || (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value)))
                rotation_prefetch();             /* finger down: look up rotation meanwhile */
            if (ev.type == EV_ABS && (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X)) {
                last_x = ev.value; if (start_x < 0) start_x = last_x;
            } else if (ev.type == EV_ABS && (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y)) {
                last_y = ev.value; if (start_y < 0) start_y = last_y;
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0) {
                int dx = (start_x >= 0) ? last_x - start_x : 0;
                int dy = (start_y >= 0) ? last_y - start_y : 0;
                const char *action = NULL, *direction = NULL; int mult = 1;

                if (abs(dy) > abs(dx) && abs(dy) > 30) {
                    if (dy > 0) { action = cfg.up; mult = cfg.up_mult; direction = "up"; }
                    else        { action = cfg.down; mult = cfg.down_mult; direction = "down"; }
                } else if (abs(dx) > 30) {
                    if (dx > 0) { action = cfg.left; mult = cfg.left_mult; direction = "left"; }
                    else        { action = cfg.right; mult = cfg.right_mult; direction = "right"; }
                }
                if (action) {
                    if (!strcmp(action, "swipe")) {
                        do_touch_swipe(direction, mult, cfg.swipe_duration_ms);
                    } else if (!strcmp(action, "alt_swipe")) {
                        int rot = rotation_read();
                        if (rot != last_rot) { fprintf(stderr, "ring_mapper: alt swipe, rotation %d\n", rot * 90); last_rot = rot; }
                        do_alt_swipe(ui_touch, rot, direction, mult, cfg.swipe_duration_ms);
                    } else if (!strcmp(action, "ring_swipe")) {
                        int rot = rotation_read();
                        if (rot != last_rot) { fprintf(stderr, "ring_mapper: ring swipe, rotation %d\n", rot * 90); last_rot = rot; }
                        do_ring_swipe(ui_touch, rot, direction, mult, cfg.swipe_duration_ms);
                    } else {
                        for (int i = 0; i < mult; i++) do_action(ui_wheel, ui_keys, action);
                    }
                }
                rotation_drop();                 /* no-op if alt_swipe already read it */
                start_x = last_x = start_y = last_y = -1;
            }
        }
        rotation_drop();
        ioctl(fd, EVIOCGRAB, 0);
        close(fd);
        if (!g_quit && !g_reload) fprintf(stderr, "ring_mapper: disconnected, waiting\n");
    }

    destroy_uinput(&ui_wheel);
    destroy_uinput(&ui_keys);
    destroy_uinput(&ui_touch);
    close(lock);
    fprintf(stderr, "ring_mapper: stopped\n");
    return 0;
}
