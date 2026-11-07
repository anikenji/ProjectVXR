#include "VxrOpenXrClient.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "VXR-OpenXR", __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, "VXR-OpenXR", __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VXR-OpenXR", __VA_ARGS__)
#endif

namespace vxr {

VxrOpenXrClient::VxrOpenXrClient()
    : m_initialized(false)
    , m_sessionRunning(false)
    , m_realXrActive(false)
    , m_shouldRenderFrame(true)
    , m_sessionState(OpenXrSessionState::Unknown)
    , m_frameIndex(0)
    , m_lastPredictedDisplayTimeNs(0)
    , m_javaVm(nullptr)
    , m_activityClazz(nullptr)
    , m_loaderHandle(nullptr)
    , m_pfnGetInstanceProcAddr(nullptr)
    , m_instance(nullptr)
    , m_systemId(nullptr)
    , m_session(nullptr)
    , m_localSpace(nullptr)
    , m_viewSpace(nullptr)
    , m_eglDisplay(nullptr)
    , m_eglConfig(nullptr)
    , m_eglContext(nullptr)
    , m_eglSurface(nullptr)
    , m_quadSwapchain(nullptr)
    , m_skySwapchain(nullptr)
    , m_quadDistM(2.0f)
    , m_quadWidthM(2.2f)
    , m_quadHeightM(1.375f)
    , m_actionSet(nullptr)
    , m_aimPoseAction(nullptr)
    , m_triggerAction(nullptr)
    , m_squeezeAction(nullptr)
    , m_thumbstickAction(nullptr)
    , m_buttonAAction(nullptr)
    , m_buttonBAction(nullptr)
    , m_menuAction(nullptr)
    , m_rightAimSpace(nullptr)
    , m_leftAimSpace(nullptr)
{
    m_handPaths[0] = 0;
    m_handPaths[1] = 0;
    std::memset(m_cachedViews, 0, sizeof(m_cachedViews));
    std::memset(&m_controllerState, 0, sizeof(m_controllerState));
    m_controllerState.ray_origin[0] = 0.0f;
    m_controllerState.ray_origin[1] = 1.50f;
    m_controllerState.ray_origin[2] = 0.0f;
    m_controllerState.ray_direction[0] = 0.0f;
    m_controllerState.ray_direction[1] = 0.0f;
    m_controllerState.ray_direction[2] = -1.0f;
    m_controllerState.is_active = true;
}

VxrOpenXrClient::~VxrOpenXrClient() {
    Cleanup();
}

void VxrOpenXrClient::SetAndroidAppContext(void* javaVm, void* activityClazz) {
    m_javaVm = javaVm;
    m_activityClazz = activityClazz;
}

bool VxrOpenXrClient::Initialize(const OpenXrClientConfig& config) {
    Cleanup();
    m_config = config;

#if defined(__ANDROID__)
    if (m_javaVm != nullptr && m_activityClazz != nullptr) {
        ALOGI("Initializing real Meta Quest OpenXR Loader & Runtime...");
        PFN_xrInitializeLoaderKHR pfnInitLoader = nullptr;
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                               reinterpret_cast<PFN_xrVoidFunction*>(&pfnInitLoader))) && pfnInitLoader) {
            XrLoaderInitInfoAndroidKHR loaderInit = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
            loaderInit.applicationVM = m_javaVm;
            loaderInit.applicationContext = m_activityClazz;
            XrResult lr = pfnInitLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInit));
            ALOGI("xrInitializeLoaderKHR returned %d", (int)lr);
        }

        const char* enabledExts[] = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
        };

        XrInstanceCreateInfoAndroidKHR androidCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
        androidCreateInfo.applicationVM = m_javaVm;
        androidCreateInfo.applicationActivity = m_activityClazz;

        XrInstanceCreateInfo instInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
        instInfo.next = &androidCreateInfo;
        std::strncpy(instInfo.applicationInfo.applicationName, config.app_name.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
        instInfo.applicationInfo.applicationVersion = 1;
        std::strncpy(instInfo.applicationInfo.engineName, "ProjectVXR", XR_MAX_ENGINE_NAME_SIZE - 1);
        instInfo.applicationInfo.engineVersion = 1;
        instInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
        instInfo.enabledExtensionCount = 2;
        instInfo.enabledExtensionNames = enabledExts;

        XrInstance xrInst = XR_NULL_HANDLE;
        XrResult res = xrCreateInstance(&instInfo, &xrInst);
        if (XR_SUCCEEDED(res) && xrInst != XR_NULL_HANDLE) {
            ALOGI("xrCreateInstance succeeded: %p", (void*)xrInst);
            m_instance = reinterpret_cast<void*>(xrInst);

            XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
            sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
            XrSystemId sysId = XR_NULL_SYSTEM_ID;
            res = xrGetSystem(xrInst, &sysInfo, &sysId);
            if (XR_SUCCEEDED(res)) {
                m_systemId = reinterpret_cast<void*>(static_cast<uintptr_t>(sysId));

                // Query OpenGLES graphics requirements (mandatory before xrCreateSession)
                PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetGlesReqs = nullptr;
                xrGetInstanceProcAddr(xrInst, "xrGetOpenGLESGraphicsRequirementsKHR",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetGlesReqs));
                if (pfnGetGlesReqs) {
                    XrGraphicsRequirementsOpenGLESKHR glesReqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
                    pfnGetGlesReqs(xrInst, sysId, &glesReqs);
                }

                // Initialize EGL 1.4 + OpenGL ES 3.0 Context for OpenXR Swapchain rendering
                EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                EGLint major = 0, minor = 0;
                eglInitialize(display, &major, &minor);

                const EGLint configAttribs[] = {
                    EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8,
                    EGL_BLUE_SIZE, 8,
                    EGL_ALPHA_SIZE, 8,
                    EGL_DEPTH_SIZE, 0,
                    EGL_STENCIL_SIZE, 0,
                    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                    EGL_NONE
                };
                EGLConfig eglCfg = nullptr;
                EGLint numConfigs = 0;
                eglChooseConfig(display, configAttribs, &eglCfg, 1, &numConfigs);

                const EGLint ctxAttribs[] = {
                    EGL_CONTEXT_CLIENT_VERSION, 3,
                    EGL_NONE
                };
                EGLContext eglCtx = eglCreateContext(display, eglCfg, EGL_NO_CONTEXT, ctxAttribs);

                const EGLint pbufAttribs[] = {
                    EGL_WIDTH, 16,
                    EGL_HEIGHT, 16,
                    EGL_NONE
                };
                EGLSurface eglSurf = eglCreatePbufferSurface(display, eglCfg, pbufAttribs);
                eglMakeCurrent(display, eglSurf, eglSurf, eglCtx);

                m_eglDisplay = display;
                m_eglConfig = eglCfg;
                m_eglContext = eglCtx;
                m_eglSurface = eglSurf;

                XrGraphicsBindingOpenGLESAndroidKHR gfxBinding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
                gfxBinding.display = display;
                gfxBinding.config = eglCfg;
                gfxBinding.context = eglCtx;

                XrSessionCreateInfo sessInfo = { XR_TYPE_SESSION_CREATE_INFO };
                sessInfo.next = &gfxBinding;
                sessInfo.systemId = sysId;

                XrSession xrSess = XR_NULL_HANDLE;
                res = xrCreateSession(xrInst, &sessInfo, &xrSess);
                if (XR_SUCCEEDED(res) && xrSess != XR_NULL_HANDLE) {
                    ALOGI("xrCreateSession succeeded: %p", (void*)xrSess);
                    m_session = reinterpret_cast<void*>(xrSess);

                    // Create LOCAL & VIEW reference spaces
                    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
                    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
                    spaceInfo.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
                    spaceInfo.poseInReferenceSpace.position = { 0.0f, 0.0f, 0.0f };
                    XrSpace localSp = XR_NULL_HANDLE;
                    xrCreateReferenceSpace(xrSess, &spaceInfo, &localSp);
                    m_localSpace = reinterpret_cast<void*>(localSp);

                    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
                    XrSpace viewSp = XR_NULL_HANDLE;
                    xrCreateReferenceSpace(xrSess, &spaceInfo, &viewSp);
                    m_viewSpace = reinterpret_cast<void*>(viewSp);

                    // Choose RGBA8 / SRGB8_ALPHA8 swapchain format
                    uint32_t fmtCount = 0;
                    xrEnumerateSwapchainFormats(xrSess, 0, &fmtCount, nullptr);
                    std::vector<int64_t> formats(fmtCount, 0);
                    if (fmtCount > 0) {
                        xrEnumerateSwapchainFormats(xrSess, fmtCount, &fmtCount, formats.data());
                    }
                    int64_t chosenFormat = 0x8058; // GL_RGBA8
                    for (int64_t f : formats) {
                        if (f == 0x8058 || f == 0x8C43) { // GL_RGBA8 or GL_SRGB8_ALPHA8
                            chosenFormat = f;
                            if (f == 0x8058) break;
                        }
                    }

                    // 1. Create 1280x800 Quad Swapchain for Virtual Desktop Menu & Live Desktop Screen
                    XrSwapchainCreateInfo scInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
                    scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
                    scInfo.format = chosenFormat;
                    scInfo.sampleCount = 1;
                    scInfo.width = 1280;
                    scInfo.height = 800;
                    scInfo.faceCount = 1;
                    scInfo.arraySize = 1;
                    scInfo.mipCount = 1;

                    XrSwapchain quadSc = XR_NULL_HANDLE;
                    if (XR_SUCCEEDED(xrCreateSwapchain(xrSess, &scInfo, &quadSc))) {
                        m_quadSwapchain = reinterpret_cast<void*>(quadSc);
                        uint32_t imgCount = 0;
                        xrEnumerateSwapchainImages(quadSc, 0, &imgCount, nullptr);
                        std::vector<XrSwapchainImageOpenGLESKHR> glImgs(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
                        xrEnumerateSwapchainImages(quadSc, imgCount, &imgCount,
                                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(glImgs.data()));
                        m_quadSwapchainTexIds.clear();
                        for (const auto& img : glImgs) {
                            m_quadSwapchainTexIds.push_back(img.image);
                        }
                        ALOGI("Created 1280x800 OpenXR Quad Swapchain with %u GLES textures", imgCount);
                    }

                    // 2. Create 64x64 Environment Projection Swapchain for dark VR theater room backdrop
                    scInfo.width = 64;
                    scInfo.height = 64;
                    XrSwapchain skySc = XR_NULL_HANDLE;
                    if (XR_SUCCEEDED(xrCreateSwapchain(xrSess, &scInfo, &skySc))) {
                        m_skySwapchain = reinterpret_cast<void*>(skySc);
                        uint32_t imgCount = 0;
                        xrEnumerateSwapchainImages(skySc, 0, &imgCount, nullptr);
                        std::vector<XrSwapchainImageOpenGLESKHR> glImgs(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
                        xrEnumerateSwapchainImages(skySc, imgCount, &imgCount,
                                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(glImgs.data()));
                        m_skySwapchainTexIds.clear();
                        for (const auto& img : glImgs) {
                            m_skySwapchainTexIds.push_back(img.image);
                        }
                    }

                    // 3. Create 6DoF Oculus Touch Controller Action Set & Bindings
                    XrActionSetCreateInfo asInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
                    std::strcpy(asInfo.actionSetName, "vxr_actions");
                    std::strcpy(asInfo.localizedActionSetName, "Project VXR Actions");
                    asInfo.priority = 0;
                    XrActionSet actSet = XR_NULL_HANDLE;
                    if (XR_SUCCEEDED(xrCreateActionSet(xrInst, &asInfo, &actSet))) {
                        m_actionSet = reinterpret_cast<void*>(actSet);
                        xrStringToPath(xrInst, "/user/hand/left", &m_handPaths[0]);
                        xrStringToPath(xrInst, "/user/hand/right", &m_handPaths[1]);

                        auto createAct = [&](XrActionType type, const char* name, const char* locName) -> XrAction {
                            XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
                            aci.actionType = type;
                            std::strcpy(aci.actionName, name);
                            std::strcpy(aci.localizedActionName, locName);
                            aci.countSubactionPaths = 2;
                            aci.subactionPaths = m_handPaths;
                            XrAction act = XR_NULL_HANDLE;
                            xrCreateAction(actSet, &aci, &act);
                            return act;
                        };

                        XrAction aimAct   = createAct(XR_ACTION_TYPE_POSE_INPUT,     "aim_pose",   "Controller Aim Pose");
                        XrAction trigAct  = createAct(XR_ACTION_TYPE_FLOAT_INPUT,    "trigger",    "Trigger Pull");
                        XrAction sqzAct   = createAct(XR_ACTION_TYPE_FLOAT_INPUT,    "squeeze",    "Grip Squeeze");
                        XrAction thumbAct = createAct(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick");
                        XrAction btnAAct  = createAct(XR_ACTION_TYPE_BOOLEAN_INPUT,  "button_a",   "Button A / X");
                        XrAction btnBAct  = createAct(XR_ACTION_TYPE_BOOLEAN_INPUT,  "button_b",   "Button B / Y");
                        XrAction menuAct  = createAct(XR_ACTION_TYPE_BOOLEAN_INPUT,  "menu_btn",   "Menu Button");

                        m_aimPoseAction    = reinterpret_cast<void*>(aimAct);
                        m_triggerAction    = reinterpret_cast<void*>(trigAct);
                        m_squeezeAction    = reinterpret_cast<void*>(sqzAct);
                        m_thumbstickAction = reinterpret_cast<void*>(thumbAct);
                        m_buttonAAction    = reinterpret_cast<void*>(btnAAct);
                        m_buttonBAction    = reinterpret_cast<void*>(btnBAct);
                        m_menuAction       = reinterpret_cast<void*>(menuAct);

                        auto bindPath = [&](XrAction act, const char* pathStr) -> XrActionSuggestedBinding {
                            XrPath p = XR_NULL_PATH;
                            xrStringToPath(xrInst, pathStr, &p);
                            return XrActionSuggestedBinding{ act, p };
                        };

                        XrActionSuggestedBinding touchBindings[] = {
                            bindPath(aimAct,   "/user/hand/left/input/aim/pose"),
                            bindPath(aimAct,   "/user/hand/right/input/aim/pose"),
                            bindPath(trigAct,  "/user/hand/left/input/trigger/value"),
                            bindPath(trigAct,  "/user/hand/right/input/trigger/value"),
                            bindPath(sqzAct,   "/user/hand/left/input/squeeze/value"),
                            bindPath(sqzAct,   "/user/hand/right/input/squeeze/value"),
                            bindPath(thumbAct, "/user/hand/left/input/thumbstick"),
                            bindPath(thumbAct, "/user/hand/right/input/thumbstick"),
                            bindPath(btnAAct,  "/user/hand/right/input/a/click"),
                            bindPath(btnAAct,  "/user/hand/left/input/x/click"),
                            bindPath(btnBAct,  "/user/hand/right/input/b/click"),
                            bindPath(btnBAct,  "/user/hand/left/input/y/click"),
                            bindPath(menuAct,  "/user/hand/left/input/menu/click")
                        };

                        XrPath touchProfile = XR_NULL_PATH;
                        xrStringToPath(xrInst, "/interaction_profiles/oculus/touch_controller", &touchProfile);
                        XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
                        suggested.interactionProfile = touchProfile;
                        suggested.suggestedBindings = touchBindings;
                        suggested.countSuggestedBindings = static_cast<uint32_t>(sizeof(touchBindings) / sizeof(touchBindings[0]));
                        xrSuggestInteractionProfileBindings(xrInst, &suggested);

                        XrActionSpaceCreateInfo aspInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
                        aspInfo.action = aimAct;
                        aspInfo.poseInActionSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
                        aspInfo.poseInActionSpace.position = { 0.0f, 0.0f, 0.0f };

                        aspInfo.subactionPath = m_handPaths[1]; // Right hand
                        XrSpace rSpace = XR_NULL_HANDLE;
                        xrCreateActionSpace(xrSess, &aspInfo, &rSpace);
                        m_rightAimSpace = reinterpret_cast<void*>(rSpace);

                        aspInfo.subactionPath = m_handPaths[0]; // Left hand
                        XrSpace lSpace = XR_NULL_HANDLE;
                        xrCreateActionSpace(xrSess, &aspInfo, &lSpace);
                        m_leftAimSpace = reinterpret_cast<void*>(lSpace);

                        XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
                        attachInfo.countActionSets = 1;
                        attachInfo.actionSets = &actSet;
                        xrAttachSessionActionSets(xrSess, &attachInfo);
                    }

                    m_realXrActive = true;
                    m_initialized = true;
                    m_sessionRunning = true;
                    m_sessionState = OpenXrSessionState::Focused;

                    // Pump initial events so if READY is already queued, xrBeginSession runs immediately
                    PollXrEvents();
                    ALOGI("Real Meta Quest OpenXR Session initialized and ready!");
                    return true;
                }
            }
        }
        ALOGW("OpenXR runtime not active on this device (%d); using fallback mode", (int)res);
    }
#endif

    // Desktop / Simulation / Fallback initialization
    m_instance = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00010001));
    m_systemId = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00020002));
    m_sessionState = OpenXrSessionState::Idle;
    m_initialized = true;
    m_realXrActive = false;

    std::memset(&m_controllerState, 0, sizeof(m_controllerState));
    m_controllerState.ray_origin[0] = 0.0f;
    m_controllerState.ray_origin[1] = 1.50f;
    m_controllerState.ray_origin[2] = 0.0f;
    m_controllerState.ray_direction[0] = 0.0f;
    m_controllerState.ray_direction[1] = 0.0f;
    m_controllerState.ray_direction[2] = -1.0f;
    m_controllerState.is_active = true;

    std::cout << "[VXR-OpenXR] Initialized OpenXR client for '" << config.app_name
              << "' (Display rate: " << config.display_refresh_rate << "Hz, "
              << "SpaceWarp SSW: " << (config.enable_space_warp_ssw ? "ON" : "OFF") << ")" << std::endl;

    HandleSessionStateChanged(OpenXrSessionState::Ready);
    return true;
}

