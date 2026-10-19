/*
 * pcx2replay - replay a frame the Windows driver sent to a PowerVR PCX2,
 *              directly on the card through VFIO, with no VM.
 *
 * Inputs, both taken from one QEMU tracing run:
 *   TRACE  the QEMU trace log (vfio_region_write lines for the card)
 *   RAM    a dump of the VM's memory from address 0, taken at the (qemu)
 *          prompt after the frame was started:  pmemsave 0 0x1000000 ram16.bin
 *
 * The tool maps RAM into the card's view of memory (IOMMU) at the same
 * addresses the VM used, so the driver's page table, parameter pointers and
 * drawing address work unchanged, and the card's pixel writes land in this
 * program's copy instead of anyone's real memory. It then replays every
 * register and texture-memory write from the trace in order, starts the
 * render, and works out how the end-of-render interrupt is acknowledged.
 *
 * Build:  make bin/pcx2replay   (from the top of the repository)
 * Use:    sudo bin/pcx2replay TRACE RAM            (VM off; card bound to vfio-pci)
 *         bin/pcx2replay --dry-run TRACE RAM       (no card: parse, and save the
 *                                                 frame already in RAM)
 *
 * Output folder pcx2replay-DATE/: report.txt, frame-before.ppm (what the RAM
 * dump held at the drawing address), frame.ppm (what the card drew), and the
 * raw 16-bit frames.
 *
 * Safety: the card can only reach the buffer this program maps for it. The
 * worst expected outcome is a card that stops responding until the next
 * reboot.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "pcx2pci.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/vfio.h>

#define RAM_MAX       (64u << 20)
#define REG_INTSTATUS 0x03
#define REG_SOFTRESET 0x02
#define REG_START     0x05
#define REG_PAGECTRL  0x08
#define REG_LSTRIDE   0x12
#define REG_SOFADDR   0x13
#define REG_XCLIP     0x14
#define REG_PACKMODE  0x10
#define FB_W          640
#define FB_H          480
#define FILL          0xf81f /* magenta in RGB565: pixels the card never wrote */

typedef struct {
    uint8_t  bar;
    uint32_t off;
    uint32_t val;
} wr_t;

static FILE *report;
static char  dir[96];

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

/* ---- trace parsing ---------------------------------------------------- */

static wr_t *
parse_trace(const char *fn, size_t *n_out, uint32_t shadow[0x400])
{
    FILE  *f = fopen(fn, "r");
    char   line[512];
    size_t n = 0, cap = 1 << 16;
    wr_t  *w = malloc(cap * sizeof(*w));

    if (!f) {
        perror(fn);
        exit(1);
    }
    while (fgets(line, sizeof(line), f)) {
        char        *p = strstr(line, "vfio_region_write");
        unsigned     bar, size;
        unsigned int off, val;
        if (!p)
            continue;
        p = strstr(p, ":region");
        if (!p || sscanf(p, ":region%u+0x%x, 0x%x, %u)", &bar, &off, &val, &size) != 4)
            continue;
        if (bar > 1 || size != 4) {
            fprintf(stderr, "skipping unexpected access: %s", line);
            continue;
        }
        if (n == cap)
            w = realloc(w, (cap *= 2) * sizeof(*w));
        w[n].bar = bar;
        w[n].off = off;
        w[n].val = val;
        if (bar == 0 && off < 0x1000)
            shadow[off >> 2] = val;
        n++;
    }
    fclose(f);
    *n_out = n;
    return w;
}

/* ---- frame output ----------------------------------------------------- */

