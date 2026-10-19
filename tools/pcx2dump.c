/*
 * pcx2dump - preserve what can be read from a PowerVR PCX1/PCX2 card on Linux.
 *
 *   sudo ./pcx2dump                 config space, option ROM, register file
 *   sudo ./pcx2dump --masks         also: implemented-bit masks of safe registers
 *   sudo ./pcx2dump --texmem        also: set up texture memory the way the
 *                                   Windows driver does, then test all 4 MB
 *   sudo ./pcx2dump --dev 0000:0c:01.0   (default: the first PCX1/PCX2 found)
 *
 * Run it with QEMU stopped. Without --texmem it never touches the 4 MB
 * texture-memory window (BAR1): the driver source warns that reading texture
 * memory before the memory registers are set up can lock the machine.
 *
 * --texmem first writes the memory/bus registers with the exact values and
 * order the 4.x VSGL.VXD uses at start-up (TMEM_SETUP, TMEM_REFRESH, ARBMODE,
 * PCI21COMP, CLKSELECT, POWERDOWN, then a SOFTRESET pulse), syncs everything
 * already saved to disk, and only then touches BAR1. A small chance of a
 * host freeze remains; nothing on the card can be damaged by it.
 *
 * Output goes to a new folder pcx2dump-YYYYMMDD-HHMMSS/ :
 *   config.bin   256 bytes of PCI configuration space
 *   rom.bin      option ROM, if the card has one
 *   regs.bin     BAR0 dwords 0x000-0x3FF (4 KB) as read at power-on
 *   report.txt   everything printed on screen
 *
 * --masks writes 0xFFFFFFFF then 0x00000000 to each register in a short
 * list of inert ones (nothing that starts a render, resets the chip, enables
 * interrupts, or touches memory/clock/bus configuration), reads back after
 * each write, and restores the original value.
 *
 * Build:  make bin/pcx2dump   (from the top of the repository)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "pcx2pci.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static FILE *report;

/* Base of the per-device sysfs directories (overridable for testing). */
static const char *
sysfs_base(void)
{
    const char *e = getenv("PCX2DUMP_SYSFS");
    return e ? e : "/sys/bus/pci/devices";
}

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
    }
}

static const char *
reg_name(unsigned i)
{
    static const char *n[0x20] = {
        "ID", "REVISION", "SOFTRESET", "INTSTATUS", "INTMASK", "STARTRENDER", "FOGAMOUNT", "OBJECT_OFFSET",
        "PAGE_CTRL", "(09)", "ISP_BASE", "PREC_BASE", "TMEM_SETUP", "TMEM_REFRESH", "FOGCOL", "CAMERA",
        "PACKMODE", "ARBMODE", "LSTRIDE", "SOFADDR", "XCLIP", "ABORTADDR", "GPPORT", "(17)",
        "IEEEFP", "BILINEAR", "(1A)", "PCI21COMP", "CLKSELECT", "FASTFOG", "POWERDOWN", "(1F)"
    };

    if (i < 0x20)
        return n[i];
    if (i >= 0x7d && i <= 0x7f)
        return "MEMTEST";
    if (i >= 0x80 && i < 0x100)
        return "FOG_TABLE";
    if (i >= 0x100 && i < 0x200)
        return "TLB";
    if (i >= 0x200 && i < 0x400)
        return "DIVIDER_TABLE";
    return "";
}

static int
write_file(const char *dir, const char *name, const void *buf, size_t len)
{
    char  path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    return 0;
}

static int
sysfs_write(const char *dev, const char *attr, const char *val)
{
    char path[256];
    int  fd;
    int  r;

    snprintf(path, sizeof(path), "%s/%s/%s", sysfs_base(), dev, attr);
    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    r = write(fd, val, strlen(val));
    close(fd);
    return r < 0 ? -1 : 0;
}

