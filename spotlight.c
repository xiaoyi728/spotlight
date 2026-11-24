/*
 * spotlight —— 实时聚光灯覆盖层（X11 / ARGB）
 *
 * 整屏压暗，鼠标周围留一个圆形亮区，实时跟随；点击穿透、不抓键。
 *
 * 用法:
 *   spotlight [--dim 0.55] [--radius 160] [--soft 0.5] [--debug]
 *
 *   --dim     压暗程度 0..1（默认 0.55）
 *   --radius  亮区半径像素（默认 160）
 *   --soft    边缘柔和度 0..1（默认 0.5，0=硬边）
 *   --debug   打印光圈图自检信息
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void on_sig(int s) { (void)s; running = 0; }

static void usage(void) {
    fprintf(stderr, "用法: spotlight [--dim 0.55] [--radius 160] [--soft 0.5]\n");
}

int main(int argc, char **argv) {
    double dim = 0.55, soft = 0.5;
    int radius = 160, debug = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dim") && i + 1 < argc) dim = atof(argv[++i]);
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc) radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--soft") && i + 1 < argc) soft = atof(argv[++i]);
        else if (!strcmp(argv[i], "--debug")) debug = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }
    if (dim < 0) dim = 0;
    if (dim > 0.95) dim = 0.95;
    if (radius < 20) radius = 20;
    if (radius > 900) radius = 900;
    if (soft < 0) soft = 0;
    if (soft > 1) soft = 1;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGHUP, on_sig);

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "spotlight: 无法连接 X server\n"); return 1; }

    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);
    int W = DisplayWidth(dpy, scr), H = DisplayHeight(dpy, scr);

    XVisualInfo vinfo;
    if (!XMatchVisualInfo(dpy, scr, 32, TrueColor, &vinfo)) {
        fprintf(stderr, "spotlight: 没有 32 位 ARGB visual\n");
        XCloseDisplay(dpy);
        return 1;
    }

    XSetWindowAttributes attrs;
    attrs.colormap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);
    attrs.background_pixel = 0;
    attrs.background_pixmap = None;
    attrs.border_pixel = 0;
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask;

    Window win = XCreateWindow(dpy, root, 0, 0, W, H, 0, vinfo.depth,
                               InputOutput, vinfo.visual,
                               CWColormap | CWBackPixel | CWBackPixmap |
                               CWBorderPixel | CWOverrideRedirect, &attrs);
    XStoreName(dpy, win, "spotlight");
    XSelectInput(dpy, win, ExposureMask | StructureNotifyMask);

    /* 输入区域为空 => 点击穿透 */
    XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XMapRaised(dpy, win);

    Picture dst = XRenderCreatePicture(
        dpy, win, XRenderFindStandardFormat(dpy, PictStandardARGB32), 0, NULL);

    /* 全屏压暗层 */
    XRenderColor dimc = {0, 0, 0, (unsigned short)(dim * 65535.0)};
    Picture dimfill = XRenderCreateSolidFill(dpy, &dimc);

    /*
     * 手工生成 2R x 2R 的 32 位 ARGB 光圈图：
     *   圆心 alpha=0（透出桌面）→ 圆周 alpha=dim（与暗层一致）
     *   方形四角也保持 dim，贴上去与暗层无缝。
     */
    int SZ = 2 * radius;
    unsigned char *data = calloc((size_t)SZ * SZ * 4, 1);
    if (!data) { fprintf(stderr, "spotlight: 内存不足\n"); return 1; }

    double inner = 1.0 - soft * 0.9;
    if (inner < 0.02) inner = 0.02;
    double c = SZ / 2.0;

    for (int y = 0; y < SZ; y++) {
        for (int x = 0; x < SZ; x++) {
            double d = hypot(x + 0.5 - c, y + 0.5 - c) / (double)radius;
            double a;
            if (d <= inner) a = 0.0;
            else if (d >= 1.0) a = dim;
            else {
                double k = (d - inner) / (1.0 - inner);
                a = dim * (k * k * (3.0 - 2.0 * k));
            }
            unsigned int alpha = (unsigned int)(a * 255.0 + 0.5);
            size_t off = ((size_t)y * SZ + x) * 4;
            data[off + 3] = (unsigned char)alpha;   /* 预乘 ARGB，黑色 */
        }
    }

    Pixmap spot = XCreatePixmap(dpy, win, SZ, SZ, 32);
    GC gc = XCreateGC(dpy, spot, 0, NULL);
    XImage *img = XCreateImage(dpy, vinfo.visual, 32, ZPixmap, 0, (char *)data,
                               SZ, SZ, 32, 0);
    XPutImage(dpy, spot, gc, img, 0, 0, 0, 0, SZ, SZ);
    img->data = NULL;
    XDestroyImage(img);

    if (debug) {
        XImage *back = XGetImage(dpy, spot, 0, 0, SZ, SZ, AllPlanes, ZPixmap);
        unsigned long pc = XGetPixel(back, SZ / 2, SZ / 2);
        unsigned long pe = XGetPixel(back, SZ / 2 + radius - 2, SZ / 2);
        unsigned long pk = XGetPixel(back, 1, 1);
        fprintf(stderr, "debug: 中心alpha=%lu 边缘alpha=%lu 角落alpha=%lu (期望 0/%d/%d)\n",
                (pc >> 24) & 0xff, (pe >> 24) & 0xff, (pk >> 24) & 0xff,
                (int)(dim * 255), (int)(dim * 255));
        XDestroyImage(back);
    }

    Picture spotp = XRenderCreatePicture(
        dpy, spot, XRenderFindStandardFormat(dpy, PictStandardARGB32), 0, NULL);

    while (running) {
        Window r1, c1; int rx, ry, wx, wy; unsigned int mask;
        int px = W / 2, py = H / 2;
        if (XQueryPointer(dpy, root, &r1, &c1, &rx, &ry, &wx, &wy, &mask)) {
            px = rx; py = ry;
        }

        /* 1) 整屏压暗 */
        XRenderFillRectangle(dpy, PictOpSrc, dst, &dimc, 0, 0, W, H);

        /* 2) 贴光圈图：方形内整体替换（中心透亮、四角=dim，与暗层无缝） */
        XRenderComposite(dpy, PictOpSrc, spotp, None, dst,
                         0, 0, 0, 0,
                         px - radius, py - radius, SZ, SZ);

        XFlush(dpy);
        usleep(1000 * 14);
    }

    XRenderFreePicture(dpy, spotp);
    XFreePixmap(dpy, spot);
    XFreeGC(dpy, gc);
    XRenderFreePicture(dpy, dimfill);
    XRenderFreePicture(dpy, dst);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
