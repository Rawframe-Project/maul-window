// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A monitor's HDR static metadata from its EDID: an HDR monitor's
// block read (PQ and HLG, the most, the frame average and the least);
// a block with only its transfer functions; none in an SDR monitor's
// EDID, in a block whose checksum fails, in bytes that are no EDID,
// past the bytes there are, or in a data block longer than its
// collection.

#include "edid.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

// A base block (header, one extension, checksum) and a CTA-861
// extension with the given data blocks.
static void Make(uint8_t* bytes, const uint8_t* blocks, size_t length)
{
    static const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    memset(bytes, 0, 256);
    memcpy(bytes, header, sizeof(header));
    bytes[126] = 1;
    uint8_t* extension = bytes + 128;
    extension[0] = 0x02;
    extension[1] = 3;
    extension[2] = (uint8_t)(4 + length);
    memcpy(extension + 4, blocks, length);
    for (int b = 0; b < 2; b++)
    {
        uint8_t sum = 0;
        for (int i = 0; i < 127; i++)
        {
            sum = (uint8_t)(sum + bytes[b * 128 + i]);
        }
        bytes[b * 128 + 127] = (uint8_t)(0x100 - sum);
    }
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 0.01f * (b > 1.0f ? b : 1.0f);
}

int main(void)
{
    uint8_t bytes[256];
    mwinEdidHdr hdr;
    // A video block first, then HDR static metadata: SDR, HDR, PQ and
    // HLG; type 1; codes 115, 90 and 50.
    static const uint8_t hdrMonitor[] = {0x43, 0x10, 0x04, 0x03, 0xE6, 0x06,
                                         0x0F, 0x01, 115,  90,   50};
    Make(bytes, hdrMonitor, sizeof(hdrMonitor));
    float peak = 50.0f * exp2f(115.0f / 32.0f);
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && hdr.pq && hdr.hlg &&
              Near(hdr.peakNits, peak) &&
              Near(hdr.frameAverageNits, 50.0f * exp2f(90.0f / 32.0f)) &&
              Near(hdr.minimumNits, peak * (50.0f / 255.0f) * (50.0f / 255.0f) / 100.0f),
          "an HDR monitor's metadata");
    static const uint8_t bare[] = {0xE3, 0x06, 0x05, 0x01};
    Make(bytes, bare, sizeof(bare));
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && hdr.pq && !hdr.hlg && hdr.peakNits == 0.0f &&
              hdr.frameAverageNits == 0.0f && hdr.minimumNits == 0.0f,
          "transfer functions and no luminance");
    static const uint8_t zero[] = {0xE6, 0x06, 0x05, 0x01, 0, 0, 0};
    Make(bytes, zero, sizeof(zero));
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && hdr.peakNits == 0.0f &&
              hdr.minimumNits == 0.0f,
          "a code of 0 gives no luminance");
    static const uint8_t sdr[] = {0x43, 0x10, 0x04, 0x03};
    Make(bytes, sdr, sizeof(sdr));
    CHECK(!mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && !hdr.pq && hdr.peakNits == 0.0f,
          "none in an SDR monitor's EDID");
    Make(bytes, hdrMonitor, sizeof(hdrMonitor));
    bytes[128 + 20] ^= 1;
    CHECK(!mwinEdidHdrOf(bytes, sizeof(bytes), &hdr), "none in a block whose checksum fails");
    Make(bytes, hdrMonitor, sizeof(hdrMonitor));
    CHECK(!mwinEdidHdrOf(bytes, 128, &hdr) && !mwinEdidHdrOf(bytes, 200, &hdr),
          "none past the bytes there are");
    bytes[0] = 0x01;
    CHECK(!mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && !mwinEdidHdrOf(nullptr, 256, &hdr),
          "none in bytes that are no EDID");
    // A block claiming 31 bytes in a collection of 8.
    static const uint8_t overlong[] = {0xFF, 0x06, 0x05, 0x01, 1, 2, 3, 4};
    Make(bytes, overlong, sizeof(overlong));
    CHECK(!mwinEdidHdrOf(bytes, sizeof(bytes), &hdr), "none in a block past its collection");
    return s_failures == 0 ? 0 : 1;
}