void VxrOpenXrClient::PollXrEvents() {
#if defined(__ANDROID__)
    if (!m_realXrActive || !m_instance) return;
    XrInstance xrInst = reinterpret_cast<XrInstance>(m_instance);
    XrSession xrSess = reinterpret_cast<XrSession>(m_session);

    while (true) {
        XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
        XrResult res = xrPollEvent(xrInst, &ev);
        if (res != XR_SUCCESS) break;

        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* sc = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            ALOGI("OpenXR Session State changed to %d", (int)sc->state);
            switch (sc->state) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
                    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (XR_SUCCEEDED(xrBeginSession(xrSess, &beginInfo))) {
                        ALOGI("xrBeginSession(PRIMARY_STEREO) succeeded!");
                        m_sessionRunning = true;
                        m_sessionState = OpenXrSessionState::Focused;
                    }
                    break;
                }
                case XR_SESSION_STATE_SYNCHRONIZED:
                case XR_SESSION_STATE_VISIBLE:
                case XR_SESSION_STATE_FOCUSED:
                    m_sessionRunning = true;
                    m_sessionState = OpenXrSessionState::Focused;
                    break;
                case XR_SESSION_STATE_STOPPING:
                    xrEndSession(xrSess);
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    m_sessionRunning = false;
                    m_sessionState = OpenXrSessionState::Exiting;
                    break;
                default:
                    break;
            }
        }
    }
