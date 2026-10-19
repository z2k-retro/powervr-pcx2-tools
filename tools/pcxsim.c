/* pcxsim - run the PowerVR renderer on captured frames, outside 86Box.
 *
 *   pcxsim replay TRACE.log RAM.bin OUT.ppm [REFERENCE.raw]
 *       A QEMU VFIO trace plus a dump of guest memory from address 0
 *       (the pcx2replay inputs). REFERENCE.raw is pcx2replay's frame.raw
 *       (640x480 16-bit, as the real card drew it) to compare against.
 *
 *   pcxsim dump FRAME.bin OUT.ppm [CARD.raw]
 *       A frame dump written by the 86Box device (PCXDUMP1). CARD.raw is
 *       pcx2play's NAME-card.raw for the same frame (16-bit modes only);
 *       pixels the renderer leaves unwritten must be 0xA5A5 there, the
 *       fill pcx2play uses.
 *
 * Environment:
 *   SET=10=2,06=8   override registers before rendering (dump mode; hex
 *                   dword index = hex value), as pcx2play --set does
 *   THREADS=N       also time a render on N threads and check the output
 *                   is identical
 *
 * Build:  make bin/pcxsim   (from the top of the repository)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include "vid_powervr_pcx_render.h"
#include "vid_powervr_pcx_tables.h"

#define TEX_WORDS (1u << 20) /* 4 MB */

static uint32_t regs[0x400];
static uint32_t tex[TEX_WORDS];
static uint32_t *isp;
static uint32_t isp_words;
static uint32_t rgb[PCXR_MAX_W * PCXR_MAX_H];

static void
die(const char *m)
{
    fprintf(stderr, "%s\n", m);
    exit(1);
}

static void
load_trace(const char *fn)
{
    FILE    *f = fopen(fn, "r");
    char     line[512];
    unsigned bar, off, val, sz;
    if (!f)
        die("cannot open trace");
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "vfio_region_write");
        if (!p || !(p = strstr(p, ":region")))
            continue;
        if (sscanf(p, ":region%u+0x%x, 0x%x, %u)", &bar, &off, &val, &sz) != 4 || sz != 4)
            continue;
        if (bar == 0 && off < 0x1000)
            regs[off >> 2] = val;
        else if (bar == 1 && off < TEX_WORDS * 4)
            tex[off >> 2] = val;
    }
    fclose(f);
}

static uint8_t *
load_file(const char *fn, long *len)
{
    FILE    *f = fopen(fn, "rb");
    uint8_t *b;
    if (!f)
        die("cannot open file");
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(*len);
    if (fread(b, 1, *len, f) != (size_t) *len)
        die("short read");
    fclose(f);
    return b;
}

