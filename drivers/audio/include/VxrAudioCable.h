#pragma once

#include <windows.h>
#include <stdint.h>

#ifdef VXR_AUDIO_EXPORTS
#define VXR_AUDIO_API __declspec(dllexport)
#else
#define VXR_AUDIO_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 if virtual audio endpoint is currently active, 0 otherwise
VXR_AUDIO_API int VxrAudio_IsActive(void);

// Starts virtual audio loopback capture endpoint
VXR_AUDIO_API int VxrAudio_StartEndpoint(uint32_t sampleRate, uint16_t channels);

// Stops virtual audio loopback endpoint
VXR_AUDIO_API void VxrAudio_StopEndpoint(void);

// Read interleaved PCM float samples (returns sample count)
VXR_AUDIO_API int VxrAudio_ReadSamples(float* outBuffer, int maxSamples);

#ifdef __cplusplus
}
#endif