#endif
}

void VxrOpenXrClient::HandleSessionStateChanged(OpenXrSessionState new_state) {
    m_sessionState = new_state;
    switch (new_state) {
        case OpenXrSessionState::Ready:
            BeginSession();
            break;
        case OpenXrSessionState::Stopping:
            EndSession();
            break;
        default:
            break;
    }
}

bool VxrOpenXrClient::BeginSession() {
    if (!m_initialized) return false;
    if (!m_realXrActive) {
        m_session = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00030003));
        m_localSpace = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00040004));
    }
    m_sessionRunning = true;
    m_sessionState = OpenXrSessionState::Focused;
    return true;
}

bool VxrOpenXrClient::EndSession() {
    if (!m_sessionRunning) return false;
    m_sessionRunning = false;
    m_sessionState = OpenXrSessionState::Idle;
    if (!m_realXrActive) {
        m_localSpace = nullptr;
        m_session = nullptr;
    }
    return true;
}

bool VxrOpenXrClient::WaitFrame(uint64_t* out_predicted_display_time_ns) {
    if (!m_sessionRunning) return false;

#if defined(__ANDROID__)
    if (m_realXrActive && m_session) {
        PollXrEvents();
        XrSession xrSess = reinterpret_cast<XrSession>(m_session);
        XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState frameState = { XR_TYPE_FRAME_STATE };
        XrResult res = xrWaitFrame(xrSess, &waitInfo, &frameState);
        if (XR_SUCCEEDED(res)) {
            m_lastPredictedDisplayTimeNs = static_cast<uint64_t>(frameState.predictedDisplayTime);
            m_shouldRenderFrame = (frameState.shouldRender == XR_TRUE);
            if (out_predicted_display_time_ns) {
                *out_predicted_display_time_ns = m_lastPredictedDisplayTimeNs;
            }
            return true;
        }
        if (out_predicted_display_time_ns) {
            *out_predicted_display_time_ns = m_lastPredictedDisplayTimeNs;
        }
        return true;
    }
#endif

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    uint32_t fps = (m_config.display_refresh_rate > 0) ? m_config.display_refresh_rate : 90;
    uint64_t frame_interval_ns = 1000000000ULL / fps;
    m_lastPredictedDisplayTimeNs = now_ns + frame_interval_ns;
    if (out_predicted_display_time_ns) {
        *out_predicted_display_time_ns = m_lastPredictedDisplayTimeNs;
    }
    return true;
}