static void
dump_rom(const char *dev, const char *dir)
{
    char     path[256];
    uint8_t *buf;
    ssize_t  n;
    int      fd;

    snprintf(path, sizeof(path), "%s/%s/rom", sysfs_base(), dev);
    if (access(path, F_OK) != 0) {
        out("Option ROM: none (no ROM BAR exposed)\n");
        return;
    }
    if (sysfs_write(dev, "rom", "1") != 0) {
        out("Option ROM: could not enable reading (%s)\n", strerror(errno));
        return;
    }
    buf = malloc(1 << 20);
    fd  = open(path, O_RDONLY);
    n   = (fd >= 0) ? read(fd, buf, 1 << 20) : -1;
    if (fd >= 0)
        close(fd);
    sysfs_write(dev, "rom", "0");

    if (n <= 0)
        out("Option ROM: none readable\n");
    else {
        write_file(dir, "rom.bin", buf, n);
        out("Option ROM: %zd bytes saved to rom.bin, starts %02x %02x%s\n", n, buf[0], buf[1],
            (buf[0] == 0x55 && buf[1] == 0xaa) ? " (valid PC ROM signature)" : "");
    }
    free(buf);
}

/* Registers that are inert to write: none of them starts a render, resets
   the chip, raises interrupts, or configures memory, clocks or the bus. */
static const unsigned mask_regs[] = {
    0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0e, 0x0f, 0x10, 0x12, 0x13, 0x14, 0x15, 0x17,
    0x18, 0x19, 0x1a, 0x1d,
    0x080, 0x0ff, /* first and last fog table entries */
    0x100, 0x1ff, /* first and last TLB entries */
    0x200, 0x3ff  /* first and last divider table entries */
};


/* ---------------------------------------------------------------------- */
/* Texture memory test                                                     */
/* ---------------------------------------------------------------------- */

#define TEX_BYTES  (4u << 20)
#define TEX_DWORDS (TEX_BYTES / 4)

static uint32_t
pattern(uint32_t i, uint32_t seed)
{
    uint32_t x = (i + 1) * 0x9e3779b1u ^ seed;
    x ^= x >> 15;
    x *= 0x2c1b3c6du;
    x ^= x >> 12;
    return x;
}

static double
now_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* One full write pass then one full read pass with an address-derived
   pattern. Returns the number of mismatching dwords. */
static uint32_t
pattern_pass(volatile uint32_t *tex, uint32_t seed, double *wr_mbs, double *rd_mbs, uint32_t *alias_hits)
{
    uint32_t bad = 0, shown = 0;
    double   t0, t1, t2;

    t0 = now_s();
    for (uint32_t i = 0; i < TEX_DWORDS; i++)
        tex[i] = pattern(i, seed);
    t1 = now_s();
    *alias_hits = 0;
    for (uint32_t i = 0; i < TEX_DWORDS; i++) {
        uint32_t v = tex[i];
        if (v != pattern(i, seed)) {
            bad++;
            /* Smaller memory mirrored into the window shows up as the
               lower half holding the upper half's values. */
            if (i < TEX_DWORDS / 2 && v == pattern(i + TEX_DWORDS / 2, seed))
                (*alias_hits)++;
            if (shown++ < 8)
                out("    mismatch at BAR1+%06x: wrote %08x read %08x\n", i * 4, pattern(i, seed), v);
        }
    }
    t2 = now_s();
    *wr_mbs = TEX_BYTES / (1 << 20) / (t1 - t0);
    *rd_mbs = TEX_BYTES / (1 << 20) / (t2 - t1);
    return bad;
}