static void
save_frame(const uint8_t *ram, size_t ram_len, uint32_t sof, uint32_t stride, const char *name)
{
    char     fn[160];
    FILE    *raw, *ppm;
    uint32_t written = 0;

    snprintf(fn, sizeof(fn), "%s/%s.raw", dir, name);
    raw = fopen(fn, "wb");
    snprintf(fn, sizeof(fn), "%s/%s.ppm", dir, name);
    ppm = fopen(fn, "wb");
    if (!raw || !ppm) {
        out("  cannot write %s\n", fn);
        return;
    }
    fprintf(ppm, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++) {
        for (int x = 0; x < FB_W; x++) {
            uint32_t a  = sof + y * stride + x * 2;
            uint16_t px = (a + 1 < ram_len) ? (ram[a] | (ram[a + 1] << 8)) : 0;
            uint8_t  rgb[3];
            fwrite(&px, 2, 1, raw);
            written += (px != FILL);
            rgb[0] = ((px >> 11) & 0x1f) * 255 / 31; /* RGB565 */
            rgb[1] = ((px >> 5) & 0x3f) * 255 / 63;
            rgb[2] = (px & 0x1f) * 255 / 31;
            fwrite(rgb, 3, 1, ppm);
        }
    }
    fclose(raw);
    fclose(ppm);
    out("  saved %s.ppm and %s.raw (640x480 RGB565 from 0x%08x, stride %u); %u pixels differ from the fill\n",
        name, name, sof, stride, written);
}

/* ---- VFIO ------------------------------------------------------------- */

typedef struct {
    int      container, group, device, efd;
    uint64_t bar_off[2], cfg_off;
} vfio_t;

static uint32_t
bar_rd(vfio_t *v, int bar, uint32_t off)
{
    uint32_t val = 0xffffffff;
    if (pread(v->device, &val, 4, v->bar_off[bar] + off) != 4)
        out("  read BAR%d+%x failed: %s\n", bar, off, strerror(errno));
    return val;
}

static void
bar_wr(vfio_t *v, int bar, uint32_t off, uint32_t val)
{
    if (pwrite(v->device, &val, 4, v->bar_off[bar] + off) != 4)
        out("  write BAR%d+%x failed: %s\n", bar, off, strerror(errno));
}

static uint32_t reg_rd(vfio_t *v, int r) { return bar_rd(v, 0, r * 4); }
static void     reg_wr(vfio_t *v, int r, uint32_t val) { bar_wr(v, 0, r * 4, val); }

static void
irq_unmask(vfio_t *v)
{
    struct vfio_irq_set s = { .argsz = sizeof(s),
                              .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK,
                              .index = VFIO_PCI_INTX_IRQ_INDEX, .start = 0, .count = 1 };
    if (ioctl(v->device, VFIO_DEVICE_SET_IRQS, &s))
        out("  unmask failed: %s\n", strerror(errno));
}

