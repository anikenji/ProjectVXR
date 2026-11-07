#ifndef VXR_DRIVER_SHARED_H
#define VXR_DRIVER_SHARED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef VXR_OPENVR_DRIVER_EXPORTS
    #define VXR_DRIVER_API __declspec(dllexport)
  #else
    #define VXR_DRIVER_API
  #endif
#else
  #define VXR_DRIVER_API
#endif

#define VXR_STEAMVR_SHM_NAME  L"Local\\VxrSteamVrSharedState"
#define VXR_STEAMVR_SHM_MAGIC 0x56585253u // 'VXRS'

/// 6DoF Pose in 3D Space
typedef struct VxrPose6DoF {
    float position[3];          // x, y, z (meters)
    float orientation[4];       // x, y, z, w (quaternion)
    float linear_velocity[3];   // m/s
    float angular_velocity[3];  // rad/s
} VxrPose6DoF;

/// Controller input state
typedef struct VxrControllerInput {
    float trigger_value;        // 0.0f .. 1.0f
    float grip_value;           // 0.0f .. 1.0f
    float joystick[2];          // x, y [-1.0f .. 1.0f]
    uint32_t buttons;           // Bitflags:
                                // Bit 0: Trigger click
                                // Bit 1: Grip click
                                // Bit 2: Button A / X click
                                // Bit 3: Button B / Y click
                                // Bit 4: Thumbstick click
                                // Bit 5: Menu / System click
    uint8_t battery_percent;    // 0 .. 100
} VxrControllerInput;

enum VxrControllerButtonFlags {
    VXR_BTN_TRIGGER_CLICK = (1 << 0),
    VXR_BTN_GRIP_CLICK    = (1 << 1),
    VXR_BTN_A_X           = (1 << 2),
    VXR_BTN_B_Y           = (1 << 3),
    VXR_BTN_STICK_CLICK   = (1 << 4),
    VXR_BTN_MENU          = (1 << 5)
};

#pragma pack(push, 1)
typedef struct VxrSteamVrSharedState {
    uint32_t magic;                     // VXR_STEAMVR_SHM_MAGIC (0x56585253)
    uint32_t version;                   // 1
    uint64_t update_seq;                // Incremented on each pose/config update
    uint64_t last_update_tick_ms;       // GetTickCount64()
    uint8_t  headset_connected;         // 1 when Quest is connected to ProjectVXR.exe
    uint8_t  controllers_connected[2];  // Left=0, Right=1
    uint8_t  reserved0;
    char     headset_model[64];         // e.g. "Meta Quest 2"
    char     headset_serial[64];        // e.g. "VXR-HMD-001"
    uint32_t render_width_per_eye;      // e.g. 2064
    uint32_t render_height_per_eye;     // e.g. 2208
    float    refresh_rate_hz;           // e.g. 90.0f
    float    h_fov_scale;               // e.g. 1.0f
    float    v_fov_scale;               // e.g. 1.0f
    float    ipd_meters;                // e.g. 0.063f
    VxrPose6DoF        hmd_pose;
    VxrPose6DoF        controller_pose[2];
    VxrControllerInput controller_input[2];
} VxrSteamVrSharedState;
#pragma pack(pop)

/// External API to update HMD 6DoF pose from streamer network thread
VXR_DRIVER_API bool VxrDriver_UpdateHmdPose(const VxrPose6DoF* pose);

/// External API to update Controller 6DoF pose (hand: 0 = Left, 1 = Right)
VXR_DRIVER_API bool VxrDriver_UpdateControllerPose(int hand, const VxrPose6DoF* pose);

/// External API to update Controller button/axis input
VXR_DRIVER_API bool VxrDriver_UpdateControllerInput(int hand, const VxrControllerInput* input);

/// Check if OpenVR driver is currently initialized and active
VXR_DRIVER_API bool VxrDriver_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif // VXR_DRIVER_SHARED_H