static void
texmem_test(const char *dev, volatile uint32_t *bar0, int is_pcx2)
{
    char               path[256];
    volatile uint32_t *tex;
    volatile uint16_t *tex16;
    volatile uint8_t  *tex8;
    uint32_t           v, bad, alias;
    double             wr, rd;
    int                fd;

    out("\nTexture memory test\n");
    out("  Before: TMEM_SETUP=%08x TMEM_REFRESH=%08x ARBMODE=%08x CLKSELECT=%08x POWERDOWN=%08x\n",
        bar0[0x0c], bar0[0x0d], bar0[0x11], bar0[0x1c], bar0[0x1e]);

    /* The VxD writes its default table in register-index order. */
    bar0[0x0c] = 0x22212275;                 /* TMEM_SETUP: 4 MB */
    bar0[0x0d] = 0x00000480;                 /* TMEM_REFRESH */
    bar0[0x11] = is_pcx2 ? 0x0000000d : 1;   /* ARBMODE */
    if (is_pcx2) {
        bar0[0x1b] = 0x00000000;             /* PCI21COMP */
        bar0[0x1c] = 0x00000000;             /* CLKSELECT */
        bar0[0x1e] = 0x00000001;             /* POWERDOWN */
    }
    bar0[0x02] = 1;                          /* SOFTRESET pulse, as at the end of InitBoard */
    bar0[0x02] = 0;
    usleep(10000);
    out("  After:  TMEM_SETUP=%08x TMEM_REFRESH=%08x ARBMODE=%08x CLKSELECT=%08x POWERDOWN=%08x\n",
        bar0[0x0c], bar0[0x0d], bar0[0x11], bar0[0x1c], bar0[0x1e]);

    if (report)
        fflush(report);
    sync();

    snprintf(path, sizeof(path), "%s/%s/resource1", sysfs_base(), dev);
    fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) {
        out("  Cannot open BAR1: %s\n", strerror(errno));
        return;
    }
    tex = mmap(NULL, TEX_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (tex == MAP_FAILED) {
        out("  Cannot map BAR1: %s\n", strerror(errno));
        close(fd);
        return;
    }
    tex16 = (volatile uint16_t *) tex;
    tex8  = (volatile uint8_t *) tex;

    /* 1. One word first, so a dead memory interface stops us early. */
    tex[0] = 0x12345678;
    v      = tex[0];
    out("  Single word: wrote 12345678, read %08x %s\n", v, v == 0x12345678 ? "(ok)" : "(FAILED)");
    if (v != 0x12345678) {
        out("  Stopping the memory test here.\n");
        goto done;
    }

    /* 2. Partial writes: how 16- and 8-bit writes land in a dword. */
    tex[1]    = 0x11112222;
    tex16[3]  = 0xbeef;          /* upper half of dword 1 */
    v         = tex[1];
    out("  16-bit write to upper half: %08x (expect beef2222) %s\n", v, v == 0xbeef2222 ? "ok" : "DIFFERENT");
    tex[2]    = 0x33334444;
    tex8[9]   = 0xab;            /* byte 1 of dword 2 */
    v         = tex[2];
    out("  8-bit write to byte 1:      %08x (expect 3333ab44) %s\n", v, v == 0x3333ab44 ? "ok" : "DIFFERENT");

    /* 3. Full pattern passes over all 4 MB. */
    bad = pattern_pass(tex, 0x00000000, &wr, &rd, &alias);
    out("  Pattern pass 1: %u of %u dwords wrong; write %.1f MB/s, read %.1f MB/s\n", bad, TEX_DWORDS, wr, rd);
    if (alias > TEX_DWORDS / 4)
        out("  The lower 2 MB mirrors the upper 2 MB: this board probably has 2 MB fitted\n");
    bad = pattern_pass(tex, 0xffffffff, &wr, &rd, &alias);
    out("  Pattern pass 2 (different values): %u of %u dwords wrong\n", bad, TEX_DWORDS);

    /* 4. Clear it, so the next driver start sees zeroed memory. */
    for (uint32_t i = 0; i < TEX_DWORDS; i++)
        tex[i] = 0;
    out("  Texture memory cleared. TMEM registers left at the driver's values.\n");

done:
    munmap((void *) tex, TEX_BYTES);
    close(fd);
}

