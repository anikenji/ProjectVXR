#include "VxrQuestApp.h"
#include <iostream>

#if defined(__ANDROID__)
#include <android_native_app_glue.h>

void android_main(struct android_app* app) {
    std::cout << "[VXR-Client] Starting Project VXR NativeActivity..." << std::endl;

    vxr::QuestAppConfig config;
    config.preferred_codec = vxr::VideoCodecType::Hevc;
    config.stream_width = 2064;
    config.stream_height = 2208;
    config.target_fps = 90;
    config.enable_zero_copy = true;
    config.enable_space_warp = true;
    config.enable_foveation = true;

    vxr::VxrQuestApp questApp;
    if (!questApp.Initialize(config)) {
        std::cerr << "[VXR-Client] Failed to initialize Quest client app!" << std::endl;
        return;
    }

    while (app->destroyRequested == 0) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
        }

        questApp.RunFrame();
    }

    questApp.Shutdown();
}

#endif
