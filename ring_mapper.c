#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <libgen.h>
#include <unistd.h>

char CONFIG_PATH[512];

void resolve_config_path(const char *argv0) {
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = 0;
        snprintf(CONFIG_PATH, sizeof(CONFIG_PATH), "%s/config.txt", dirname(exe_path));
    } else {
        strcpy(CONFIG_PATH, "config.txt"); /* fallback: relative to cwd */
    }
}

int SCREEN_W = 1080, SCREEN_H = 2400; /* fallback if detection fails */

typedef struct {
    char name[64], up[32], down[32], left[32], right[32];
    int up_mult, down_mult, left_mult, right_mult;
    int swipe_duration_ms;
} config_t;

int load_config(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->name, "JX-11");
    strcpy(cfg->up, "none"); strcpy(cfg->down, "none");
    strcpy(cfg->left, "none"); strcpy(cfg->right, "none");
    cfg->up_mult = cfg->down_mult = cfg->left_mult = cfg->right_mult = 1;
    cfg->swipe_duration_ms = 200;

    FILE *f = fopen(CONFIG_PATH, "r");
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

int find_device(const char *name, char *out_path, size_t out_len) {
    FILE *f = fopen("/proc/bus/input/devices", "r");
    if (!f) return -1;
    char line[512], search[128];
    int found = 0;
    snprintf(search, sizeof(search), "N: Name=\"%s\"", name);
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, search)) found = 1;
        if (found && strncmp(line, "H: Handlers=", 12) == 0) {
            char *ev = strstr(line, "event");
            if (ev) {
                int num; sscanf(ev, "event%d", &num);
                snprintf(out_path, out_len, "/dev/input/event%d", num);
                fclose(f);
                return 0;
            }
        }
        if (line[0] == '\n') found = 0;
    }
    fclose(f);
    return -1;
}

void detect_screen_size(void) {
    FILE *fp = popen("wm size", "r");
    if (!fp) return;
    char line[128], phys[64];
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
    FILE *fp = popen("dumpsys window | grep -m1 mCurrentRotation", "r");
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
    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
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

void do_action(int ui, const char *action) {
    if (!strcmp(action, "scroll_up")) emit(ui, EV_REL, REL_WHEEL, 1);
    else if (!strcmp(action, "scroll_down")) emit(ui, EV_REL, REL_WHEEL, -1);
    else if (!strcmp(action, "dpad_up")) system("input keyevent 19");
    else if (!strcmp(action, "dpad_down")) system("input keyevent 20");
    else if (!strcmp(action, "page_up")) system("input keyevent 92");
    else if (!strcmp(action, "page_down")) system("input keyevent 93");
    else if (!strcmp(action, "page_prev")) system("input keyevent 21");
    else if (!strcmp(action, "page_next")) system("input keyevent 22");
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

void schedule_cursor_park(int ui) {
    signal(SIGCHLD, SIG_IGN);
    pid_t pid = fork();
    if (pid == 0) {
        usleep(300000);
        emit(ui, EV_REL, REL_X, -999);
        emit(ui, EV_REL, REL_Y, 99);
        _exit(0);
    }
}

int main(void) {
    resolve_config_path(argv[0])
    config_t cfg;
    if (load_config(&cfg) < 0) { fprintf(stderr, "ring_mapper: cannot read config %s\n", CONFIG_PATH); return 1; }
    detect_screen_size();

    char dev_path[64];
    if (find_device(cfg.name, dev_path, sizeof(dev_path)) < 0) {
        fprintf(stderr, "ring_mapper: no input device found with name \"%s\"\n", cfg.name);
        return 2;
    }

    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "ring_mapper: cannot open %s: %s\n", dev_path, strerror(errno)); return 3; }
    ioctl(fd, EVIOCGRAB, 1);

    int ui_wheel = needs_pointer(&cfg) ? setup_uinput() : -1;
    if (ui_wheel >= 0) schedule_cursor_park(ui_wheel);

    int start_x = -1, last_x = -1, start_y = -1, last_y = -1;
    struct input_event ev;

    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
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
                } else {
                    for (int i = 0; i < mult; i++) do_action(ui_wheel, action);
                }
            }
            start_x = last_x = start_y = last_y = -1;
        }
    }

    ioctl(fd, EVIOCGRAB, 0); close(fd);
    if (ui_wheel >= 0) { ioctl(ui_wheel, UI_DEV_DESTROY); close(ui_wheel); }
    return 0;
}
