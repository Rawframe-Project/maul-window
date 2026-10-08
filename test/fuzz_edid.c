// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the EDID reader: any bytes, read as a monitor's EDID, then
// again with every whole block's checksum made right, so the data
// blocks behind the checksums are reached too. What it finds must be a
// block's: finite luminances from 0 up, the least never above the most,
// and nothing at all when it finds none; and a size is whole or none.

#include "edid.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// An EDID of a base block and up to 255 extensions.
#define MOST_BYTES (256 * 128)

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static void Check(const uint8_t* bytes, size_t length)
{
    mwinEdidHdr hdr;
    bool found = mwinEdidHdrOf(bytes, length, &hdr);
    Expect(isfinite(hdr.peakNits) && isfinite(hdr.frameAverageNits) && isfinite(hdr.minimumNits) &&
           hdr.peakNits >= 0.0f && hdr.frameAverageNits >= 0.0f && hdr.minimumNits >= 0.0f &&
           hdr.minimumNits <= hdr.peakNits);
    Expect(found || (!hdr.pq && !hdr.hlg && hdr.peakNits == 0.0f && hdr.frameAverageNits == 0.0f &&
                     hdr.minimumNits == 0.0f));
    // A size fits the fields it comes from, and is none or whole.
    uint32_t width = 0;
    uint32_t height = 0;
    bool sized = mwinEdidSizeOf(bytes, length, &width, &height);
    Expect(sized == (width != 0) && (width != 0) == (height != 0) && width <= 4095 &&
           height <= 4095);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Check(data, size);
    static uint8_t bytes[MOST_BYTES];
    size_t length = size < MOST_BYTES ? size : MOST_BYTES;
    if (length > 0)
    {
        memcpy(bytes, data, length);
    }
    for (size_t block = 0; block + 128 <= length; block += 128)
    {
        uint8_t sum = 0;
        for (size_t i = 0; i < 127; i++)
        {
            sum = (uint8_t)(sum + bytes[block + i]);
        }
        bytes[block + 127] = (uint8_t)(0x100 - sum);
    }
    Check(bytes, length);
    return 0;
}