bool VxrOpenXrClient::BeginFrame() {
    if (!m_sessionRunning) return false;
#if defined(__ANDROID__)
    if (m_realXrActive && m_session) {
        XrSession xrSess = reinterpret_cast<XrSession>(m_session);
        XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
        xrBeginFrame(xrSess, &beginInfo);
    }
#endif
    return true;
}

bool VxrOpenXrClient::LocateViews(uint64_t display_time_ns, XrEyeView out_views[2]) {
    if (!m_sessionRunning || out_views == nullptr) return false;

#if defined(__ANDROID__)
    if (m_realXrActive && m_session && m_localSpace) {
        XrSession xrSess = reinterpret_cast<XrSession>(m_session);
        XrSpace localSp = reinterpret_cast<XrSpace>(m_localSpace);
        XrViewLocateInfo locInfo = { XR_TYPE_VIEW_LOCATE_INFO };
        locInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locInfo.displayTime = static_cast<XrTime>(display_time_ns ? display_time_ns : m_lastPredictedDisplayTimeNs);
        locInfo.space = localSp;

        XrViewState viewState = { XR_TYPE_VIEW_STATE };
        XrView xrViews[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
        uint32_t viewCountOut = 0;
        if (XR_SUCCEEDED(xrLocateViews(xrSess, &locInfo, &viewState, 2, &viewCountOut, xrViews)) && viewCountOut == 2) {
            for (int i = 0; i < 2; ++i) {
                out_views[i].position[0] = xrViews[i].pose.position.x;
                out_views[i].position[1] = xrViews[i].pose.position.y;
                out_views[i].position[2] = xrViews[i].pose.position.z;
                out_views[i].orientation[0] = xrViews[i].pose.orientation.x;
                out_views[i].orientation[1] = xrViews[i].pose.orientation.y;
                out_views[i].orientation[2] = xrViews[i].pose.orientation.z;
                out_views[i].orientation[3] = xrViews[i].pose.orientation.w;
                out_views[i].fov_left   = xrViews[i].fov.angleLeft;
                out_views[i].fov_right  = xrViews[i].fov.angleRight;
                out_views[i].fov_top    = xrViews[i].fov.angleUp;
                out_views[i].fov_bottom = xrViews[i].fov.angleDown;
                m_cachedViews[i] = out_views[i];
            }
            return true;
        }
    }
#endif

    out_views[0].position[0] = -0.0315f;
    out_views[0].position[1] = 1.70f;
    out_views[0].position[2] = 0.0f;
    out_views[0].orientation[0] = 0.0f;
    out_views[0].orientation[1] = 0.0f;
    out_views[0].orientation[2] = 0.0f;
    out_views[0].orientation[3] = 1.0f;
    out_views[0].fov_left = -1.05f;
    out_views[0].fov_right = 1.05f;
    out_views[0].fov_top = 1.05f;
    out_views[0].fov_bottom = -1.05f;

    out_views[1].position[0] = 0.0315f;
    out_views[1].position[1] = 1.70f;
    out_views[1].position[2] = 0.0f;
    out_views[1].orientation[0] = 0.0f;
    out_views[1].orientation[1] = 0.0f;
    out_views[1].orientation[2] = 0.0f;
    out_views[1].orientation[3] = 1.0f;
    out_views[1].fov_left = -1.05f;
    out_views[1].fov_right = 1.05f;
    out_views[1].fov_top = 1.05f;
    out_views[1].fov_bottom = -1.05f;

    m_cachedViews[0] = out_views[0];
    m_cachedViews[1] = out_views[1];
    return true;
}

void VxrOpenXrClient::SubmitQuadSwapchainRgba(const uint32_t* rgbaPixels,
                                              uint32_t width,
                                              uint32_t height,
                                              float screenDistM,
                                              float screenScale) {
    m_quadDistM = (screenDistM > 0.5f) ? screenDistM : 2.0f;
    m_quadWidthM = 2.0f * ((screenScale > 0.2f) ? screenScale : 1.0f);
    m_quadHeightM = m_quadWidthM * (static_cast<float>(height) / static_cast<float>(width));

#if defined(__ANDROID__)
    if (!m_realXrActive || !rgbaPixels || width == 0 || height == 0) return;

    // 1. Upload 1280x800 RGBA buffer into m_quadSwapchain (vertically flipped for OpenGL UV Bottom-Left origin)
    if (m_quadSwapchain && !m_quadSwapchainTexIds.empty()) {
        XrSwapchain quadSc = reinterpret_cast<XrSwapchain>(m_quadSwapchain);
        XrSwapchainImageAcquireInfo acqInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        uint32_t imgIdx = 0;
        if (XR_SUCCEEDED(xrAcquireSwapchainImage(quadSc, &acqInfo, &imgIdx))) {
            XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            waitInfo.timeout = XR_INFINITE_DURATION;
            if (XR_SUCCEEDED(xrWaitSwapchainImage(quadSc, &waitInfo))) {
                if (imgIdx < m_quadSwapchainTexIds.size()) {
                    size_t totalPx = static_cast<size_t>(width) * height;
                    if (m_flippedUploadBuffer.size() < totalPx) {
                        m_flippedUploadBuffer.resize(totalPx);
                    }
                    for (uint32_t y = 0; y < height; ++y) {
                        const uint32_t* srcRow = rgbaPixels + static_cast<size_t>(height - 1 - y) * width;
                        uint32_t* dstRow = m_flippedUploadBuffer.data() + static_cast<size_t>(y) * width;
                        std::memcpy(dstRow, srcRow, width * sizeof(uint32_t));
                    }

                    GLuint texId = m_quadSwapchainTexIds[imgIdx];
                    glBindTexture(GL_TEXTURE_2D, texId);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                    static_cast<GLsizei>(width),
                                    static_cast<GLsizei>(height),
                                    GL_RGBA, GL_UNSIGNED_BYTE,
                                    m_flippedUploadBuffer.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glFlush();
                }
            }
            XrSwapchainImageReleaseInfo relInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(quadSc, &relInfo);
        }
    }

    // 2. Also cycle the 64x64 background projection swapchain so both layers are valid
    if (m_skySwapchain && !m_skySwapchainTexIds.empty()) {
        XrSwapchain skySc = reinterpret_cast<XrSwapchain>(m_skySwapchain);
        XrSwapchainImageAcquireInfo acqInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        uint32_t imgIdx = 0;
        if (XR_SUCCEEDED(xrAcquireSwapchainImage(skySc, &acqInfo, &imgIdx))) {
            XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            waitInfo.timeout = XR_INFINITE_DURATION;
            if (XR_SUCCEEDED(xrWaitSwapchainImage(skySc, &waitInfo))) {
                if (imgIdx < m_skySwapchainTexIds.size()) {
                    static std::vector<uint32_t> s_skyPixels(64 * 64, 0xFF140E0Cu); // dark slate theater room
                    GLuint texId = m_skySwapchainTexIds[imgIdx];
                    glBindTexture(GL_TEXTURE_2D, texId);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, s_skyPixels.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
            }
            XrSwapchainImageReleaseInfo relInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(skySc, &relInfo);
        }
    }
#endif
}

bool VxrOpenXrClient::EndFrame(uint64_t display_time_ns) {
    if (!m_sessionRunning) return false;

#if defined(__ANDROID__)
    if (m_realXrActive && m_session && m_localSpace) {
        XrSession xrSess = reinterpret_cast<XrSession>(m_session);
        XrSpace localSp = reinterpret_cast<XrSpace>(m_localSpace);
        XrTime dispTime = static_cast<XrTime>(display_time_ns ? display_time_ns : m_lastPredictedDisplayTimeNs);

        const XrCompositionLayerBaseHeader* layers[2] = {};
        uint32_t layerCount = 0;

        // Layer 0: Stereo Projection Layer (360 dark VR theater room)
        XrCompositionLayerProjectionView projViews[2] = {
            { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW },
            { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }
        };
        XrCompositionLayerProjection projLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        if (m_skySwapchain && m_shouldRenderFrame) {
            XrSwapchain skySc = reinterpret_cast<XrSwapchain>(m_skySwapchain);
            for (int i = 0; i < 2; ++i) {
                projViews[i].pose.position = { m_cachedViews[i].position[0], m_cachedViews[i].position[1], m_cachedViews[i].position[2] };
                projViews[i].pose.orientation = { m_cachedViews[i].orientation[0], m_cachedViews[i].orientation[1], m_cachedViews[i].orientation[2], m_cachedViews[i].orientation[3] };
                projViews[i].fov = { m_cachedViews[i].fov_left, m_cachedViews[i].fov_right, m_cachedViews[i].fov_top, m_cachedViews[i].fov_bottom };
                projViews[i].subImage.swapchain = skySc;
                projViews[i].subImage.imageRect.offset = { 0, 0 };
                projViews[i].subImage.imageRect.extent = { 64, 64 };
                projViews[i].subImage.imageArrayIndex = 0;
            }
            projLayer.layerFlags = 0;
            projLayer.space = localSp;
            projLayer.viewCount = 2;
            projLayer.views = projViews;
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer);
        }

        // Layer 1: High-Resolution 1280x800 Virtual Desktop Cinema Quad in 3D VR Space
        XrCompositionLayerQuad quadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
        if (m_quadSwapchain && m_shouldRenderFrame) {
            XrSwapchain quadSc = reinterpret_cast<XrSwapchain>(m_quadSwapchain);
            quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quadLayer.space = localSp;
            quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quadLayer.subImage.swapchain = quadSc;
            quadLayer.subImage.imageRect.offset = { 0, 0 };
            quadLayer.subImage.imageRect.extent = { 1280, 800 };
            quadLayer.subImage.imageArrayIndex = 0;
            quadLayer.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
            quadLayer.pose.position = { 0.0f, 0.0f, -m_quadDistM };
            quadLayer.size = { m_quadWidthM, m_quadHeightM };
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quadLayer);
        }

        XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
        endInfo.displayTime = dispTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = layerCount;
        endInfo.layers = (layerCount > 0) ? layers : nullptr;

        xrEndFrame(xrSess, &endInfo);
    }
#endif

    m_frameIndex++;
    return true;
}