/* Wait up to ms for an interrupt; returns microseconds waited or -1. */
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
vfio_open(vfio_t *v, const char *dev, void *ram, size_t ram_len)
{
    char                       path[256], link[256];
    ssize_t                    l;
    struct vfio_group_status   gs  = { .argsz = sizeof(gs) };
    struct vfio_region_info    ri  = { .argsz = sizeof(ri) };
    struct vfio_irq_info       ii  = { .argsz = sizeof(ii) };
    struct vfio_iommu_type1_dma_map dm = { .argsz = sizeof(dm) };
    char                       irqbuf[sizeof(struct vfio_irq_set) + sizeof(int)];
    struct vfio_irq_set       *is = (struct vfio_irq_set *) irqbuf;
    uint16_t                   cmd;

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/iommu_group", dev);
    if ((l = readlink(path, link, sizeof(link) - 1)) < 0) {
        out("No IOMMU group for %s: %s\n", dev, strerror(errno));
        return -1;
    }
    link[l] = 0;
    snprintf(path, sizeof(path), "/dev/vfio/%s", basename(link));

    v->container = open("/dev/vfio/vfio", O_RDWR);
    if (v->container < 0 || ioctl(v->container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        out("Cannot open /dev/vfio/vfio\n");
        return -1;
    }
    v->group = open(path, O_RDWR);
    if (v->group < 0) {
        out("Cannot open %s: %s (is the VM still running? did you run 'win98vm.sh bind'?)\n", path,
            strerror(errno));
        return -1;
    }
    ioctl(v->group, VFIO_GROUP_GET_STATUS, &gs);
    if (!(gs.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        out("IOMMU group %s is not viable (another device in it is not bound to vfio-pci)\n", path);
        return -1;
    }
    if (ioctl(v->group, VFIO_GROUP_SET_CONTAINER, &v->container) ||
        (ioctl(v->container, VFIO_SET_IOMMU, VFIO_TYPE1v2_IOMMU) &&
         ioctl(v->container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU))) {
        out("Cannot set up the IOMMU: %s\n", strerror(errno));
        return -1;
    }

    /* The card sees our RAM copy at the VM's addresses (IOVA = guest physical). */
    dm.vaddr = (uintptr_t) ram;
    dm.iova  = 0;
    dm.size  = ram_len;
    dm.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
    if (ioctl(v->container, VFIO_IOMMU_MAP_DMA, &dm)) {
        /* Some platforms reserve the lowest pages; retry from 1 MB up. */
        out("Mapping RAM at 0 failed (%s); retrying from 0x100000\n", strerror(errno));
        dm.vaddr = (uintptr_t) ram + 0x100000;
        dm.iova  = 0x100000;
        dm.size  = ram_len - 0x100000;
        if (ioctl(v->container, VFIO_IOMMU_MAP_DMA, &dm)) {
            out("Cannot map memory for the card: %s\n", strerror(errno));
            return -1;
        }
    }
    out("Card's view of memory: 0x%08llx-0x%08llx mapped to this program\n", (unsigned long long) dm.iova,
        (unsigned long long) (dm.iova + dm.size - 1));

    v->device = ioctl(v->group, VFIO_GROUP_GET_DEVICE_FD, dev);
    if (v->device < 0) {
        out("Cannot open device %s: %s\n", dev, strerror(errno));
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        ri.index = i;
        if (ioctl(v->device, VFIO_DEVICE_GET_REGION_INFO, &ri)) {
            out("No BAR%d info\n", i);
            return -1;
        }
        v->bar_off[i] = ri.offset;
        out("BAR%d: %llu KB\n", i, (unsigned long long) ri.size >> 10);
    }
    ri.index = VFIO_PCI_CONFIG_REGION_INDEX;
    ioctl(v->device, VFIO_DEVICE_GET_REGION_INFO, &ri);
    v->cfg_off = ri.offset;

    /* Memory decoding + bus mastering on. */
    if (pread(v->device, &cmd, 2, v->cfg_off + 4) == 2) {
        cmd |= 0x0006;
        if (pwrite(v->device, &cmd, 2, v->cfg_off + 4) != 2)
            out("Cannot enable bus mastering\n");
    }

    /* INTx to an eventfd. */
    ii.index = VFIO_PCI_INTX_IRQ_INDEX;
    if (ioctl(v->device, VFIO_DEVICE_GET_IRQ_INFO, &ii) || ii.count < 1) {
        out("Card has no INTx interrupt available\n");
        return -1;
    }
    v->efd       = eventfd(0, 0);
    is->argsz    = sizeof(irqbuf);
    is->flags    = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    is->index    = VFIO_PCI_INTX_IRQ_INDEX;
    is->start    = 0;
    is->count    = 1;
    memcpy(is->data, &v->efd, sizeof(int));
    if (ioctl(v->device, VFIO_DEVICE_SET_IRQS, is)) {
        out("Cannot attach the interrupt: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- the interrupt experiment ---------------------------------------- */

static void
ack_experiment(vfio_t *v)
{
    double   t;
    uint32_t a, b, c;

    out("\nInterrupt acknowledge test\n");
    /* 1. Leave the card alone: is the line still asserted? */
    irq_unmask(v);
    t = irq_wait(v, 50);
    out("  1. unmasked without touching the card: %s\n",
        t >= 0 ? "fired again (line held until acknowledged)" : "no new interrupt (pulse, or self-clearing)");
    if (t < 0)
        return;

    /* 2. Read INTSTATUS twice, then see whether the line dropped. */
    a = reg_rd(v, REG_INTSTATUS);
    b = reg_rd(v, REG_INTSTATUS);
    irq_unmask(v);
    t = irq_wait(v, 50);
    out("  2. read INTSTATUS: %08x then %08x; after unmask: %s\n", a, b,
        t >= 0 ? "fired again" : "quiet");
    if (t < 0) {
        out("  => Reading INTSTATUS acknowledges the interrupt%s.\n",
            (a & 2) && !(b & 2) ? " (bit 1 set on the first read, clear on the second)" : "");
        return;
    }

    /* 3. Write the bits back (write-1-to-clear). */
    reg_wr(v, REG_INTSTATUS, a);
    c = reg_rd(v, REG_INTSTATUS);
    irq_unmask(v);
    t = irq_wait(v, 50);
    out("  3. wrote %08x to INTSTATUS, reads %08x; after unmask: %s\n", a, c, t >= 0 ? "fired again" : "quiet");
    if (t < 0) {
        out("  => Writing 1s to INTSTATUS acknowledges the interrupt.\n");
        return;
    }

    /* 4. SOFTRESET pulse. */
    reg_wr(v, REG_SOFTRESET, 1);
    reg_wr(v, REG_SOFTRESET, 0);
    c = reg_rd(v, REG_INTSTATUS);
    irq_unmask(v);
    t = irq_wait(v, 50);
    out("  4. SOFTRESET pulse, INTSTATUS reads %08x; after unmask: %s\n", c, t >= 0 ? "fired again" : "quiet");
    out(t < 0 ? "  => Only SOFTRESET clears the interrupt.\n"
              : "  => Nothing tried clears it; the line stays asserted.\n");
}

/* ---- main ------------------------------------------------------------- */

int
main(int argc, char **argv)
{
    const char *dev = NULL, *tracefn = NULL, *ramfn = NULL;
    int         dry = 0, ai = 1;
    uint32_t    shadow[0x400] = { 0 };
    size_t      nw, ram_len;
    wr_t       *w;
    uint8_t    *ram;
    struct stat st;
    time_t      tt = time(NULL);
    char        path[160];
    FILE       *f;
    uint32_t    sof, stride;
    size_t      n0 = 0, n1 = 0;
    long        start_idx = -1;

    for (; ai < argc && argv[ai][0] == '-'; ai++) {
        if (!strcmp(argv[ai], "--dry-run"))
            dry = 1;
        else if (!strcmp(argv[ai], "--dev") && ai + 1 < argc)
            dev = argv[++ai];
        else
            break;
    }
    if (argc - ai != 2) {
        fprintf(stderr, "usage: %s [--dry-run] [--dev DDDD:BB:DD.F] TRACE.log RAM.bin\n", argv[0]);
        return 1;
    }
    tracefn = argv[ai];
    ramfn   = argv[ai + 1];

    strftime(dir, sizeof(dir), "pcx2replay-%Y%m%d-%H%M%S", localtime(&tt));
    if (mkdir(dir, 0755) && errno != EEXIST) {
        perror(dir);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/report.txt", dir);
    report = fopen(path, "w");

    /* Trace. */
    w = parse_trace(tracefn, &nw, shadow);
    for (size_t i = 0; i < nw; i++) {
        if (w[i].bar == 0)
            n0++;
        else
            n1++;
        if (w[i].bar == 0 && w[i].off == REG_START * 4)
            start_idx = (long) i;
    }
    out("Trace %s: %zu register writes, %zu texture-memory writes\n", tracefn, n0, n1);
    if (start_idx < 0) {
        out("The trace never starts a render (no write to STARTRENDER); nothing to replay.\n");
        return 1;
    }
    if ((size_t) start_idx != nw - 1)
        out("Note: %zu writes follow the first STARTRENDER; only writes up to it are replayed.\n",
            nw - 1 - (size_t) start_idx);
    sof    = shadow[REG_SOFADDR];
    stride = shadow[REG_LSTRIDE];
    out("Frame: SOFADDR=%08x LSTRIDE=%u PACKMODE=%02x XCLIP=%08x PAGE_CTRL=%08x TLB[0]=%03x TLB[127]=%03x\n", sof,
        stride, shadow[REG_PACKMODE], shadow[REG_XCLIP], shadow[REG_PAGECTRL], shadow[0x100], shadow[0x17f]);

    /* RAM image. */
    if (stat(ramfn, &st) || st.st_size <= 0 || (size_t) st.st_size > RAM_MAX) {
        out("RAM image %s missing or larger than %u MB\n", ramfn, RAM_MAX >> 20);
        return 1;
    }
    ram_len = ((size_t) st.st_size + 0xfff) & ~(size_t) 0xfff;
    ram     = mmap(NULL, ram_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    f       = fopen(ramfn, "rb");
    if (ram == MAP_FAILED || !f || fread(ram, 1, st.st_size, f) != (size_t) st.st_size) {
        out("Cannot load %s\n", ramfn);
        return 1;
    }
    fclose(f);
    out("RAM image %s: %zu MB\n", ramfn, ram_len >> 20);
    {
        uint32_t isp_lo = ((shadow[REG_PAGECTRL] & 0xff) << 24) | (shadow[0x100] << 12);
        if (isp_lo + 0x200000 > ram_len)
            out("WARNING: the ISP buffer (from 0x%08x) is outside the RAM image; dump more memory.\n", isp_lo);
    }

    /* What the dump already holds at the drawing address: on a VM that
       crashed after the render, rows above 1 MB are the card's real output. */
    save_frame(ram, ram_len, sof, stride, "frame-before");
    if (dry) {
        out("Dry run: card not touched.\n");
        return 0;
    }

    /* Fill the frame area so untouched pixels show as magenta. */
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            uint32_t a = sof + y * stride + x * 2;
            if (a + 1 < ram_len) {
                ram[a]     = FILL & 0xff;
                ram[a + 1] = FILL >> 8;
            }
        }

    {
        vfio_t v = { 0 };
        double t;
        if (mlock(ram, ram_len))
            out("(mlock failed: %s; continuing)\n", strerror(errno));
        if (!dev && !(dev = pcx2_find_device())) {
            fprintf(stderr, "No PowerVR PCX1/PCX2 card (PCI 1033:0046 or 1033:002A) found; use --dev DDDD:BB:DD.F\n");
            return 1;
        }
        if (vfio_open(&v, dev, ram, ram_len))
            return 1;
        out("Card ID %08x, REVISION %u, INTSTATUS %08x before replay\n", reg_rd(&v, 0), reg_rd(&v, 1),
            reg_rd(&v, REG_INTSTATUS));

        /* Replay everything up to (not including) STARTRENDER. */
        for (long i = 0; i < start_idx; i++)
            bar_wr(&v, w[i].bar, w[i].off, w[i].val);
        out("Replayed %ld writes; INTSTATUS %08x\n", start_idx, reg_rd(&v, REG_INTSTATUS));

        reg_wr(&v, REG_START, w[start_idx].val);
        t = irq_wait(&v, 2000);
        if (t < 0) {
            out("No interrupt within 2 s of STARTRENDER. INTSTATUS %08x\n", reg_rd(&v, REG_INTSTATUS));
        } else {
            out("End-of-render interrupt %.0f us after STARTRENDER\n", t);
            ack_experiment(&v);
        }
        reg_wr(&v, REG_SOFTRESET, 1);
        reg_wr(&v, REG_SOFTRESET, 0);
        save_frame(ram, ram_len, sof, stride, "frame");
        snprintf(path, sizeof(path), "%s/isp-after.bin", dir);
        if ((f = fopen(path, "wb"))) {
            uint32_t isp_lo = ((shadow[REG_PAGECTRL] & 0xff) << 24) | (shadow[0x100] << 12);
            if (isp_lo + 0x200000 <= ram_len)
                fwrite(ram + isp_lo, 1, 0x200000, f);
            fclose(f);
        }
    }
    out("\nDone. Please send the whole %s/ folder.\n", dir);
    return 0;
}
