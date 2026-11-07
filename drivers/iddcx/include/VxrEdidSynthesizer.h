#ifndef VXR_EDID_SYNTHESIZER_H
#define VXR_EDID_SYNTHESIZER_H

#include "VxrIddTypes.h"
#include <vector>

namespace vxr {

/// VESA EDID 1.3/1.4 binary block synthesizer for Windows Indirect Display Drivers
class VxrEdidSynthesizer {
public:
    /// Synthesizes a valid 128-byte VESA EDID block for a virtual monitor
    static bool Synthesize(
        uint32_t monitor_id,
        uint32_t preferred_width,
        uint32_t preferred_height,
        uint32_t preferred_refresh,
        uint8_t out_edid[VXR_IDD_EDID_BLOCK_SIZE]
    );

    /// Validates an EDID binary block (checks VESA magic header and checksum modulo 256)
    static bool Validate(const uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE]);

    /// Computes the VESA checksum byte ensuring sum(bytes[0..127]) % 256 == 0
    static uint8_t ComputeChecksum(const uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE]);

private:
    static void WriteDetailedTimingDescriptor(
        uint8_t* dtd,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_rate
    );

    static void WriteMonitorNameDescriptor(uint8_t* dtd, const char* name);
    static void WriteRangeLimitsDescriptor(uint8_t* dtd, uint32_t min_v, uint32_t max_v);
};

} // namespace vxr

#endif // VXR_EDID_SYNTHESIZER_H
