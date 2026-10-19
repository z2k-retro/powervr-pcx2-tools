/* pcx2pci.h - find a PowerVR PCX1/PCX2 card on the host PCI bus (Linux sysfs).
 *
 * Copyright 2026 Zvi Rosen and contributors. GPL-2.0-or-later.
 */
#ifndef PCX2PCI_H
#define PCX2PCI_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>

/* Returns the address ("0000:0c:01.0") of the first NEC 0x1033 device with
   ID 0x0046 (PCX2) or 0x002A (PCX1), or NULL. The string is static. */
static const char *
pcx2_find_device(void)
{
    static char    found[256];
    DIR           *d = opendir("/sys/bus/pci/devices");
    struct dirent *e;

    if (!d)
        return NULL;
    found[0] = 0;
    while ((e = readdir(d)) != NULL && !found[0]) {
        char     path[512];
        unsigned vendor = 0, device = 0;
        FILE    *f;

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/vendor", e->d_name);
        if ((f = fopen(path, "r")) != NULL) {
            if (fscanf(f, "%x", &vendor) != 1)
                vendor = 0;
            fclose(f);
        }
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/device", e->d_name);
        if ((f = fopen(path, "r")) != NULL) {
            if (fscanf(f, "%x", &device) != 1)
                device = 0;
            fclose(f);
        }
        if (vendor == 0x1033 && (device == 0x0046 || device == 0x002a))
            snprintf(found, sizeof(found), "%s", e->d_name);
    }
    closedir(d);
    return found[0] ? found : NULL;
}

#endif
