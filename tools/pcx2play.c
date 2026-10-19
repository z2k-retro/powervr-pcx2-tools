/*
 * pcx2play - draw a frame captured by 86Box (PCXDUMP1) on a real PowerVR
 *            PCX2 through VFIO, and compare it with the 86Box renderer.
 *
 * Build:  make bin/pcx2play   (from the top of the repository)
 *
 * Use (VM off, card bound to vfio-pci):
 *     sudo bin/pcx2play [--dev DDDD:BB:DD.F] --out DIR --name NAME [--set REG=VALUE]... FRAME.bin
 *
 * REG is a register name (PACKMODE, BILINEAR, FOGAMOUNT, FOGCOL, PREC_BASE,
 * ISP_BASE, CAMERA, LSTRIDE, ...) or a dword index such as 0x10. When
 * PACKMODE changes the pixel size and LSTRIDE is not set, LSTRIDE becomes
 * 640 x bytes per pixel.
 *
 * Writes DIR/NAME-card.ppm (what the m3D drew), DIR/NAME-emu.ppm (the
 * renderer), DIR/NAME-diff.ppm (white where they differ), DIR/NAME-card.raw
 * and appends a summary to DIR/report.txt.
 *
 * Safety: the card can only reach this program's own buffer (IOMMU).
 */
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "pcx2pci.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/vfio.h>
#include "vid_powervr_pcx_render.h"

#define IOVA_BASE  0x01000000u /* one 16 MB window: PAGE_CTRL[7:0] = 0x01 */
#define IOVA_ISP   0x01000000u /* 2 MB parameter store */
#define IOVA_FB    0x01400000u /* frame buffer, up to 4 MB */
#define MAP_SIZE   0x00800000u
#define TEX_BYTES  0x00400000u
#define W          640
#define H          480

static FILE *report;

static void
out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (report) {
        va_start(ap, fmt);
        vfprintf(report, fmt, ap);
        va_end(ap);
        fflush(report);
    }
}

static double
now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* ---- VFIO (as in pcx2replay) ------------------------------------------ */

typedef struct {
    int      container, group, device, efd;
    uint64_t bar_off[2], cfg_off;
} vfio_t;

static uint32_t
reg_rd(vfio_t *v, int r)
{
    uint32_t val = 0xffffffff;
    if (pread(v->device, &val, 4, v->bar_off[0] + r * 4) != 4)
        out("  read reg %x failed: %s\n", r, strerror(errno));
    return val;
}

static void
reg_wr(vfio_t *v, int r, uint32_t val)
{
    if (pwrite(v->device, &val, 4, v->bar_off[0] + r * 4) != 4)
        out("  write reg %x failed: %s\n", r, strerror(errno));
}

static void
irq_unmask(vfio_t *v)
{
    struct vfio_irq_set s = { .argsz = sizeof(s),
                              .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK,
                              .index = VFIO_PCI_INTX_IRQ_INDEX, .start = 0, .count = 1 };
    ioctl(v->device, VFIO_DEVICE_SET_IRQS, &s);
}

static double
irq_wait(vfio_t *v, int ms)
{
    struct pollfd pfd = { .fd = v->efd, .events = POLLIN };
    double        t0  = now_us();
    uint64_t      cnt;
    if (poll(&pfd, 1, ms) <= 0)
        return -1;
    if (read(v->efd, &cnt, 8) != 8)
        return -1;
    return now_us() - t0;
}