bool VxrOpenXrClient::PollControllerInput(XrControllerInputState* out_state) {
    if (!m_sessionRunning) return false;

#if defined(__ANDROID__)
    if (m_realXrActive && m_session && m_actionSet && m_localSpace) {
        XrSession xrSess = reinterpret_cast<XrSession>(m_session);
        XrActionSet actSet = reinterpret_cast<XrActionSet>(m_actionSet);
        XrSpace localSp = reinterpret_cast<XrSpace>(m_localSpace);

        XrActiveActionSet activeAs = { actSet, XR_NULL_PATH };
        XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeAs;

        if (XR_SUCCEEDED(xrSyncActions(xrSess, &syncInfo))) {
            auto getFloat = [&](void* actHandle) -> float {
                if (!actHandle) return 0.0f;
                XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
                gi.action = reinterpret_cast<XrAction>(actHandle);
                XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
                if (XR_SUCCEEDED(xrGetActionStateFloat(xrSess, &gi, &st)) && st.isActive) {
                    return st.currentState;
                }
                return 0.0f;
            };
            auto getBool = [&](void* actHandle) -> bool {
                if (!actHandle) return false;
                XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
                gi.action = reinterpret_cast<XrAction>(actHandle);
                XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
                if (XR_SUCCEEDED(xrGetActionStateBoolean(xrSess, &gi, &st)) && st.isActive) {
                    return st.currentState == XR_TRUE;
                }
                return false;
            };

            m_controllerState.trigger_value = getFloat(m_triggerAction);
            m_controllerState.grip_value    = getFloat(m_squeezeAction);
            m_controllerState.button_a      = getBool(m_buttonAAction);
            m_controllerState.button_b      = getBool(m_buttonBAction);
            m_controllerState.button_menu   = getBool(m_menuAction) || m_controllerState.button_b;

            if (m_thumbstickAction) {
                XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
                gi.action = reinterpret_cast<XrAction>(m_thumbstickAction);
                XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
                if (XR_SUCCEEDED(xrGetActionStateVector2f(xrSess, &gi, &st)) && st.isActive) {
                    m_controllerState.thumbstick_x = st.currentState.x;
                    m_controllerState.thumbstick_y = st.currentState.y;
                }
            }

            // Locate 6DoF Right Hand (or Left Hand fallback) Aim Pose in LOCAL space
            XrSpace aimSpaces[2] = {
                reinterpret_cast<XrSpace>(m_rightAimSpace),
                reinterpret_cast<XrSpace>(m_leftAimSpace)
            };
            bool poseLocated = false;
            for (int h = 0; h < 2; ++h) {
                if (!aimSpaces[h]) continue;
                XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
                if (XR_SUCCEEDED(xrLocateSpace(aimSpaces[h], localSp,
                                               static_cast<XrTime>(m_lastPredictedDisplayTimeNs), &loc))) {
                    if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                        (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                        m_controllerState.ray_origin[0] = loc.pose.position.x;
                        m_controllerState.ray_origin[1] = loc.pose.position.y;
                        m_controllerState.ray_origin[2] = loc.pose.position.z;

                        float qx = loc.pose.orientation.x;
                        float qy = loc.pose.orientation.y;
                        float qz = loc.pose.orientation.z;
                        float qw = loc.pose.orientation.w;

                        // Forward vector (-Z) rotated by quaternion q
                        m_controllerState.ray_direction[0] = -2.0f * (qx * qz + qw * qy);
                        m_controllerState.ray_direction[1] = -2.0f * (qy * qz - qw * qx);
                        m_controllerState.ray_direction[2] = -(1.0f - 2.0f * (qx * qx + qy * qy));
                        m_controllerState.is_active = true;
                        poseLocated = true;
                        break;
                    }
                }
            }

            // If no controller is turned on yet, use head gaze ray from m_cachedViews[0]
            if (!poseLocated) {
                m_controllerState.ray_origin[0] = m_cachedViews[0].position[0];
                m_controllerState.ray_origin[1] = m_cachedViews[0].position[1];
                m_controllerState.ray_origin[2] = m_cachedViews[0].position[2];
                float qx = m_cachedViews[0].orientation[0];
                float qy = m_cachedViews[0].orientation[1];
                float qz = m_cachedViews[0].orientation[2];
                float qw = m_cachedViews[0].orientation[3];
                m_controllerState.ray_direction[0] = -2.0f * (qx * qz + qw * qy);
                m_controllerState.ray_direction[1] = -2.0f * (qy * qz - qw * qx);
                m_controllerState.ray_direction[2] = -(1.0f - 2.0f * (qx * qx + qy * qy));
                m_controllerState.is_active = true;
            }
        }
    }
#endif

    m_controllerState.trigger_clicked = (m_controllerState.trigger_value >= 0.50f) || m_controllerState.button_a;
    m_controllerState.grip_clicked    = (m_controllerState.grip_value >= 0.55f);
    if (out_state) {
        *out_state = m_controllerState;
    }
    return m_controllerState.is_active;
}

