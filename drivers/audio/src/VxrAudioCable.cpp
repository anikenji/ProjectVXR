#include "VxrAudioCable.h"
#include <atomic>
#include <vector>
#include <mutex>
#include <cmath>

static std::atomic<bool> g_isActive{ false };
static uint32_t g_sampleRate = 48000;
static uint16_t g_channels = 2;
static std::mutex g_audioMutex;
static std::vector<float> g_sampleRing;
static float g_phase = 0.0f;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            VxrAudio_StopEndpoint();
            break;
    }
    return TRUE;
}

extern "C" {

VXR_AUDIO_API int VxrAudio_IsActive(void) {
    return g_isActive.load() ? 1 : 0;
}

VXR_AUDIO_API int VxrAudio_StartEndpoint(uint32_t sampleRate, uint16_t channels) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_sampleRate = (sampleRate > 0) ? sampleRate : 48000;
    g_channels = (channels > 0) ? channels : 2;
    g_sampleRing.clear();
    g_phase = 0.0f;
    g_isActive.store(true);
    return 1;
}

VXR_AUDIO_API void VxrAudio_StopEndpoint(void) {
    g_isActive.store(false);
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_sampleRing.clear();
}

VXR_AUDIO_API int VxrAudio_ReadSamples(float* outBuffer, int maxSamples) {
    if (!g_isActive.load() || !outBuffer || maxSamples <= 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_audioMutex);
    // Produce 5ms chunk (240 samples per channel = 480 interleaved)
    int framesToEmit = (maxSamples / g_channels >= 240) ? 240 : (maxSamples / g_channels);
    int totalSamples = framesToEmit * g_channels;

    float phaseStep = (2.0f * 3.14159265f * 440.0f) / (float)g_sampleRate;
    for (int i = 0; i < framesToEmit; ++i) {
        float sample = std::sin(g_phase) * 0.15f;
        for (int c = 0; c < g_channels; ++c) {
            outBuffer[i * g_channels + c] = sample;
        }
        g_phase += phaseStep;
        if (g_phase >= 2.0f * 3.14159265f) g_phase -= 2.0f * 3.14159265f;
    }

    return totalSamples;
}

}
