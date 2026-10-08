// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A monitor's HDR static metadata from its EDID: an HDR monitor's
// block read (PQ and HLG, the most, the frame average and the least);
// a block with only its transfer functions, or cut after the most or
// the frame average, reading nothing of the block after it; none in an
// SDR monitor's EDID, in an extension that is no CTA-861 one, in a block
// whose checksum fails, in bytes that are no EDID, past the bytes there
// are, or in a data block longer than its collection. The image's size:
// the preferred timing's millimeters, the base block's centimeters where
// the timing's stray from them (a timing in centimeters) or there is
// none, either where the other is missing, and none where neither is.

#include "edid.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

// Makes both blocks' checksums right.
static void Sum(uint8_t* bytes)
{
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
    if (length > 0)
    {
        memcpy(extension + 4, blocks, length);
    }
    Sum(bytes);
}

// An EDID whose base block gives a size in centimeters and a preferred
// timing (pixel clock 148.5 MHz) whose image is the size in millimeters
// given, 0 for no timing.
static void MakeSized(uint8_t* bytes, uint8_t widthCm, uint8_t heightCm, uint32_t widthMm,
                      uint32_t heightMm)
{
    Make(bytes, nullptr, 0);
    bytes[21] = widthCm;
    bytes[22] = heightCm;
    uint8_t* timing = bytes + 54;
    if (widthMm != 0)
    {
        timing[0] = 0x02;
        timing[1] = 0x3A;
        timing[12] = (uint8_t)widthMm;
        timing[13] = (uint8_t)heightMm;
        timing[14] = (uint8_t)((widthMm >> 8) << 4 | (heightMm >> 8));
    }
    Sum(bytes);
}

static void TestSize(void)
{
    uint8_t bytes[256];
    uint32_t width = 0;
    uint32_t height = 0;
    MakeSized(bytes, 60, 34, 597, 336);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 597 && height == 336,
          "the preferred timing's millimeters");
    MakeSized(bytes, 60, 34, 60, 34);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 600 && height == 340,
          "a timing in centimeters gives way to the base block's");
    MakeSized(bytes, 60, 34, 597, 260);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 600 && height == 340,
          "a timing whose height strays gives way too");
    MakeSized(bytes, 50, 30, 600, 240);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 600 && height == 240,
          "a timing exactly a fifth off each way still counts");
    MakeSized(bytes, 60, 34, 597, 336);
    bytes[54] = 0x10;
    bytes[55] = 0x00;
    Sum(bytes);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 597 && height == 336,
          "a timing whose pixel clock is under 2.56 MHz is a timing");
    // A display descriptor (pixel clock 0, a monitor name) whose bytes
    // 12 to 14 would read as a size near the base block's.
    MakeSized(bytes, 60, 34, 0, 0);
    bytes[54 + 3] = 0xFC;
    bytes[54 + 12] = 0x55;
    bytes[54 + 13] = 0x30;
    bytes[54 + 14] = 0x21;
    Sum(bytes);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 600 && height == 340,
          "a display descriptor at byte 54 is no timing");
    MakeSized(bytes, 52, 29, 0, 0);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 520 && height == 290,
          "the base block's centimeters without a timing");
    MakeSized(bytes, 0, 79, 1209, 680);
    CHECK(mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 1209 && height == 680,
          "the timing's where the base block gives an aspect ratio");
    MakeSized(bytes, 0, 0, 0, 0);
    CHECK(!mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 0 && height == 0,
          "none where neither gives it");
    MakeSized(bytes, 60, 34, 597, 336);
    bytes[0] = 0x01;
    CHECK(!mwinEdidSizeOf(bytes, sizeof(bytes), &width, &height) && width == 0 &&
              !mwinEdidSizeOf(bytes, 100, &width, &height),
          "none from bytes that are no EDID");
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
    // Each short block is followed by a video block, whose bytes are no
    // luminance codes of the block before it.
    static const uint8_t bare[] = {0xE3, 0x06, 0x05, 0x01, 0x43, 0x10, 0x04, 0x03};
    Make(bytes, bare, sizeof(bare));
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && hdr.pq && !hdr.hlg && hdr.peakNits == 0.0f &&
              hdr.frameAverageNits == 0.0f && hdr.minimumNits == 0.0f,
          "transfer functions and no luminance");
    static const uint8_t mostOnly[] = {0xE4, 0x06, 0x05, 0x01, 115, 0x43, 0x10, 0x04, 0x03};
    Make(bytes, mostOnly, sizeof(mostOnly));
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && Near(hdr.peakNits, peak) &&
              hdr.frameAverageNits == 0.0f && hdr.minimumNits == 0.0f,
          "the most alone");
    static const uint8_t noLeast[] = {0xE5, 0x06, 0x05, 0x01, 115, 90, 0x43, 0x10, 0x04, 0x03};
    Make(bytes, noLeast, sizeof(noLeast));
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && Near(hdr.peakNits, peak) &&
              hdr.frameAverageNits > 0.0f && hdr.minimumNits == 0.0f,
          "the most and the frame average, no least");
    static const uint8_t zero[] = {0xE6, 0x06, 0x05, 0x01, 0, 0, 0};
    Make(bytes, zero, sizeof(zero));
    CHECK(mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && hdr.peakNits == 0.0f &&
              hdr.minimumNits == 0.0f,
          "a code of 0 gives no luminance");
    static const uint8_t sdr[] = {0x43, 0x10, 0x04, 0x03};
    Make(bytes, sdr, sizeof(sdr));
    CHECK(!mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && !hdr.pq && hdr.peakNits == 0.0f,
          "none in an SDR monitor's EDID");
    // A DisplayID extension holding the same bytes.
    Make(bytes, hdrMonitor, sizeof(hdrMonitor));
    bytes[128] = 0x70;
    Sum(bytes);
    CHECK(!mwinEdidHdrOf(bytes, sizeof(bytes), &hdr) && hdr.peakNits == 0.0f,
          "none in an extension that is no CTA-861 one");
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
    TestSize();
    return s_failures == 0 ? 0 : 1;
}