void VxrOpenXrClient::SetControllerInputState(const XrControllerInputState& state) {
    m_controllerState = state;
    m_controllerState.trigger_clicked = state.trigger_clicked || (state.trigger_value >= 0.55f);
    m_controllerState.grip_clicked = state.grip_clicked || (state.grip_value >= 0.55f);
}

bool VxrOpenXrClient::ComputeDesktopPointerUV(float screenDistM, float* outU, float* outV) const {
    if (m_realXrActive) {
        return ComputeDesktopPointerUV(m_quadDistM, m_quadWidthM, m_quadHeightM, 0.0f, outU, outV);
    }
    return ComputeDesktopPointerUV(screenDistM, 2.0f, 1.125f, 1.50f, outU, outV);
}

bool VxrOpenXrClient::ComputeDesktopPointerUV(float screenDistM,
                                              float screenWidthM,
                                              float screenHeightM,
                                              float screenCenterY,
                                              float* outU,
                                              float* outV) const {
    if (!outU || !outV || screenWidthM <= 0.0f || screenHeightM <= 0.0f) {
        return false;
    }

    float planeZ = (screenDistM > 0.0f) ? -screenDistM : screenDistM;
    float dirZ = m_controllerState.ray_direction[2];
    if (std::abs(dirZ) < 1e-6f) {
        *outU = 0.5f;
        *outV = 0.5f;
        return false;
    }

    float t = (planeZ - m_controllerState.ray_origin[2]) / dirZ;
    if (t <= 0.0f) {
        *outU = 0.5f;
        *outV = 0.5f;
        return false;
    }

    float hitX = m_controllerState.ray_origin[0] + t * m_controllerState.ray_direction[0];
    float hitY = m_controllerState.ray_origin[1] + t * m_controllerState.ray_direction[1];

    float rawU = (hitX / screenWidthM) + 0.5f;
    float rawV = 0.5f - ((hitY - screenCenterY) / screenHeightM);

    bool inside = (rawU >= 0.0f && rawU <= 1.0f && rawV >= 0.0f && rawV <= 1.0f);
    *outU = (std::max)(0.0f, (std::min)(1.0f, rawU));
    *outV = (std::max)(0.0f, (std::min)(1.0f, rawV));
    return inside;
}