typedef struct { pcxr_job_t *job; uint32_t *rgb; uint32_t i, n; } targ_t;
static void *tmain(void *a) { targ_t *t = a; pcxr_render_tiles(t->job, t->rgb, t->i, t->n); return NULL; }
static double
render_threads(const pcxr_frame_t *fr, uint32_t *out, int n)
{
    static pcxr_job_t *job;
    pcxr_result_t res; pthread_t th[64]; targ_t ta[64]; struct timespec t0, t1;
    if (!job) job = pcxr_job_create();
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pcxr_prepare(job, fr, &res);
    for (int i = 0; i < n; i++) { ta[i] = (targ_t) { job, out, (uint32_t) i, (uint32_t) n }; pthread_create(&th[i], NULL, tmain, &ta[i]); }
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

static int ref_filled;

static int
render_and_save(const char *out, const char *ref)
{
    pcxr_frame_t  fr = { 0 };
    pcxr_result_t res;
    uint32_t      pm = regs[0x10];
    FILE         *f;
    int           w, h, diff = 0, compared = 0;
    uint16_t     *refpx = NULL;

    for (int i = 0; i < PCXR_MAX_W * PCXR_MAX_H; i++)
        rgb[i] = 0; /* as the device; PCXR_WRITTEN marks written pixels */
    fr.isp           = isp;
    fr.isp_words     = isp_words;
    fr.tex           = tex;
    fr.tex_words     = TEX_WORDS;
    fr.object_offset = regs[0x07];
    fr.fog_shift     = regs[0x06];
    fr.fog_colour    = regs[0x0e];
    fr.camera        = regs[0x0f];
    fr.xclip         = regs[0x14];
    fr.bilinear      = regs[0x19];
    fr.fog_table     = pcx2_fog_rom;
    {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int k = 0; k < 10; k++)
            pcxr_render(&fr, rgb, &res);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("render time %.2f ms\n", ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 10);
    }
    if (getenv("THREADS")) {
        int       n  = atoi(getenv("THREADS"));
        uint32_t *mt = malloc(sizeof(rgb));
        double    ms = 0;
        for (int k = 0; k < 5; k++) {
            for (int i = 0; i < PCXR_MAX_W * PCXR_MAX_H; i++) mt[i] = 0;
            ms += render_threads(&fr, mt, n);
        }
        printf("%d threads: %.2f ms per frame, output %s single-threaded\n", n, ms / 5,
               memcmp(mt, rgb, sizeof(rgb)) ? "DIFFERS from" : "identical to");
        free(mt);
    }
    printf("tiles %u, pixels shaded %u, area x %d-%d y %d-%d, error %d\n", res.tiles, res.pixels_shaded,
           res.min_x, res.max_x, res.min_y, res.max_y, res.error);
    printf("regs: OBJECT_OFFSET %08x FOGAMOUNT %u FOGCOL %08x CAMERA %x PACKMODE %02x XCLIP %08x BILINEAR %u\n",
           regs[0x07], regs[0x06], regs[0x0e], regs[0x0f], pm, regs[0x14], regs[0x19]);
    if (res.max_x < 0)
        return 1;

    w = 640;
    h = 480;
    if (ref) {
        long len;
        refpx = (uint16_t *) load_file(ref, &len);
    }
    f = fopen(out, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t px, c = rgb[y * PCXR_MAX_W + x];
            uint8_t  o[3];
            pcxr_pack(pm, c, x, y, &px);
            if ((pm & 3) == 3) {
                o[0] = ((px >> 10) & 31) * 255 / 31;
                o[1] = ((px >> 5) & 31) * 255 / 31;
                o[2] = (px & 31) * 255 / 31;
            } else if ((pm & 3) == 2) {
                o[0] = ((px >> 11) & 31) * 255 / 31;
                o[1] = ((px >> 5) & 63) * 255 / 63;
                o[2] = (px & 31) * 255 / 31;
            } else {
                o[0] = (c >> 16) & 0xff;
                o[1] = (c >> 8) & 0xff;
                o[2] = c & 0xff;
            }
            fwrite(o, 3, 1, f);
            if (refpx) {
                compared++;
                /* pcx2play fills the card's frame buffer with 0xa5a5 first */
                if ((c & PCXR_WRITTEN) ? (refpx[y * w + x] != (uint16_t) px) : (refpx[y * w + x] != 0xa5a5 && ref_filled)) {
                    if (diff < 0)
                        printf("  differs at (%d,%d): card %04x, renderer %04x\n", x, y, refpx[y * w + x], px);
                    diff++;
                }
            }
        }
    fclose(f);
    if (refpx)
        printf("compared %d pixels with the real card: %d differ\n", compared, diff);
    return refpx ? (diff != 0) : 0;
}

int
main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "replay")) {
        long     len;
        uint8_t *ram = load_file(argv[3], &len);
        uint32_t pagesize, hi;
        load_trace(argv[2]);
        switch ((regs[0x08] >> 8) & 3) {
            case 0: pagesize = 0x1000; break;
            case 1: pagesize = 0x2000; break;
            default: pagesize = 0x4000; break;
        }
        hi        = (regs[0x08] & 0xff) << 24;
        isp_words = 128 * pagesize / 4;
        isp       = calloc(isp_words, 4);
        for (int i = 0; i < 128; i++) {
            uint32_t pa = hi | ((regs[0x100 + i] & 0xfff) << 12);
            if (pa + pagesize <= (uint32_t) len)
                memcpy((uint8_t *) isp + i * pagesize, ram + pa, pagesize);
        }
        return render_and_save(argv[4], argc > 5 ? argv[5] : NULL);
    }
    if (argc >= 4 && !strcmp(argv[1], "dump")) {
        long      len;
        uint8_t  *d = load_file(argv[2], &len);
        uint32_t *h = (uint32_t *) d;
        uint32_t  page = h[5], pages = h[6], objb = h[7];
        long      off  = 64;
        if (memcmp(d, "PCXDUMP1", 8))
            die("not a PCXDUMP1 file");
        memcpy(regs, d + off, 4096);
        if (getenv("SET")) { /* register overrides, e.g. SET=10=2,06=8 */
            char *p = getenv("SET");
            while (*p) {
                unsigned r, v;
                int      n;
                if (sscanf(p, "%x=%x%n", &r, &v, &n) != 2 || r >= 0x400)
                    break;
                regs[r] = v;
                p += n;
                if (*p == ',')
                    p++;
            }
        }
        off += 4096;
        isp_words = pages * page / 4;
        isp       = malloc(pages * page);
        memcpy(isp, d + off, pages * page);
        off += pages * page + objb;
        if (len - off >= (long) sizeof(tex))
            memcpy(tex, d + off, sizeof(tex));
        ref_filled = 1;
        return render_and_save(argv[3], argc > 4 ? argv[4] : NULL);
    }
    fprintf(stderr, "usage: pcxsim replay TRACE RAM OUT.ppm [REF.raw] | pcxsim dump FRAME.bin OUT.ppm [CARD.raw]\n");
    return 1;
}