static int
vfio_open(vfio_t *v, const char *dev, void *mem)
{
    char                            path[256], link[256];
    ssize_t                         l;
    struct vfio_group_status        gs = { .argsz = sizeof(gs) };
    struct vfio_region_info         ri = { .argsz = sizeof(ri) };
    struct vfio_iommu_type1_dma_map dm = { .argsz = sizeof(dm) };
    char                            irqbuf[sizeof(struct vfio_irq_set) + sizeof(int)];
    struct vfio_irq_set            *is = (struct vfio_irq_set *) irqbuf;
    uint16_t                        cmd;

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/iommu_group", dev);
    if ((l = readlink(path, link, sizeof(link) - 1)) < 0) {
        out("No IOMMU group for %s: %s\n", dev, strerror(errno));
        return -1;
    }
    link[l] = 0;
    snprintf(path, sizeof(path), "/dev/vfio/%s", basename(link));
    v->container = open("/dev/vfio/vfio", O_RDWR);
    v->group     = open(path, O_RDWR);
    if (v->container < 0 || v->group < 0) {
        out("Cannot open VFIO (%s): %s. Is the VM off and the card bound (win98vm.sh bind)?\n", path,
            strerror(errno));
        return -1;
    }
    ioctl(v->group, VFIO_GROUP_GET_STATUS, &gs);
    if (!(gs.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        out("IOMMU group not viable\n");
        return -1;
    }
    if (ioctl(v->group, VFIO_GROUP_SET_CONTAINER, &v->container) ||
        (ioctl(v->container, VFIO_SET_IOMMU, VFIO_TYPE1v2_IOMMU) && ioctl(v->container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU))) {
        out("Cannot set up the IOMMU: %s\n", strerror(errno));
        return -1;
    }
    dm.vaddr = (uintptr_t) mem;
    dm.iova  = IOVA_BASE;
    dm.size  = MAP_SIZE;
    dm.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
    if (ioctl(v->container, VFIO_IOMMU_MAP_DMA, &dm)) {
        out("Cannot map memory for the card: %s\n", strerror(errno));
        return -1;
    }
    v->device = ioctl(v->group, VFIO_GROUP_GET_DEVICE_FD, dev);
    if (v->device < 0) {
        out("Cannot open device %s: %s\n", dev, strerror(errno));
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        ri.index = i;
        if (ioctl(v->device, VFIO_DEVICE_GET_REGION_INFO, &ri))
            return -1;
        v->bar_off[i] = ri.offset;
    }
    ri.index = VFIO_PCI_CONFIG_REGION_INDEX;
    ioctl(v->device, VFIO_DEVICE_GET_REGION_INFO, &ri);
    v->cfg_off = ri.offset;
    if (pread(v->device, &cmd, 2, v->cfg_off + 4) == 2) {
        cmd |= 0x0006;
        if (pwrite(v->device, &cmd, 2, v->cfg_off + 4) != 2)
            out("Cannot enable bus mastering\n");
    }
    v->efd    = eventfd(0, 0);
    is->argsz = sizeof(irqbuf);
    is->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    is->index = VFIO_PCI_INTX_IRQ_INDEX;
    is->start = 0;
    is->count = 1;
    memcpy(is->data, &v->efd, sizeof(int));
    if (ioctl(v->device, VFIO_DEVICE_SET_IRQS, is)) {
        out("Cannot attach the interrupt: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- registers -------------------------------------------------------- */

static const struct {
    const char *name;
    int         idx;
} reg_names[] = {
    { "FOGAMOUNT", 0x06 }, { "OBJECT_OFFSET", 0x07 }, { "PAGE_CTRL", 0x08 }, { "ISP_BASE", 0x0a },
    { "PREC_BASE", 0x0b }, { "FOGCOL", 0x0e }, { "CAMERA", 0x0f }, { "PACKMODE", 0x10 },
    { "ARBMODE", 0x11 }, { "LSTRIDE", 0x12 }, { "SOFADDR", 0x13 }, { "XCLIP", 0x14 },
    { "IEEEFP", 0x18 }, { "BILINEAR", 0x19 }, { "FASTFOG", 0x1d },
};

static int
reg_index(const char *s)
{
    for (unsigned i = 0; i < sizeof(reg_names) / sizeof(reg_names[0]); i++)
        if (!strcasecmp(s, reg_names[i].name))
            return reg_names[i].idx;
    return (int) strtol(s, NULL, 0);
}

static int
bytes_pp(uint32_t packmode)
{
    return ((packmode & 3) == 0) ? 4 : ((packmode & 3) == 1) ? 3 : 2;
}

static void
to_rgb(uint32_t pm, const uint8_t *p, uint8_t *o)
{
    uint32_t v;
    switch (pm & 3) {
        case 0:
        case 1:
            o[0] = p[2];
            o[1] = p[1];
            o[2] = p[0];
            break;
        case 2:
            v    = p[0] | (p[1] << 8);
            o[0] = ((v >> 11) & 31) * 255 / 31;
            o[1] = ((v >> 5) & 63) * 255 / 63;
            o[2] = (v & 31) * 255 / 31;
            break;
        default:
            v    = p[0] | (p[1] << 8);
            o[0] = ((v >> 10) & 31) * 255 / 31;
            o[1] = ((v >> 5) & 31) * 255 / 31;
            o[2] = (v & 31) * 255 / 31;
            break;
    }
}

static void
write_ppm(const char *fn, const uint8_t *fb, uint32_t stride, uint32_t pm)
{
    FILE *f = fopen(fn, "wb");
    int   bpp = bytes_pp(pm);
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t o[3];
            to_rgb(pm, fb + y * stride + x * bpp, o);
            fwrite(o, 3, 1, f);
        }
    fclose(f);
}

int
main(int argc, char **argv)
{
    const char   *dev = NULL, *outdir = ".", *name = "frame", *dumpfn = NULL;
    int           set_idx[32];
    uint32_t      set_val[32];
    int           nset = 0, stride_set = 0;
    uint32_t      regs[0x400];
    uint8_t      *d, *mem, *isp, *fb, *emu;
    long          len;
    FILE         *f;
    vfio_t        v = { 0 };
    char          path[512];
    uint32_t      page, pages, objb, stride, pm, sof;
    double        t;
    pcxr_frame_t  fr;
    pcxr_result_t res;
    uint32_t     *rgb;
    int           bpp, diff = 0, outside = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dev") && i + 1 < argc)
            dev = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            outdir = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc)
            name = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc && nset < 32) {
            char *eq = strchr(argv[++i], '=');
            if (!eq) {
                fprintf(stderr, "--set needs REG=VALUE\n");
                return 1;
            }
            *eq            = 0;
            set_idx[nset]  = reg_index(argv[i]);
            set_val[nset]  = (uint32_t) strtoul(eq + 1, NULL, 0);
            stride_set    |= (set_idx[nset] == 0x12);
            nset++;
        } else
            dumpfn = argv[i];
    }
    if (!dumpfn) {
        fprintf(stderr, "usage: %s [--dev D] [--out DIR] [--name NAME] [--set REG=VAL]... FRAME.bin\n", argv[0]);
        return 1;
    }
    mkdir(outdir, 0755);
    snprintf(path, sizeof(path), "%s/report.txt", outdir);
    report = fopen(path, "a");

    /* Load the 86Box dump. */
    if (!(f = fopen(dumpfn, "rb"))) {
        out("Cannot open %s\n", dumpfn);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc(len);
    if (fread(d, 1, len, f) != (size_t) len || memcmp(d, "PCXDUMP1", 8)) {
        out("%s is not an 86Box PowerVR frame dump\n", dumpfn);
        return 1;
    }
    fclose(f);
    page  = ((uint32_t *) d)[5];
    pages = ((uint32_t *) d)[6];
    objb  = ((uint32_t *) d)[7];
    memcpy(regs, d + 64, sizeof(regs));
    if (pages > 128 || page * pages > 0x200000 || len < (long) (64 + 4096 + page * pages + objb + TEX_BYTES)) {
        out("%s: unexpected layout\n", dumpfn);
        return 1;
    }

    /* Apply overrides. */
    for (int i = 0; i < nset; i++)
        regs[set_idx[i]] = set_val[i];
    pm = regs[0x10];
    bpp = bytes_pp(pm);
    if (!stride_set)
        regs[0x12] = W * bpp;
    stride = regs[0x12];

    out("\n== %s: %s", name, dumpfn);
    for (int i = 0; i < nset; i++)
        out(" [%02x]=%08x", set_idx[i], set_val[i]);
    out("\n  PACKMODE %02x LSTRIDE %u BILINEAR %x FOGAMOUNT %u FOGCOL %08x CAMERA %x PREC_BASE %x ISP_BASE %x\n",
        pm, stride, regs[0x19], regs[0x06], regs[0x0e], regs[0x0f], regs[0x0b], regs[0x0a]);

    /* Card memory: parameter store and frame buffer. */
    mem = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return 1;
    isp = mem + (IOVA_ISP - IOVA_BASE);
    fb  = mem + (IOVA_FB - IOVA_BASE);
    memcpy(isp, d + 64 + 4096, page * pages);
    memset(fb, 0xa5, stride * H);
    mlock(mem, MAP_SIZE);

    if (getenv("PCX2PLAY_NOCARD")) {
        out("  (test mode: card not used)\n");
        goto card_done;
    }
    if (!dev && !(dev = pcx2_find_device())) {
        fprintf(stderr, "No PowerVR PCX1/PCX2 card (PCI 1033:0046 or 1033:002A) found; use --dev DDDD:BB:DD.F\n");
        return 1;
    }
    if (vfio_open(&v, dev, mem))
        return 1;
    out("  card ID %08x REVISION %u\n", reg_rd(&v, 0), reg_rd(&v, 1));

    /* Start-up, as the Matrox driver does it. */
    reg_wr(&v, 0x04, 2);
    reg_wr(&v, 0x08, 0);
    reg_wr(&v, 0x0a, regs[0x0a]);
    reg_wr(&v, 0x0b, regs[0x0b]);
    reg_wr(&v, 0x0c, 0x22212275);
    reg_wr(&v, 0x0d, 0x480);
    reg_wr(&v, 0x11, 0xd);
    reg_wr(&v, 0x14, 0);
    reg_wr(&v, 0x15, 0);
    reg_wr(&v, 0x16, 0);
    reg_wr(&v, 0x18, 7);
    reg_wr(&v, 0x19, 0);
    reg_wr(&v, 0x1b, 0);
    reg_wr(&v, 0x1c, 0);
    reg_wr(&v, 0x1d, 0);
    reg_wr(&v, 0x1e, 1);
    reg_wr(&v, 0x02, 1);
    reg_wr(&v, 0x02, 0);

    /* Texture memory, including the TSP parameters at its start. */
    {
        const uint8_t *tex = d + 64 + 4096 + page * pages + objb;
        double         t0  = now_us();
        for (uint32_t off = 0; off < TEX_BYTES; off += 0x10000)
            if (pwrite(v.device, tex + off, 0x10000, v.bar_off[1] + off) != 0x10000) {
                out("  texture upload failed at %x: %s\n", off, strerror(errno));
                return 1;
            }
        out("  texture memory uploaded (%.0f ms)\n", (now_us() - t0) / 1000);
    }

    /* The frame. */
    sof = IOVA_FB;
    reg_wr(&v, 0x16, 0);
    reg_wr(&v, 0x16, 0x80000000);
    reg_wr(&v, 0x06, regs[0x06]);
    reg_wr(&v, 0x07, regs[0x07]);
    reg_wr(&v, 0x0e, regs[0x0e]);
    reg_wr(&v, 0x0f, regs[0x0f]);
    reg_wr(&v, 0x10, pm);
    reg_wr(&v, 0x11, regs[0x11] ? regs[0x11] : 0xd);
    reg_wr(&v, 0x12, stride);
    reg_wr(&v, 0x13, sof);
    reg_wr(&v, 0x14, regs[0x14]);
    reg_wr(&v, 0x19, regs[0x19]);
    reg_wr(&v, 0x1d, regs[0x1d]);
    reg_wr(&v, 0x0a, regs[0x0a]);
    reg_wr(&v, 0x0b, regs[0x0b]);
    reg_wr(&v, 0x08, (regs[0x08] & 0x300) | (IOVA_ISP >> 24));
    for (uint32_t i = 0; i < 128; i++)
        reg_wr(&v, 0x100 + i, ((IOVA_ISP + i * page) >> 12) & 0xfff);
    reg_wr(&v, 0x02, 1);
    reg_wr(&v, 0x02, 0);
    reg_wr(&v, 0x05, 0);
    t = irq_wait(&v, 2000);
    if (t < 0)
        out("  NO end-of-render interrupt within 2 s; INTSTATUS %08x\n", reg_rd(&v, 0x03));
    else
        out("  rendered in %.0f us; INTSTATUS %08x\n", t, reg_rd(&v, 0x03));
    irq_unmask(&v);
    reg_wr(&v, 0x02, 1);
    reg_wr(&v, 0x02, 0);

card_done:
    snprintf(path, sizeof(path), "%s/%s-card.raw", outdir, name);
    if ((f = fopen(path, "wb"))) {
        fwrite(fb, 1, stride * H, f);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/%s-card.ppm", outdir, name);
    write_ppm(path, fb, stride, pm);

    /* The 86Box renderer on the same input. */
    rgb = calloc(PCXR_MAX_W * PCXR_MAX_H, 4);
    emu = malloc(stride * H);
    memset(emu, 0xa5, stride * H);
    memset(&fr, 0, sizeof(fr));
    fr.isp           = (const uint32_t *) isp;
    fr.isp_words     = page * pages / 4;
    fr.tex           = (const uint32_t *) (d + 64 + 4096 + page * pages + objb);
    fr.tex_words     = TEX_BYTES / 4;
    fr.object_offset = regs[0x07];
    fr.fog_shift     = regs[0x06];
    fr.fog_colour    = regs[0x0e];
    fr.camera        = regs[0x0f];
    fr.xclip         = regs[0x14];
    fr.bilinear      = regs[0x19];
    fr.fog_table     = &regs[0x80];
    pcxr_render(&fr, rgb, &res);
    for (int y = res.min_y; y <= res.max_y && y < H; y++)
        for (int x = res.min_x; x <= res.max_x && x < W; x++) {
            uint32_t px;
            if (!(rgb[y * PCXR_MAX_W + x] & PCXR_WRITTEN))
                continue; /* not written by the chip (masking plane) */
            pcxr_pack(pm, rgb[y * PCXR_MAX_W + x], x, y, &px);
            memcpy(emu + y * stride + x * bpp, &px, bpp);
        }
    snprintf(path, sizeof(path), "%s/%s-emu.ppm", outdir, name);
    write_ppm(path, emu, stride, pm);

    /* Compare. */
    snprintf(path, sizeof(path), "%s/%s-diff.ppm", outdir, name);
    f = fopen(path, "wb");
    if (f)
        fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int     dif = memcmp(fb + y * stride + x * bpp, emu + y * stride + x * bpp, bpp) != 0;
            uint8_t o[3];
            if (dif) {
                if (diff < 8) {
                    uint8_t a[3], b[3];
                    to_rgb(pm, fb + y * stride + x * bpp, a);
                    to_rgb(pm, emu + y * stride + x * bpp, b);
                    out("    (%d,%d) card %02x%02x%02x emulator %02x%02x%02x\n", x, y, a[0], a[1], a[2], b[0], b[1], b[2]);
                }
                diff++;
                if (x < res.min_x || x > res.max_x || y < res.min_y || y > res.max_y)
                    outside++;
            }
            o[0] = o[1] = o[2] = dif ? 255 : 0;
            if (f)
                fwrite(o, 3, 1, f);
        }
    if (f)
        fclose(f);
    out("  card vs 86Box renderer: %d of %d pixels differ (%d outside the tiles)\n", diff, W * H, outside);
    return 0;
}
