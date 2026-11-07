#include "VxrEdidSynthesizer.h"
#include <cstring>
#include <cstdio>
#include <numeric>

namespace vxr {

uint8_t VxrEdidSynthesizer::ComputeChecksum(const uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE]) {
    uint32_t sum = 0;
    for (int i = 0; i < VXR_IDD_EDID_BLOCK_SIZE - 1; ++i) {
        sum += edid[i];
    }
    return static_cast<uint8_t>((256 - (sum % 256)) % 256);
}

bool VxrEdidSynthesizer::Validate(const uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE]) {
    if (!edid) return false;

    // Check VESA Magic Header: 00 FF FF FF FF FF FF 00
    const uint8_t vesaHeader[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    if (memcmp(edid, vesaHeader, 8) != 0) {
        return false;
    }

    // Checksum verification: sum of all 128 bytes must be 0 mod 256
    uint32_t total = 0;
    for (int i = 0; i < VXR_IDD_EDID_BLOCK_SIZE; ++i) {
        total += edid[i];
    }

    return (total % 256) == 0;
}

void VxrEdidSynthesizer::WriteDetailedTimingDescriptor(
    uint8_t* dtd,
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate
) {
    memset(dtd, 0, 18);

    // Approximate CVT-RB (reduced blanking) timing calculation
    uint32_t h_blank = 160;
    uint32_t v_blank = 45;
    uint32_t h_total = width + h_blank;
    uint32_t v_total = height + v_blank;

    uint64_t pixel_clock_hz = static_cast<uint64_t>(h_total) * v_total * refresh_rate;
    uint16_t pixel_clock_10khz = static_cast<uint16_t>(pixel_clock_hz / 10000ULL);

    // Bytes 0-1: Pixel clock in 10kHz units (Little Endian)
    dtd[0] = static_cast<uint8_t>(pixel_clock_10khz & 0xFF);
    dtd[1] = static_cast<uint8_t>((pixel_clock_10khz >> 8) & 0xFF);

    // Byte 2: H active lower 8 bits
    dtd[2] = static_cast<uint8_t>(width & 0xFF);
    // Byte 3: H blank lower 8 bits
    dtd[3] = static_cast<uint8_t>(h_blank & 0xFF);
    // Byte 4: H active & blank upper 4 bits
    dtd[4] = static_cast<uint8_t>(((width >> 8) & 0x0F) << 4 | ((h_blank >> 8) & 0x0F));

    // Byte 5: V active lower 8 bits
    dtd[5] = static_cast<uint8_t>(height & 0xFF);
    // Byte 6: V blank lower 8 bits
    dtd[6] = static_cast<uint8_t>(v_blank & 0xFF);
    // Byte 7: V active & blank upper 4 bits
    dtd[7] = static_cast<uint8_t>(((height >> 8) & 0x0F) << 4 | ((v_blank >> 8) & 0x0F));

    // Bytes 8-11: Sync offsets & pulse widths
    dtd[8] = 48; // H front porch lower 8 bits
    dtd[9] = 32; // H sync pulse lower 8 bits
    dtd[10] = static_cast<uint8_t>((3 & 0x0F) << 4 | (5 & 0x0F)); // V sync
    dtd[11] = 0x00;

    // Bytes 12-14: Image size in mm (600mm x 340mm)
    dtd[12] = static_cast<uint8_t>(600 & 0xFF);
    dtd[13] = static_cast<uint8_t>(340 & 0xFF);
    dtd[14] = static_cast<uint8_t>(((600 >> 8) & 0x0F) << 4 | ((340 >> 8) & 0x0F));

    // Byte 17: Flags: Digital separate sync, non-interlaced
    dtd[17] = 0x1E;
}

void VxrEdidSynthesizer::WriteMonitorNameDescriptor(uint8_t* dtd, const char* name) {
    memset(dtd, 0, 18);
    // Descriptor header: 0x00, 0x00, 0x00, 0xFC (Display Name), 0x00
    dtd[0] = 0x00;
    dtd[1] = 0x00;
    dtd[2] = 0x00;
    dtd[3] = 0xFC;
    dtd[4] = 0x00;

    size_t len = strlen(name);
    for (size_t i = 0; i < 13; ++i) {
        if (i < len) {
            dtd[5 + i] = static_cast<uint8_t>(name[i]);
        } else if (i == len) {
            dtd[5 + i] = 0x0A; // newline terminator
        } else {
            dtd[5 + i] = 0x20; // space fill
        }
    }
}

void VxrEdidSynthesizer::WriteRangeLimitsDescriptor(uint8_t* dtd, uint32_t min_v, uint32_t max_v) {
    memset(dtd, 0, 18);
    // Descriptor header: 0x00, 0x00, 0x00, 0xFD (Range Limits), 0x00
    dtd[0] = 0x00;
    dtd[1] = 0x00;
    dtd[2] = 0x00;
    dtd[3] = 0xFD;
    dtd[4] = 0x00;

    dtd[5] = static_cast<uint8_t>(min_v); // Min vertical rate (48 Hz)
    dtd[6] = static_cast<uint8_t>(max_v); // Max vertical rate (120 Hz)
    dtd[7] = 30;                          // Min horizontal rate in kHz
    dtd[8] = 255;                         // Max horizontal rate in kHz
    dtd[9] = 60;                          // Max pixel clock in 10MHz (600 MHz)
    dtd[10] = 0x00;                       // Extended timing formula: default
}

bool VxrEdidSynthesizer::Synthesize(
    uint32_t monitor_id,
    uint32_t preferred_width,
    uint32_t preferred_height,
    uint32_t preferred_refresh,
    uint8_t out_edid[VXR_IDD_EDID_BLOCK_SIZE]
) {
    if (!out_edid) return false;
    memset(out_edid, 0, VXR_IDD_EDID_BLOCK_SIZE);

    // 1. VESA Header (0..7)
    const uint8_t vesaHeader[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memcpy(&out_edid[0], vesaHeader, 8);

    // 2. Vendor ID: "VXR" -> 0x5B, 0x12 (8..9)
    out_edid[8] = 0x5B;
    out_edid[9] = 0x12;

    // 3. Product ID (10..11)
    out_edid[10] = static_cast<uint8_t>(monitor_id & 0xFF);
    out_edid[11] = static_cast<uint8_t>((monitor_id >> 8) & 0xFF);

    // 4. Serial Number (12..15)
    uint32_t serial = 1000 + monitor_id;
    out_edid[12] = static_cast<uint8_t>(serial & 0xFF);
    out_edid[13] = static_cast<uint8_t>((serial >> 8) & 0xFF);
    out_edid[14] = static_cast<uint8_t>((serial >> 16) & 0xFF);
    out_edid[15] = static_cast<uint8_t>((serial >> 24) & 0xFF);

    // 5. Week & Year of Manufacture (16..17)
    out_edid[16] = 45; // Week 45
    out_edid[17] = 36; // 1990 + 36 = 2026

    // 6. EDID Version 1.4 (18..19)
    out_edid[18] = 1;
    out_edid[19] = 4;

    // 7. Video Input Definition: Digital, 8-bit depth, DisplayPort (20)
    out_edid[20] = 0xA5;

    // 8. Horizontal & Vertical Screen Size in cm (21..22)
    out_edid[21] = 60; // 60 cm width
    out_edid[22] = 34; // 34 cm height

    // 9. Display Gamma: 2.20 (23) -> value = (gamma * 100) - 100 = 120
    out_edid[23] = 120;

    // 10. Feature Support: sRGB, Preferred Timing in DTD1 (24)
    out_edid[24] = 0x02;

    // 11. Color Characteristics: Standard sRGB coordinates (25..34)
    out_edid[25] = 0xEE; out_edid[26] = 0x91;
    out_edid[27] = 0xA3; out_edid[28] = 0x54;
    out_edid[29] = 0x4C; out_edid[30] = 0x99;
    out_edid[31] = 0x26; out_edid[32] = 0x0F;
    out_edid[33] = 0x50; out_edid[34] = 0x54;

    // 12. Established Timings: 640x480, 800x600, 1024x768 (35..37)
    out_edid[35] = 0x21;
    out_edid[36] = 0x08;
    out_edid[37] = 0x00;

    // 13. Standard Timings (38..53): 1080p, 1440p, 4K
    // Byte 0: (X res / 8) - 31, Byte 1: aspect ratio (2 bits) | refresh rate (6 bits - 60)
    // 1920x1080 @ 60Hz: (1920/8)-31 = 209 (0xD1), 16:9 aspect = (11 << 6) | 0 = 0xC0
    out_edid[38] = 0xD1; out_edid[39] = 0xC0; // 1920x1080 @ 60Hz
    out_edid[40] = 0xD1; out_edid[41] = 0xFC; // 1920x1080 @ 120Hz
    out_edid[42] = 0x01; out_edid[43] = 0x01; // unused standard timing slots

    // 14. Detailed Timing Descriptors (54..125)
    // DTD 1: Preferred Timing (e.g. 1920x1080 @ 120Hz or 4K @ 120Hz)
    WriteDetailedTimingDescriptor(&out_edid[54], preferred_width, preferred_height, preferred_refresh);

    // DTD 2: Secondary Standard Mode (2560x1440 @ 120Hz)
    WriteDetailedTimingDescriptor(&out_edid[72], 2560, 1440, 120);

    // DTD 3: Display Range Limits (48 - 120 Hz)
    WriteRangeLimitsDescriptor(&out_edid[90], 48, 120);

    // DTD 4: Display Name String
    char monitorName[32];
    snprintf(monitorName, sizeof(monitorName), "VXR-VDA-0%u", monitor_id);
    WriteMonitorNameDescriptor(&out_edid[108], monitorName);

    // 15. Extension Flag (126) = 0
    out_edid[126] = 0;

    // 16. Checksum (127)
    out_edid[127] = ComputeChecksum(out_edid);

    return Validate(out_edid);
}

} // namespace vxr
