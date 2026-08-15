/*
 * ATI Rage 128 CCE microcode.
 *
 * Source lineage: Linux r128 DRM driver (r128_cce.c), which labels this
 * sequence "CCE microcode (from ATI)" and distributes it under the MIT/X11
 * permission notice reproduced below.
 *
 * Copyright 2000 Precision Insight, Inc.
 * Copyright 2000 VA Linux Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef BK_ATI_RAGE128_CCE_UCODE_H
#define BK_ATI_RAGE128_CCE_UCODE_H

#include "../../include/types.h"

#define R128_CCE_MICROCODE_DWORDS 512U

static const uint32_t r128_cce_microcode[R128_CCE_MICROCODE_DWORDS] = {
    0, 276838400, 0, 268449792, 2, 142, 2, 145,
    0, 1076765731, 0, 1617039951, 0, 774592877, 0, 1987540286,
    0, 2307490946U, 0, 599558925, 0, 589505315, 0, 596487092,
    0, 589505315, 1, 11544576, 1, 206848, 1, 311296,
    1, 198656, 2, 912273422, 11, 262144, 0, 0,
    1, 33559837, 1, 7438, 1, 14809, 1, 6615,
    12, 28, 1, 6614, 12, 28, 2, 23,
    11, 18874368, 0, 16790922, 1, 409600, 9, 30,
    1, 147854772, 16, 420483072, 3, 8192, 0, 10240,
    1, 198656, 1, 15630, 1, 51200, 10, 34858,
    9, 42, 1, 33559823, 2, 10276, 1, 15717,
    1, 15718, 2, 43, 1, 15936948, 1, 570480831,
    1, 14715071, 12, 322123831, 1, 33953125, 12, 55,
    1, 33559908, 1, 15718, 2, 46, 4, 2099258,
    1, 526336, 1, 442623, 4, 4194365, 1, 509952,
    1, 459007, 3, 0, 12, 92, 2, 46,
    12, 176, 1, 15734, 1, 206848, 1, 18432,
    1, 133120, 1, 100670734, 1, 149504, 1, 165888,
    1, 15975928, 1, 1048576, 6, 3145806, 1, 15715,
    16, 2150645232U, 2, 268449859, 2, 10307, 12, 176,
    1, 15734, 1, 15735, 1, 15630, 1, 15631,
    1, 5253120, 6, 3145810, 16, 2150645232U, 1, 15864,
    2, 82, 1, 343310, 1, 1064207, 2, 3145813,
    1, 15728, 1, 7817, 1, 15729, 3, 15730,
    12, 92, 2, 98, 1, 16168, 1, 16167,
    1, 16002, 1, 16008, 1, 15974, 1, 15975,
    1, 15990, 1, 15976, 1, 15977, 1, 15980,
    0, 15981, 1, 10240, 1, 5253120, 1, 15720,
    1, 198656, 6, 110, 1, 180224, 1, 103824738,
    2, 112, 2, 3145839, 0, 536885440, 1, 114880,
    14, 125, 12, 206975, 1, 33559995, 12, 198784,
    0, 33570236, 1, 15803, 0, 15804, 3, 294912,
    1, 294912, 3, 442370, 1, 11544576, 0, 811612160,
    1, 12593152, 1, 11536384, 1, 14024704, 7, 310382726,
    0, 10240, 1, 14796, 1, 14797, 1, 14793,
    1, 14794, 0, 14795, 1, 268679168, 1, 9437184,
    1, 268449792, 1, 198656, 1, 9452827, 1, 1075854602,
    1, 1075854603, 1, 557056, 1, 114880, 14, 159,
    12, 198784, 1, 1109409213, 12, 198783, 1, 1107312059,
    12, 198784, 1, 1109409212, 2, 162, 1, 1075854781,
    1, 1073757627, 1, 1075854780, 1, 540672, 1, 10485760,
    6, 3145894, 16, 274741248, 9, 168, 3, 4194304,
    3, 4209949, 0, 0, 0, 256, 14, 174,
    1, 114857, 1, 33560007, 12, 176, 0, 10240,
    1, 114858, 1, 33560018, 1, 114857, 3, 33560007,
    1, 16008, 1, 114874, 1, 33560360, 1, 114875,
    1, 33560154, 0, 15963, 0, 256, 0, 4096,
    1, 409611, 9, 188, 0, 10240, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

#endif