void VxrOpenXrClient::Cleanup() {
    EndSession();
#if defined(__ANDROID__)
    if (m_realXrActive) {
        if (m_rightAimSpace) { xrDestroySpace(reinterpret_cast<XrSpace>(m_rightAimSpace)); m_rightAimSpace = nullptr; }
        if (m_leftAimSpace)  { xrDestroySpace(reinterpret_cast<XrSpace>(m_leftAimSpace));  m_leftAimSpace = nullptr; }
        if (m_actionSet)     { xrDestroyActionSet(reinterpret_cast<XrActionSet>(m_actionSet)); m_actionSet = nullptr; }
        if (m_quadSwapchain) { xrDestroySwapchain(reinterpret_cast<XrSwapchain>(m_quadSwapchain)); m_quadSwapchain = nullptr; }
        if (m_skySwapchain)  { xrDestroySwapchain(reinterpret_cast<XrSwapchain>(m_skySwapchain));  m_skySwapchain = nullptr; }
        if (m_localSpace)    { xrDestroySpace(reinterpret_cast<XrSpace>(m_localSpace)); m_localSpace = nullptr; }
        if (m_viewSpace)     { xrDestroySpace(reinterpret_cast<XrSpace>(m_viewSpace));  m_viewSpace = nullptr; }
        if (m_session)       { xrDestroySession(reinterpret_cast<XrSession>(m_session)); m_session = nullptr; }
        if (m_instance)      { xrDestroyInstance(reinterpret_cast<XrInstance>(m_instance)); m_instance = nullptr; }
        if (m_eglDisplay) {
            eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_eglContext) eglDestroyContext(m_eglDisplay, m_eglContext);
            if (m_eglSurface) eglDestroySurface(m_eglDisplay, m_eglSurface);
            eglTerminate(m_eglDisplay);
            m_eglDisplay = nullptr;
            m_eglContext = nullptr;
            m_eglSurface = nullptr;
        }
        m_realXrActive = false;
    }
    if (m_loaderHandle != nullptr) {
        dlclose(m_loaderHandle);
        m_loaderHandle = nullptr;
    }
#endif
    m_pfnGetInstanceProcAddr = nullptr;
    m_systemId = nullptr;
    m_instance = nullptr;
    m_initialized = false;
}

} // namespace vxr