int
main(int argc, char **argv)
{
    const char        *dev   = NULL;
    int                masks = 0;
    int                texmem = 0;
    int                assume_yes = 0;
    char               dir[64];
    char               path[256];
    char               drv[256] = "";
    uint8_t            cfg[256];
    uint32_t           regs[0x400];
    volatile uint32_t *bar0;
    int                fd;
    time_t             t = time(NULL);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--masks"))
            masks = 1;
        else if (!strcmp(argv[i], "--texmem"))
            texmem = 1;
        else if (!strcmp(argv[i], "--yes"))
            assume_yes = 1;
        else if (!strcmp(argv[i], "--dev") && i + 1 < argc)
            dev = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--masks] [--texmem [--yes]] [--dev DDDD:BB:DD.F]\n", argv[0]);
            return 1;
        }
    }
    if (!dev && !(dev = pcx2_find_device())) {
        fprintf(stderr, "No PowerVR PCX1/PCX2 card (PCI 1033:0046 or 1033:002A) found; use --dev DDDD:BB:DD.F\n");
        return 1;
    }

    strftime(dir, sizeof(dir), "pcx2dump-%Y%m%d-%H%M%S", localtime(&t));
    {
        /* Two runs in the same second (e.g. "--masks; --texmem") get -2, -3... */
        char base[64];
        int  n = 1;
        snprintf(base, sizeof(base), "%s", dir);
        while (mkdir(dir, 0755) != 0) {
            if (errno != EEXIST || ++n > 99) {
                perror(dir);
                return 1;
            }
            snprintf(dir, sizeof(dir), "%.50s-%d", base, n);
        }
    }
    snprintf(path, sizeof(path), "%s/report.txt", dir);
    report = fopen(path, "w");

    out("pcx2dump: device %s, output %s/\n", dev, dir);
    snprintf(path, sizeof(path), "%s/%s/driver", sysfs_base(), dev);
    if (readlink(path, drv, sizeof(drv) - 1) > 0)
        out("Bound driver: %s (fine, as long as QEMU is not running)\n", strrchr(drv, '/') + 1);

    /* Configuration space. */
    snprintf(path, sizeof(path), "%s/%s/config", sysfs_base(), dev);
    fd = open(path, O_RDONLY);
    if (fd < 0 || read(fd, cfg, 256) != 256) {
        out("Cannot read config space (run with sudo): %s\n", strerror(errno));
        return 1;
    }
    close(fd);
    if (cfg[0] != 0x33 || cfg[1] != 0x10) {
        out("Not an NEC device at %s; stopping.\n", dev);
        return 1;
    }
    write_file(dir, "config.bin", cfg, 256);
    out("\nPCI configuration space (config.bin):\n");
    for (int i = 0; i < 256; i += 16) {
        out("  %02x:", i);
        for (int j = 0; j < 16; j++)
            out(" %02x", cfg[i + j]);
        out("\n");
    }
    if (cfg[0x06] & 0x10)
        out("  Capabilities list present at 0x%02x\n", cfg[0x34]);
    else
        out("  No capabilities list\n");

    dump_rom(dev, dir);

    /* Register file. */
    sysfs_write(dev, "enable", "1");
    snprintf(path, sizeof(path), "%s/%s/resource0", sysfs_base(), dev);
    fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) {
        out("Cannot open BAR0: %s\n", strerror(errno));
        return 1;
    }
    bar0 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bar0 == MAP_FAILED) {
        out("Cannot map BAR0: %s\n", strerror(errno));
        return 1;
    }

    for (int i = 0; i < 0x400; i++)
        regs[i] = bar0[i];
    write_file(dir, "regs.bin", regs, sizeof(regs));
    out("\nRegister file, non-zero dwords (regs.bin holds all 1024):\n");
    for (int i = 0; i < 0x400; i++)
        if (regs[i])
            out("  [%03x] %-14s %08x\n", i, reg_name(i), regs[i]);
    {
        int fog_nz = 0, tlb_nz = 0, div_nz = 0;
        for (int i = 0x80; i < 0x100; i++)
            fog_nz += regs[i] != 0;
        for (int i = 0x100; i < 0x200; i++)
            tlb_nz += regs[i] != 0;
        for (int i = 0x200; i < 0x400; i++)
            div_nz += regs[i] != 0;
        out("  Non-zero entries: fog table %d/128, TLB %d/256, divider table %d/512\n", fog_nz, tlb_nz, div_nz);
    }

    if (masks) {
        out("\nImplemented bits (write ones / write zeros, then restore):\n");
        out("  reg   name            before    after-1s  after-0s  restored\n");
        for (unsigned k = 0; k < sizeof(mask_regs) / sizeof(mask_regs[0]); k++) {
            unsigned i    = mask_regs[k];
            uint32_t orig = bar0[i], ones, zeros, back;

            bar0[i] = 0xffffffff;
            ones    = bar0[i];
            bar0[i] = 0x00000000;
            zeros   = bar0[i];
            bar0[i] = orig;
            back    = bar0[i];
            out("  [%03x] %-14s %08x  %08x  %08x  %08x%s\n", i, reg_name(i), orig, ones, zeros, back,
                back == orig ? "" : "  <- did not restore");
        }

        /* Did the mask writes disturb anything else? Re-read everything. */
        {
            uint32_t after[0x400];
            int      diffs = 0;
            for (int i = 0; i < 0x400; i++)
                after[i] = bar0[i];
            write_file(dir, "regs-after-masks.bin", after, sizeof(after));
            out("\nRegisters that changed during the mask test (excluding the tested ones):\n");
            for (int i = 0; i < 0x400; i++) {
                int tested = 0;
                for (unsigned k = 0; k < sizeof(mask_regs) / sizeof(mask_regs[0]); k++)
                    tested |= (mask_regs[k] == (unsigned) i);
                if (!tested && after[i] != regs[i]) {
                    if (diffs < 40)
                        out("  [%03x] %-14s %08x -> %08x\n", i, reg_name(i), regs[i], after[i]);
                    diffs++;
                }
            }
            out("  %d changed\n", diffs);
        }

        /* Divider-table read behaviour: is a read answered with the entry
           asked for, or with the previous one (pipelined ROM)? */
        {
            uint32_t a, b, c, d;
            out("\nDivider table read-order probe:\n");
            a = bar0[0x3ff]; b = bar0[0x3ff]; c = bar0[0x3ff]; d = bar0[0x3ff];
            out("  [3ff] x4           %08x %08x %08x %08x\n", a, b, c, d);
            a = bar0[0x3fe]; b = bar0[0x3ff];
            out("  [3fe],[3ff]        %08x %08x\n", a, b);
            a = bar0[0x200]; b = bar0[0x3ff];
            out("  [200],[3ff]        %08x %08x\n", a, b);
            a = bar0[0x3ff]; b = bar0[0x200];
            out("  [3ff],[200]        %08x %08x\n", a, b);
            a = bar0[0x203]; b = bar0[0x2c5]; c = bar0[0x203]; d = bar0[0x2c5];
            out("  [203],[2c5] x2     %08x %08x %08x %08x  (expect 00000fe0 00000724)\n", a, b, c, d);
            a = bar0[0x000]; b = bar0[0x203];
            out("  [000],[203]        %08x %08x\n", a, b);
        }
    }

    if (texmem) {
        int is_pcx2 = (cfg[2] == 0x46 && cfg[3] == 0x00);
        if (!assume_yes) {
            char ans[16] = "";
            printf("\nThe texture memory test writes the card's memory registers and then all 4 MB.\n"
                   "Small chance of a host freeze (reset button; no damage). Everything above is\n"
                   "already saved. Close other work first. Type yes to continue: ");
            fflush(stdout);
            if (!fgets(ans, sizeof(ans), stdin) || strncmp(ans, "yes", 3) != 0) {
                out("Texture memory test skipped.\n");
                texmem = 0;
            }
        }
        if (texmem)
            texmem_test(dev, bar0, is_pcx2);
    }

    munmap((void *) bar0, 0x1000);
    close(fd);
    out("\nDone. Please keep the whole %s/ folder.\n", dir);
    if (report)
        fclose(report);
    return 0;
}
