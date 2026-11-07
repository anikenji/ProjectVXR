#include "VxrQuestApp.h"
#include <iostream>

#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <unistd.h>
#include <cstring>

struct AndroidAppContext {
    vxr::VxrQuestApp questApp;
    bool windowReady = false;
    int32_t windowWidth = 1280;
    int32_t windowHeight = 800;
};

static void HandleAppCmd(struct android_app* app, int32_t cmd) {
    auto* ctx = static_cast<AndroidAppContext*>(app->userData);
    if (!ctx) return;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                if (!ctx->questApp.GetOpenXr().IsRealRuntimeActive()) {
                    ANativeWindow_setBuffersGeometry(app->window, 1280, 800, WINDOW_FORMAT_RGBA_8888);
                }
                ctx->windowWidth = ANativeWindow_getWidth(app->window);
                ctx->windowHeight = ANativeWindow_getHeight(app->window);
                if (ctx->windowWidth <= 0) ctx->windowWidth = 1280;
                if (ctx->windowHeight <= 0) ctx->windowHeight = 800;
                ctx->windowReady = true;
                __android_log_print(ANDROID_LOG_INFO, "VXR-Client",
                    "APP_CMD_INIT_WINDOW ready (RealOpenXR=%d)",
                    ctx->questApp.GetOpenXr().IsRealRuntimeActive() ? 1 : 0);
            }
            break;

        case APP_CMD_TERM_WINDOW:
            ctx->windowReady = false;
            break;

        default:
            break;
    }
}

static int32_t HandleInputEvent(struct android_app* app, AInputEvent* event) {
    auto* ctx = static_cast<AndroidAppContext*>(app->userData);
    if (!ctx || !event) return 0;

    int32_t eventType = AInputEvent_getType(event);
    if (eventType == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);

        int32_t w = (app->window != nullptr) ? ANativeWindow_getWidth(app->window) : ctx->windowWidth;
        int32_t h = (app->window != nullptr) ? ANativeWindow_getHeight(app->window) : ctx->windowHeight;
        if (w <= 0) w = 1280;
        if (h <= 0) h = 800;

        float normX = x / static_cast<float>(w);
        float normY = y / static_cast<float>(h);

        bool isDown = (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_MOVE);
        bool isClick = (action == AMOTION_EVENT_ACTION_DOWN);

        ctx->questApp.OnSurfacePointerEvent(normX, normY, isDown, isClick);
        return 1;
    } else if (eventType == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        if (action == AKEY_EVENT_ACTION_DOWN) {
            if (keyCode == AKEYCODE_BACK || keyCode == AKEYCODE_MENU || keyCode == AKEYCODE_BUTTON_START) {
                ctx->questApp.GetVrMenu().ToggleVisible();
                return 1;
            }
        }
    }
    return 0;
}

static void PresentRgbaToNativeWindow(ANativeWindow* window, const uint32_t* srcRgba, uint32_t srcW, uint32_t srcH) {
    if (!window || !srcRgba || srcW == 0 || srcH == 0) return;

    ANativeWindow_Buffer buffer = {};
    if (ANativeWindow_lock(window, &buffer, nullptr) != 0 || buffer.bits == nullptr) {
        return;
    }

    uint32_t dstW = (buffer.width > 0) ? static_cast<uint32_t>(buffer.width) : srcW;
    uint32_t dstH = (buffer.height > 0) ? static_cast<uint32_t>(buffer.height) : srcH;
    uint32_t dstStride = (buffer.stride > 0) ? static_cast<uint32_t>(buffer.stride) : dstW;
    uint32_t* dstPixels = static_cast<uint32_t*>(buffer.bits);

    if (dstW == srcW && dstH == srcH) {
        for (uint32_t y = 0; y < srcH; ++y) {
            std::memcpy(dstPixels + y * dstStride, srcRgba + y * srcW, srcW * sizeof(uint32_t));
        }
    } else {
        for (uint32_t y = 0; y < dstH; ++y) {
            uint32_t sy = (y * srcH) / dstH;
            if (sy >= srcH) sy = srcH - 1;
            const uint32_t* srcRow = srcRgba + sy * srcW;
            uint32_t* dstRow = dstPixels + y * dstStride;
            for (uint32_t x = 0; x < dstW; ++x) {
                uint32_t sx = (x * srcW) / dstW;
                if (sx >= srcW) sx = srcW - 1;
                dstRow[x] = srcRow[sx];
            }
        }
    }

    ANativeWindow_unlockAndPost(window);
}

void android_main(struct android_app* app) {
    __android_log_print(ANDROID_LOG_INFO, "VXR-Client",
        "Starting Project VXR NativeActivity (Real OpenXR + 7-Tab VR Menu + Live Desktop Streamer)...");

    AndroidAppContext ctx;
    app->userData = &ctx;
    app->onAppCmd = HandleAppCmd;
    app->onInputEvent = HandleInputEvent;

    if (app->activity != nullptr) {
        ctx.questApp.GetOpenXr().SetAndroidAppContext(app->activity->vm, app->activity->clazz);
    }

    vxr::QuestAppConfig config;
    config.preferred_codec = vxr::VideoCodecType::Hevc;
    config.stream_width = 2496;
    config.stream_height = 2592;
    config.target_fps = 90;
    config.enable_zero_copy = true;
    config.enable_space_warp = true;
    config.enable_foveation = true;

    if (!ctx.questApp.Initialize(config)) {
        __android_log_print(ANDROID_LOG_ERROR, "VXR-Client", "Failed to initialize Quest client app!");
        return;
    }

    // Attempt initial discovery broadcast on LAN
    ctx.questApp.GetNetwork().SendDiscoverAndConnect("255.255.255.255", 38810);

    while (app->destroyRequested == 0) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) break;
        }

        if (app->destroyRequested != 0) break;

        if (ctx.questApp.GetVrMenu().GetSettings().quit_requested) {
            ANativeActivity_finish(app->activity);
            break;
        }

        ctx.questApp.RunFrame();

        // If running on a non-VR 2D device where OpenXR runtime wasn't active, present directly to ANativeWindow
        if (!ctx.questApp.GetOpenXr().IsRealRuntimeActive()) {
            if (ctx.windowReady && app->window != nullptr) {
                uint32_t fw = 0, fh = 0;
                const uint32_t* rgba = ctx.questApp.GetCompositedRgbaFrame(&fw, &fh);
                PresentRgbaToNativeWindow(app->window, rgba, fw, fh);
            }
            usleep(11000);
        }
    }

    ctx.questApp.Shutdown();
}

#endif
