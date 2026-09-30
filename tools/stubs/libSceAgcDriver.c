/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Link-time facade of the PS5 libSceAgcDriver module: every function the OpenGL and RADV
 * drivers import. The native packaging tool turns each symbol into an import of the firmware
 * module; nothing here is linked into the application or ever runs.
 */
#include <stdint.h>

int32_t sceAgcDriverSubmitDcb(void) { return -1; }
int32_t sceAgcDriverGetWaitRenderingPacketSizeInDwords(void) { return -1; }
int32_t sceAgcDriverWaitUntilSafeForRendering(void) { return -1; }
int32_t sceAgcDriverSetTFRing(void) { return -1; }
int32_t sceAgcDriverGetTFRing(void) { return -1; }
int32_t sceAgcDriverGetHsOffchipParam(void) { return -1; }
int32_t sceAgcDriverSetHsOffchipParam(void) { return -1; }
