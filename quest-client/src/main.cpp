#include "VxrQuestApp.h"
#include <iostream>

#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#include <android/log.h>

void android_main(struct android_app* app) {
    __android_log_print(ANDROID_LOG_INFO, "VXR-Client", "Starting Project VXR NativeActivity (OpenXR + Vulkan 1.3 + 7-Tab VR Menu)...");

    vxr::QuestAppConfig config;
    config.preferred_codec = vxr::VideoCodecType::Hevc;
    config.stream_width = 2496;
    config.stream_height = 2592;
    config.target_fps = 90;
    config.enable_zero_copy = true;
    config.enable_space_warp = true;
    config.enable_foveation = true;

    vxr::VxrQuestApp questApp;
    if (!questApp.Initialize(config)) {
        __android_log_print(ANDROID_LOG_ERROR, "VXR-Client", "Failed to initialize Quest client app!");
        return;
    }

    // Attempt initial discovery broadcast on LAN
    questApp.GetNetwork().SendDiscoverAndConnect("255.255.255.255", 38810);

    while (app->destroyRequested == 0) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) break;
        }

        if (questApp.GetVrMenu().GetSettings().quit_requested) {
            break;
        }

        questApp.RunFrame();
    }

    questApp.Shutdown();
}

#endif
