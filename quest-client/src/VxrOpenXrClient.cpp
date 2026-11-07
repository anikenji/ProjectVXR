#include "VxrOpenXrClient.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <jni.h>
#include <sys/system_properties.h>
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

#if defined(__ANDROID__)
namespace {

struct Vec3 {
    float x, y, z;
};

struct Vertex3D {
    float pos[3];
    float norm[3];
};

static void Mat4Identity(float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void Mat4Multiply(float out[16], const float a[16], const float b[16]) {
    float r[16] = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    std::memcpy(out, r, 16 * sizeof(float));
}

static void Mat4CreateProjectionFromFov(float out[16],
                                        float angleLeft,
                                        float angleRight,
                                        float angleUp,
                                        float angleDown,
                                        float nearZ,
                                        float farZ) {
    const float tanLeft  = std::tan(angleLeft);
    const float tanRight = std::tan(angleRight);
    const float tanUp    = std::tan(angleUp);
    const float tanDown  = std::tan(angleDown);

    const float tanWidth  = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;

    std::memset(out, 0, 16 * sizeof(float));
    out[0]  = 2.0f / tanWidth;
    out[5]  = 2.0f / tanHeight;
    out[8]  = (tanRight + tanLeft) / tanWidth;
    out[9]  = (tanUp + tanDown) / tanHeight;
    out[10] = -(farZ + nearZ) / (farZ - nearZ);
    out[11] = -1.0f;
    out[14] = -(farZ * 2.0f * nearZ) / (farZ - nearZ);
}

static void Mat4CreateFromQuatPos(float out[16], const float q[4], const float pos[3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;

    out[0]  = 1.0f - (yy + zz);
    out[1]  = xy + wz;
    out[2]  = xz - wy;
    out[3]  = 0.0f;

    out[4]  = xy - wz;
    out[5]  = 1.0f - (xx + zz);
    out[6]  = yz + wx;
    out[7]  = 0.0f;

    out[8]  = xz + wy;
    out[9]  = yz - wx;
    out[10] = 1.0f - (xx + yy);
    out[11] = 0.0f;

    out[12] = pos[0];
    out[13] = pos[1];
    out[14] = pos[2];
    out[15] = 1.0f;
}

static void Mat4CreateViewFromEyePose(float out[16], const float q[4], const float pos[3]) {
    // Inverse of rigid transform (R, T) is (R^T, -R^T * T)
    float worldFromEye[16];
    Mat4CreateFromQuatPos(worldFromEye, q, pos);

    out[0]  = worldFromEye[0];
    out[1]  = worldFromEye[4];
    out[2]  = worldFromEye[8];
    out[3]  = 0.0f;

    out[4]  = worldFromEye[1];
    out[5]  = worldFromEye[5];
    out[6]  = worldFromEye[9];
    out[7]  = 0.0f;

    out[8]  = worldFromEye[2];
    out[9]  = worldFromEye[6];
    out[10] = worldFromEye[10];
    out[11] = 0.0f;

    out[12] = -(out[0] * pos[0] + out[4] * pos[1] + out[8]  * pos[2]);
    out[13] = -(out[1] * pos[0] + out[5] * pos[1] + out[9]  * pos[2]);
    out[14] = -(out[2] * pos[0] + out[6] * pos[1] + out[10] * pos[2]);
    out[15] = 1.0f;
}

static void AppendOrientedBox(std::vector<Vertex3D>* verts,
                              float cx, float cy, float cz,
                              float hx, float hy, float hz,
                              float rotXRad = 0.0f,
                              float rotZRad = 0.0f) {
    const float cxr = std::cos(rotXRad), sxr = std::sin(rotXRad);
    const float czr = std::cos(rotZRad), szr = std::sin(rotZRad);

    auto xform = [&](float x, float y, float z, float* ox, float* oy, float* oz) {
        // Rotate around X then Z, then translate
        float y1 = y * cxr - z * sxr;
        float z1 = y * sxr + z * cxr;
        float x2 = x * czr - y1 * szr;
        float y2 = x * szr + y1 * czr;
        *ox = cx + x2;
        *oy = cy + y2;
        *oz = cz + z1;
    };

    struct FaceDef {
        float n[3];
        float v[4][3];
    };
    const FaceDef faces[6] = {
        { {  0,  0,  1 }, { {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz} } },
        { {  0,  0, -1 }, { { hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx,  hy, -hz}, { hx,  hy, -hz} } },
        { {  0,  1,  0 }, { {-hx,  hy,  hz}, { hx,  hy,  hz}, { hx,  hy, -hz}, {-hx,  hy, -hz} } },
        { {  0, -1,  0 }, { {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx, -hy,  hz}, {-hx, -hy,  hz} } },
        { {  1,  0,  0 }, { { hx, -hy,  hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, { hx,  hy,  hz} } },
        { { -1,  0,  0 }, { {-hx, -hy, -hz}, {-hx, -hy,  hz}, {-hx,  hy,  hz}, {-hx,  hy, -hz} } }
    };

    const int triOrder[6] = { 0, 1, 2, 0, 2, 3 };
    for (int f = 0; f < 6; ++f) {
        float nx, ny, nz;
        float ny1 = faces[f].n[1] * cxr - faces[f].n[2] * sxr;
        float nz1 = faces[f].n[1] * sxr + faces[f].n[2] * cxr;
        nx = faces[f].n[0] * czr - ny1 * szr;
        ny = faces[f].n[0] * szr + ny1 * czr;
        nz = nz1;

        for (int k = 0; k < 6; ++k) {
            int vi = triOrder[k];
            Vertex3D vtx = {};
            xform(faces[f].v[vi][0], faces[f].v[vi][1], faces[f].v[vi][2],
                  &vtx.pos[0], &vtx.pos[1], &vtx.pos[2]);
            vtx.norm[0] = nx;
            vtx.norm[1] = ny;
            vtx.norm[2] = nz;
            verts->push_back(vtx);
        }
    }
}

static void AppendCylinderY(std::vector<Vertex3D>* verts,
                            float cx, float cy, float cz,
                            float radius, float halfHeight,
                            int segments = 12,
                            float tiltXRad = 0.0f,
                            float tiltZRad = 0.0f) {
    const float cxr = std::cos(tiltXRad), sxr = std::sin(tiltXRad);
    const float czr = std::cos(tiltZRad), szr = std::sin(tiltZRad);

    auto xform = [&](float x, float y, float z, float* ox, float* oy, float* oz) {
        float y1 = y * cxr - z * sxr;
        float z1 = y * sxr + z * cxr;
        float x2 = x * czr - y1 * szr;
        float y2 = x * szr + y1 * czr;
        *ox = cx + x2;
        *oy = cy + y2;
        *oz = cz + z1;
    };

    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float a0 = i * step;
        float a1 = (i + 1) * step;
        float x0 = std::cos(a0) * radius, z0 = std::sin(a0) * radius;
        float x1 = std::cos(a1) * radius, z1 = std::sin(a1) * radius;

        // Top disk triangle
        Vertex3D t0 = {}, t1 = {}, t2 = {};
        xform(0.0f, halfHeight, 0.0f, &t0.pos[0], &t0.pos[1], &t0.pos[2]);
        xform(x0,   halfHeight, z0,   &t1.pos[0], &t1.pos[1], &t1.pos[2]);
        xform(x1,   halfHeight, z1,   &t2.pos[0], &t2.pos[1], &t2.pos[2]);
        t0.norm[1] = t1.norm[1] = t2.norm[1] = 1.0f;
        verts->push_back(t0); verts->push_back(t1); verts->push_back(t2);

        // Side quad (2 triangles)
        Vertex3D s0 = {}, s1 = {}, s2 = {}, s3 = {};
        xform(x0, -halfHeight, z0, &s0.pos[0], &s0.pos[1], &s0.pos[2]);
        xform(x1, -halfHeight, z1, &s1.pos[0], &s1.pos[1], &s1.pos[2]);
        xform(x1,  halfHeight, z1, &s2.pos[0], &s2.pos[1], &s2.pos[2]);
        xform(x0,  halfHeight, z0, &s3.pos[0], &s3.pos[1], &s3.pos[2]);
        s0.norm[0] = s3.norm[0] = std::cos(a0); s0.norm[2] = s3.norm[2] = std::sin(a0);
        s1.norm[0] = s2.norm[0] = std::cos(a1); s1.norm[2] = s2.norm[2] = std::sin(a1);
        verts->push_back(s0); verts->push_back(s1); verts->push_back(s2);
        verts->push_back(s0); verts->push_back(s2); verts->push_back(s3);
    }
}

static GLuint CompileGlShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char logBuf[512] = {};
        glGetShaderInfoLog(sh, sizeof(logBuf) - 1, nullptr, logBuf);
        ALOGE("GLES3 Shader compile error: %s", logBuf);
    }
    return sh;
}

} // namespace
#endif

VxrOpenXrClient::VxrOpenXrClient()
    : m_initialized(false)
    , m_sessionRunning(false)
    , m_realXrActive(false)
    , m_shouldRenderFrame(true)
    , m_fbRenderModelSupported(false)
    , m_passthroughEnabled(false)
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
#if defined(__ANDROID__)
    , m_screenCenterY(0.0f)
#else
    , m_screenCenterY(1.40f)
#endif
    , m_screenAnchorInitialized(false)
    , m_recenterHeld(false)
    , m_eglDisplay(nullptr)
    , m_eglConfig(nullptr)
    , m_eglContext(nullptr)
    , m_eglSurface(nullptr)
    , m_quadSwapchain(nullptr)
    , m_skySwapchain(nullptr)
    , m_skyWidth(1024)
    , m_skyHeight(1024)
    , m_skyFbo(0)
    , m_skyDepthRbo(0)
    , m_sceneProgram(0)
    , m_uViewProjLoc(-1)
    , m_uModelLoc(-1)
    , m_uColorLoc(-1)
    , m_quadDistM(2.0f)
    , m_quadWidthM(2.2f)
    , m_quadHeightM(1.375f)
    , m_actionSet(nullptr)
    , m_aimPoseAction(nullptr)
    , m_gripPoseAction(nullptr)
    , m_triggerAction(nullptr)
    , m_squeezeAction(nullptr)
    , m_thumbstickAction(nullptr)
    , m_buttonAAction(nullptr)
    , m_buttonBAction(nullptr)
    , m_menuAction(nullptr)
    , m_rightAimSpace(nullptr)
    , m_leftAimSpace(nullptr)
    , m_rightGripSpace(nullptr)
    , m_leftGripSpace(nullptr)
    , m_activeHand(1)
    , m_triggerPressed(false)
    , m_gripPressed(false)
    , m_thumbstickX(0.0f)
    , m_thumbstickY(0.0f)
    , m_hasLaserHit(false)
{
    m_handPaths[0] = 0;
    m_handPaths[1] = 0;
    std::memset(&m_leftControllerPose, 0, sizeof(m_leftControllerPose));
    std::memset(&m_rightControllerPose, 0, sizeof(m_rightControllerPose));
    m_leftControllerPose.orientation[3] = 1.0f;
    m_rightControllerPose.orientation[3] = 1.0f;
    std::memset(&m_headPose, 0, sizeof(m_headPose));
    m_headPose.orientation[3] = 1.0f;
    m_laserHitPoint[0] = 0.0f;
    m_laserHitPoint[1] = m_screenCenterY;
    m_laserHitPoint[2] = -2.0f;

    std::memset(m_cachedViews, 0, sizeof(m_cachedViews));
    std::memset(&m_controllerState, 0, sizeof(m_controllerState));
    m_controllerState.ray_origin[0] = 0.0f;
    m_controllerState.ray_origin[1] = m_screenCenterY;
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

        // Probe available OpenXR instance extensions (including XR_FB_render_model)
        uint32_t extCount = 0;
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
        if (extCount > 0) {
            std::vector<XrExtensionProperties> exts(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
            if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data()))) {
                for (const auto& ep : exts) {
                    if (std::strcmp(ep.extensionName, "XR_FB_render_model") == 0) {
                        m_fbRenderModelSupported = true;
                        ALOGI("Detected OpenXR extension: XR_FB_render_model (version %u)", ep.extensionVersion);
                    }
                }
            }
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

                // Query real XrSystemProperties for headset identification
                XrSystemProperties sysProps = { XR_TYPE_SYSTEM_PROPERTIES };
                if (XR_SUCCEEDED(xrGetSystemProperties(xrInst, sysId, &sysProps))) {
                    m_xrSystemName = sysProps.systemName;
                    ALOGI("OpenXR System Name: '%s' (vendorId=%u)", sysProps.systemName, sysProps.vendorId);
                }

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
                    EGL_DEPTH_SIZE, 24,
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

                    // 1. Create 1920x1080 (Full HD 16:9) Quad Swapchain for Live Desktop Screen & 1280x800 VR Menu
                    XrSwapchainCreateInfo scInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
                    scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
                    scInfo.format = chosenFormat;
                    scInfo.sampleCount = 1;
                    scInfo.width = 1920;
                    scInfo.height = 1080;
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
                        ALOGI("Created 1920x1080 OpenXR Quad Swapchain with %u GLES textures", imgCount);
                    }

                    // 2. Create 1024x1024 Stereo Projection Swapchain for 3D Environment, 3D Touch Controllers & 3D Laser Beam
                    m_skyWidth = 1024;
                    m_skyHeight = 1024;
                    scInfo.width = m_skyWidth;
                    scInfo.height = m_skyHeight;
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
                        ALOGI("Created %ux%u OpenXR Stereo 3D Scene Swapchain with %u GLES textures",
                              m_skyWidth, m_skyHeight, imgCount);
                    }

                    InitStereoSceneResources();

                    // Try loading optional custom .glb environment from /sdcard/ProjectVXR/Environments/
                    VxrGltfLoader gltfLoader;
                    gltfLoader.LoadEnvironmentByIndex(0, &m_customEnvModel);

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
                        XrAction gripAct  = createAct(XR_ACTION_TYPE_POSE_INPUT,     "grip_pose",  "Controller Grip Pose");
                        XrAction trigAct  = createAct(XR_ACTION_TYPE_FLOAT_INPUT,    "trigger",    "Trigger Pull");
                        XrAction sqzAct   = createAct(XR_ACTION_TYPE_FLOAT_INPUT,    "squeeze",    "Grip Squeeze");
                        XrAction thumbAct = createAct(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick");
                        XrAction btnAAct  = createAct(XR_ACTION_TYPE_BOOLEAN_INPUT,  "button_a",   "Button A / X");
                        XrAction btnBAct  = createAct(XR_ACTION_TYPE_BOOLEAN_INPUT,  "button_b",   "Button B / Y");
                        XrAction menuAct  = createAct(XR_ACTION_TYPE_BOOLEAN_INPUT,  "menu_btn",   "Menu Button");

                        m_aimPoseAction    = reinterpret_cast<void*>(aimAct);
                        m_gripPoseAction   = reinterpret_cast<void*>(gripAct);
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
                            bindPath(gripAct,  "/user/hand/left/input/grip/pose"),
                            bindPath(gripAct,  "/user/hand/right/input/grip/pose"),
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

                        aspInfo.subactionPath = m_handPaths[1]; // Right hand aim
                        XrSpace rSpace = XR_NULL_HANDLE;
                        xrCreateActionSpace(xrSess, &aspInfo, &rSpace);
                        m_rightAimSpace = reinterpret_cast<void*>(rSpace);

                        aspInfo.subactionPath = m_handPaths[0]; // Left hand aim
                        XrSpace lSpace = XR_NULL_HANDLE;
                        xrCreateActionSpace(xrSess, &aspInfo, &lSpace);
                        m_leftAimSpace = reinterpret_cast<void*>(lSpace);

                        aspInfo.action = gripAct;
                        aspInfo.subactionPath = m_handPaths[1]; // Right hand grip
                        XrSpace rgSpace = XR_NULL_HANDLE;
                        xrCreateActionSpace(xrSess, &aspInfo, &rgSpace);
                        m_rightGripSpace = reinterpret_cast<void*>(rgSpace);

                        aspInfo.subactionPath = m_handPaths[0]; // Left hand grip
                        XrSpace lgSpace = XR_NULL_HANDLE;
                        xrCreateActionSpace(xrSess, &aspInfo, &lgSpace);
                        m_leftGripSpace = reinterpret_cast<void*>(lgSpace);

                        XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
                        attachInfo.countActionSets = 1;
                        attachInfo.actionSets = &actSet;
                        xrAttachSessionActionSets(xrSess, &attachInfo);
                    }

                    m_realXrActive = true;
                    m_initialized = true;
                    m_sessionRunning = true;
                    m_sessionState = OpenXrSessionState::Focused;
                    m_screenCenterY = 0.0f;
                    m_screenAnchorInitialized = false;

                    // Pump initial events so if READY is already queued, xrBeginSession runs immediately
                    PollXrEvents();
                    ALOGI("Real Meta Quest OpenXR Session initialized and ready! Model='%s'",
                          GetDetectedHeadsetModel().c_str());
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
    m_screenCenterY = 1.40f;
    m_screenAnchorInitialized = true;

    std::memset(&m_controllerState, 0, sizeof(m_controllerState));
    m_controllerState.ray_origin[0] = 0.0f;
    m_controllerState.ray_origin[1] = m_screenCenterY;
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

void VxrOpenXrClient::InitStereoSceneResources() {
#if defined(__ANDROID__)
    DestroyStereoSceneResources();

    glGenFramebuffers(1, &m_skyFbo);
    glGenRenderbuffers(1, &m_skyDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_skyDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          static_cast<GLsizei>(m_skyWidth), static_cast<GLsizei>(m_skyHeight));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    const char* vsSrc =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aNormal;\n"
        "uniform mat4 uViewProj;\n"
        "uniform mat4 uModel;\n"
        "out vec3 vNormal;\n"
        "void main() {\n"
        "    vNormal = mat3(uModel) * aNormal;\n"
        "    gl_Position = uViewProj * (uModel * vec4(aPos, 1.0));\n"
        "}\n";

    const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vNormal;\n"
        "uniform vec4 uColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    vec3 n = normalize(vNormal + vec3(0.0, 0.0001, 0.0));\n"
        "    vec3 lightDir = normalize(vec3(0.35, 0.85, 0.50));\n"
        "    float diff = max(dot(n, lightDir), 0.0);\n"
        "    float lighting = 0.48 + 0.52 * diff;\n"
        "    fragColor = vec4(uColor.rgb * lighting, uColor.a);\n"
        "}\n";

    GLuint vs = CompileGlShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileGlShader(GL_FRAGMENT_SHADER, fsSrc);
    m_sceneProgram = glCreateProgram();
    glAttachShader(m_sceneProgram, vs);
    glAttachShader(m_sceneProgram, fs);
    glLinkProgram(m_sceneProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    m_uViewProjLoc = glGetUniformLocation(m_sceneProgram, "uViewProj");
    m_uModelLoc    = glGetUniformLocation(m_sceneProgram, "uModel");
    m_uColorLoc    = glGetUniformLocation(m_sceneProgram, "uColor");
#endif
}

void VxrOpenXrClient::DestroyStereoSceneResources() {
#if defined(__ANDROID__)
    if (m_sceneProgram != 0) {
        glDeleteProgram(m_sceneProgram);
        m_sceneProgram = 0;
    }
    if (m_skyDepthRbo != 0) {
        glDeleteRenderbuffers(1, &m_skyDepthRbo);
        m_skyDepthRbo = 0;
    }
    if (m_skyFbo != 0) {
        glDeleteFramebuffers(1, &m_skyFbo);
        m_skyFbo = 0;
    }
#endif
}

void VxrOpenXrClient::RenderStereoEnvironmentAndControllers(uint32_t skyTexId) {
#if defined(__ANDROID__)
    if (m_skyFbo == 0 || m_sceneProgram == 0 || skyTexId == 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_skyFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, skyTexId, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_skyDepthRbo);

    glViewport(0, 0, static_cast<GLsizei>(m_skyWidth), static_cast<GLsizei>(m_skyHeight));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (m_passthroughEnabled) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glClearColor(0.055f, 0.065f, 0.090f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_sceneProgram);

    auto drawVerts = [&](const std::vector<Vertex3D>& verts, const float modelMat[16],
                         float r, float g, float b, float a = 1.0f) {
        if (verts.empty()) return;
        glUniformMatrix4fv(m_uModelLoc, 1, GL_FALSE, modelMat);
        glUniform4f(m_uColorLoc, r, g, b, a);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), &verts[0].pos[0]);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), &verts[0].norm[0]);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    };

    float identityMat[16];
    Mat4Identity(identityMat);

    // Build 3D Environment Room geometry (Floor grid + architectural theater frame)
    std::vector<Vertex3D> roomFloorVerts;
    std::vector<Vertex3D> roomGridVerts;
    std::vector<Vertex3D> roomFrameVerts;
    if (!m_passthroughEnabled) {
        const float floorY = m_screenCenterY - 1.25f;
        AppendOrientedBox(&roomFloorVerts, 0.0f, floorY - 0.02f, -1.5f, 4.5f, 0.02f, 4.5f);

        // Floor architectural grid strips
        for (int i = -4; i <= 4; ++i) {
            float offset = static_cast<float>(i) * 0.75f;
            AppendOrientedBox(&roomGridVerts, offset, floorY + 0.002f, -1.5f, 0.006f, 0.002f, 3.2f);
            AppendOrientedBox(&roomGridVerts, 0.0f, floorY + 0.002f, -1.5f + offset, 3.2f, 0.002f, 0.006f);
        }

        // Architectural cinema frame behind screen quad
        const float backZ = -m_quadDistM - 0.12f;
        AppendOrientedBox(&roomFrameVerts, -1.45f, m_screenCenterY, backZ, 0.06f, 1.05f, 0.06f);
        AppendOrientedBox(&roomFrameVerts,  1.45f, m_screenCenterY, backZ, 0.06f, 1.05f, 0.06f);
        AppendOrientedBox(&roomFrameVerts,  0.0f,  m_screenCenterY + 1.02f, backZ, 1.51f, 0.05f, 0.06f);
        AppendOrientedBox(&roomFrameVerts,  0.0f,  m_screenCenterY - 1.02f, backZ, 1.51f, 0.05f, 0.06f);
    }

    // Build custom .glb mesh vertices if a custom room was loaded
    std::vector<Vertex3D> customGlbVerts;
    if (!m_passthroughEnabled && m_customEnvModel.valid && !m_customEnvModel.meshes.empty()) {
        for (const auto& mesh : m_customEnvModel.meshes) {
            for (uint32_t idx : mesh.indices) {
                if (idx < mesh.vertices.size()) {
                    Vertex3D v = {};
                    v.pos[0]  = mesh.vertices[idx].pos[0];
                    v.pos[1]  = mesh.vertices[idx].pos[1] + m_screenCenterY - 1.25f;
                    v.pos[2]  = mesh.vertices[idx].pos[2];
                    v.norm[0] = mesh.vertices[idx].normal[0];
                    v.norm[1] = mesh.vertices[idx].normal[1];
                    v.norm[2] = mesh.vertices[idx].normal[2];
                    customGlbVerts.push_back(v);
                }
            }
        }
    }

    // Render Side-by-Side Stereo Views (eye 0 = left half [0..512], eye 1 = right half [512..1024])
    const GLsizei halfW = static_cast<GLsizei>(m_skyWidth / 2);
    const GLsizei fullH = static_cast<GLsizei>(m_skyHeight);

    for (int eye = 0; eye < 2; ++eye) {
        glViewport(eye * halfW, 0, halfW, fullH);

        float projMat[16], viewMat[16], viewProjMat[16];
        Mat4CreateProjectionFromFov(projMat,
                                    m_cachedViews[eye].fov_left,
                                    m_cachedViews[eye].fov_right,
                                    m_cachedViews[eye].fov_top,
                                    m_cachedViews[eye].fov_bottom,
                                    0.05f, 100.0f);
        Mat4CreateViewFromEyePose(viewMat, m_cachedViews[eye].orientation, m_cachedViews[eye].position);
        Mat4Multiply(viewProjMat, projMat, viewMat);
        glUniformMatrix4fv(m_uViewProjLoc, 1, GL_FALSE, viewProjMat);

        // 1. Draw 3D Environment Room
        if (!m_passthroughEnabled) {
            drawVerts(roomFloorVerts, identityMat, 0.08f, 0.10f, 0.14f);
            drawVerts(roomGridVerts,  identityMat, 0.14f, 0.22f, 0.34f);
            drawVerts(roomFrameVerts, identityMat, 0.12f, 0.15f, 0.21f);
            if (!customGlbVerts.empty()) {
                drawVerts(customGlbVerts, identityMat, 0.20f, 0.24f, 0.32f);
            }
        }

        // 2. Draw Left & Right 3D Quest Touch Controller Models
        for (int hand = 0; hand < 2; ++hand) {
            const XrControllerPose& cp = (hand == 0) ? m_leftControllerPose : m_rightControllerPose;
            float ctrlPos[3] = { cp.position[0], cp.position[1], cp.position[2] };
            float ctrlQuat[4] = { cp.orientation[0], cp.orientation[1], cp.orientation[2], cp.orientation[3] };
            if (!cp.is_tracked) {
                // Default ergonomic resting pose in front of user if controller is idle
                ctrlPos[0] = (hand == 0) ? -0.18f : 0.18f;
                ctrlPos[1] = m_screenCenterY - 0.26f;
                ctrlPos[2] = -0.36f;
                ctrlQuat[0] = 0.0f; ctrlQuat[1] = 0.0f; ctrlQuat[2] = 0.0f; ctrlQuat[3] = 1.0f;
            }

            float ctrlModelMat[16];
            Mat4CreateFromQuatPos(ctrlModelMat, ctrlQuat, ctrlPos);

            const bool isActiveHand = (hand == m_activeHand);
            const float trigPull = isActiveHand ? (m_triggerPressed ? 1.0f : m_controllerState.trigger_value) : 0.0f;
            const float gripPull = isActiveHand ? (m_gripPressed ? 1.0f : m_controllerState.grip_value) : 0.0f;
            const float stickX   = isActiveHand ? m_thumbstickX : 0.0f;
            const float stickY   = isActiveHand ? m_thumbstickY : 0.0f;
            const float innerSign = (hand == 0) ? 1.0f : -1.0f;

            std::vector<Vertex3D> bodyShellVerts;
            std::vector<Vertex3D> faceplateVerts;
            std::vector<Vertex3D> controlsVerts;

            // (a) Ergonomic angled grip handle + outer body shell (#E8ECF2 matte pearl white/gray)
            AppendOrientedBox(&bodyShellVerts, 0.0f, -0.028f, 0.042f, 0.017f, 0.052f, 0.019f, -0.45f, 0.0f);
            AppendCylinderY(&bodyShellVerts,   0.0f,  0.002f, -0.005f, 0.029f, 0.008f, 14);

            // (b) Circular dark slate top faceplate (#242936)
            AppendCylinderY(&faceplateVerts, 0.0f, 0.007f, -0.005f, 0.025f, 0.004f, 14);

            // (c) Thumbstick stalk & cap tilting with m_thumbstickX / m_thumbstickY
            const float stickXOff = -innerSign * 0.007f;
            AppendCylinderY(&controlsVerts, stickXOff, 0.016f, -0.008f, 0.0035f, 0.007f, 8,
                            -stickY * 0.35f, -stickX * 0.35f);
            AppendCylinderY(&controlsVerts,
                            stickXOff + stickX * 0.003f, 0.023f, -0.008f - stickY * 0.003f,
                            0.0085f, 0.0025f, 10, -stickY * 0.35f, -stickX * 0.35f);

            // (d) A/B or X/Y Face buttons
            AppendCylinderY(&controlsVerts, innerSign * 0.008f, 0.012f, -0.001f, 0.0045f, 0.002f, 8);
            AppendCylinderY(&controlsVerts, innerSign * 0.005f, 0.012f, -0.013f, 0.0045f, 0.002f, 8);

            // (e) Index Trigger on front underside that rotates/depresses with trigger pull
            AppendOrientedBox(&controlsVerts, 0.0f, -0.012f, -0.026f + trigPull * 0.005f,
                              0.007f, 0.012f, 0.005f, 0.30f + trigPull * 0.38f, 0.0f);

            // (f) Side Grip button on inner handle that depresses with grip squeeze
            AppendOrientedBox(&controlsVerts, innerSign * (0.016f - gripPull * 0.003f), -0.026f, 0.036f,
                              0.003f, 0.018f, 0.008f, -0.45f, 0.0f);

            // Render #E8ECF2 body shell, #242936 top faceplate, and dark controls
            drawVerts(bodyShellVerts, ctrlModelMat, 0.910f, 0.925f, 0.949f);
            drawVerts(faceplateVerts, ctrlModelMat, 0.141f, 0.161f, 0.212f);
            drawVerts(controlsVerts,  ctrlModelMat, 0.220f, 0.255f, 0.330f);
        }

        // 3. Draw 3D Glowing Laser Beam in DodgerBlue (#1E90FF) from active controller tip to m_laserHitPoint
        if (m_controllerState.is_active) {
            const float ox = m_controllerState.ray_origin[0];
            const float oy = m_controllerState.ray_origin[1];
            const float oz = m_controllerState.ray_origin[2];
            const float hx = m_laserHitPoint[0];
            const float hy = m_laserHitPoint[1];
            const float hz = m_laserHitPoint[2];

            // Build thin 3D rectangular prism beam between (ox, oy, oz) and (hx, hy, hz)
            const float r = 0.0022f;
            std::vector<Vertex3D> beamVerts;
            const Vertex3D v[8] = {
                { { ox - r, oy - r, oz }, { 0, 1, 0 } },
                { { ox + r, oy - r, oz }, { 0, 1, 0 } },
                { { ox + r, oy + r, oz }, { 0, 1, 0 } },
                { { ox - r, oy + r, oz }, { 0, 1, 0 } },
                { { hx - r, hy - r, hz }, { 0, 1, 0 } },
                { { hx + r, hy - r, hz }, { 0, 1, 0 } },
                { { hx + r, hy + r, hz }, { 0, 1, 0 } },
                { { hx - r, hy + r, hz }, { 0, 1, 0 } }
            };
            const int idx[24] = {
                0, 1, 5,  0, 5, 4,
                1, 2, 6,  1, 6, 5,
                2, 3, 7,  2, 7, 6,
                3, 0, 4,  3, 4, 7
            };
            for (int k = 0; k < 24; ++k) {
                beamVerts.push_back(v[idx[k]]);
            }
            // DodgerBlue (#1E90FF -> 0.118, 0.565, 1.0)
            drawVerts(beamVerts, identityMat, 0.18f, 0.64f, 1.0f);

            // 3D glowing cursor dot at m_laserHitPoint
            std::vector<Vertex3D> hitDotVerts;
            AppendOrientedBox(&hitDotVerts, hx, hy, hz + 0.008f, 0.012f, 0.012f, 0.004f);
            drawVerts(hitDotVerts, identityMat, 0.35f, 0.78f, 1.0f);
        }
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
#else
    (void)skyTexId;
#endif
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
        } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            // User recentered headset via Meta button long-press -> re-anchor screen Y on next LocateViews
            m_recenterHeld = true;
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

            m_headPose = out_views[0];
            m_headPose.position[0] = 0.5f * (out_views[0].position[0] + out_views[1].position[0]);
            m_headPose.position[1] = 0.5f * (out_views[0].position[1] + out_views[1].position[1]);
            m_headPose.position[2] = 0.5f * (out_views[0].position[2] + out_views[1].position[2]);

            // Anchor screen center Y in LOCAL space to the user's actual eye height on first valid head pose or Recenter
            if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0) {
                if (!m_screenAnchorInitialized || m_recenterHeld) {
                    m_screenCenterY = m_headPose.position[1];
                    m_screenAnchorInitialized = true;
                    m_recenterHeld = false;
                    ALOGI("Anchored VR Screen Center Y to LOCAL head height: %.3fm", m_screenCenterY);
                }
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
    m_headPose = out_views[0];
    m_headPose.position[0] = 0.0f;
    return true;
}

static uint32_t g_activeQuadW = 1280;
static uint32_t g_activeQuadH = 800;

void VxrOpenXrClient::SubmitQuadSwapchainRgba(const uint32_t* rgbaPixels,
                                              uint32_t width,
                                              uint32_t height,
                                              float screenDistM,
                                              float screenScale) {
    if (width > 1920) width = 1920;
    if (height > 1080) height = 1080;
    if (width > 0 && height > 0) {
        g_activeQuadW = width;
        g_activeQuadH = height;
    }
    m_quadDistM = (screenDistM > 0.5f) ? screenDistM : 2.0f;
    m_quadWidthM = 2.0f * ((screenScale > 0.2f) ? screenScale : 1.0f);
    m_quadHeightM = m_quadWidthM * (static_cast<float>(height) / static_cast<float>(width));

#if defined(__ANDROID__)
    if (!m_realXrActive || !rgbaPixels || width == 0 || height == 0) return;

    // 1. Upload RGBA buffer (1920x1080 Native Desktop or 1280x800 VR Menu) into m_quadSwapchain
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

    // 2. Render 3D Stereo Environment, 3D Quest Touch Controllers & 3D Laser Beam into m_skySwapchain (1024x1024)
    if (m_skySwapchain && !m_skySwapchainTexIds.empty()) {
        XrSwapchain skySc = reinterpret_cast<XrSwapchain>(m_skySwapchain);
        XrSwapchainImageAcquireInfo acqInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        uint32_t imgIdx = 0;
        if (XR_SUCCEEDED(xrAcquireSwapchainImage(skySc, &acqInfo, &imgIdx))) {
            XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            waitInfo.timeout = XR_INFINITE_DURATION;
            if (XR_SUCCEEDED(xrWaitSwapchainImage(skySc, &waitInfo))) {
                if (imgIdx < m_skySwapchainTexIds.size()) {
                    RenderStereoEnvironmentAndControllers(m_skySwapchainTexIds[imgIdx]);
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

        // Layer 0: Stereo Projection Layer (1024x1024 side-by-side stereo: 3D Environment + 3D Controllers + 3D Laser)
        XrCompositionLayerProjectionView projViews[2] = {
            { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW },
            { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }
        };
        XrCompositionLayerProjection projLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        if (m_skySwapchain && m_shouldRenderFrame) {
            XrSwapchain skySc = reinterpret_cast<XrSwapchain>(m_skySwapchain);
            const int32_t halfW = static_cast<int32_t>(m_skyWidth / 2);
            const int32_t fullH = static_cast<int32_t>(m_skyHeight);
            for (int i = 0; i < 2; ++i) {
                projViews[i].pose.position = { m_cachedViews[i].position[0], m_cachedViews[i].position[1], m_cachedViews[i].position[2] };
                projViews[i].pose.orientation = { m_cachedViews[i].orientation[0], m_cachedViews[i].orientation[1], m_cachedViews[i].orientation[2], m_cachedViews[i].orientation[3] };
                projViews[i].fov = { m_cachedViews[i].fov_left, m_cachedViews[i].fov_right, m_cachedViews[i].fov_top, m_cachedViews[i].fov_bottom };
                projViews[i].subImage.swapchain = skySc;
                projViews[i].subImage.imageRect.offset = { i * halfW, 0 };
                projViews[i].subImage.imageRect.extent = { halfW, fullH };
                projViews[i].subImage.imageArrayIndex = 0;
            }
            projLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            projLayer.space = localSp;
            projLayer.viewCount = 2;
            projLayer.views = projViews;
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer);
        }

        // Layer 1: High-Resolution 1920x1080 Native Desktop / 1280x800 VR Menu Cinema Quad in 3D VR Space at Y = m_screenCenterY
        XrCompositionLayerQuad quadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
        if (m_quadSwapchain && m_shouldRenderFrame) {
            XrSwapchain quadSc = reinterpret_cast<XrSwapchain>(m_quadSwapchain);
            quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quadLayer.space = localSp;
            quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quadLayer.subImage.swapchain = quadSc;
            quadLayer.subImage.imageRect.offset = { 0, 0 };
            quadLayer.subImage.imageRect.extent = { static_cast<int32_t>(g_activeQuadW), static_cast<int32_t>(g_activeQuadH) };
            quadLayer.subImage.imageArrayIndex = 0;
            quadLayer.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
            quadLayer.pose.position = { 0.0f, m_screenCenterY, -m_quadDistM };
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
            auto getFloatHand = [&](void* actHandle, XrPath subPath) -> float {
                if (!actHandle) return 0.0f;
                XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
                gi.action = reinterpret_cast<XrAction>(actHandle);
                gi.subactionPath = subPath;
                XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
                if (XR_SUCCEEDED(xrGetActionStateFloat(xrSess, &gi, &st)) && st.isActive) {
                    return st.currentState;
                }
                return 0.0f;
            };
            auto getBoolHand = [&](void* actHandle, XrPath subPath) -> bool {
                if (!actHandle) return false;
                XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
                gi.action = reinterpret_cast<XrAction>(actHandle);
                gi.subactionPath = subPath;
                XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
                if (XR_SUCCEEDED(xrGetActionStateBoolean(xrSess, &gi, &st)) && st.isActive) {
                    return st.currentState == XR_TRUE;
                }
                return false;
            };

            const float leftTrig  = getFloatHand(m_triggerAction, m_handPaths[0]);
            const float rightTrig = getFloatHand(m_triggerAction, m_handPaths[1]);
            const float anyTrig   = getFloatHand(m_triggerAction, XR_NULL_PATH);

            const float leftGrip  = getFloatHand(m_squeezeAction, m_handPaths[0]);
            const float rightGrip = getFloatHand(m_squeezeAction, m_handPaths[1]);
            const float anyGrip   = getFloatHand(m_squeezeAction, XR_NULL_PATH);

            const bool btnX = getBoolHand(m_buttonAAction, m_handPaths[0]);
            const bool btnA = getBoolHand(m_buttonAAction, m_handPaths[1]) || getBoolHand(m_buttonAAction, XR_NULL_PATH);
            const bool btnY = getBoolHand(m_buttonBAction, m_handPaths[0]);
            const bool btnB = getBoolHand(m_buttonBAction, m_handPaths[1]) || getBoolHand(m_buttonBAction, XR_NULL_PATH);
            const bool btnMenu = getBoolHand(m_menuAction, m_handPaths[0]) || getBoolHand(m_menuAction, XR_NULL_PATH);

            m_controllerState.trigger_value = (std::max)(anyTrig, (std::max)(leftTrig, rightTrig));
            m_controllerState.grip_value    = (std::max)(anyGrip, (std::max)(leftGrip, rightGrip));
            m_controllerState.button_a      = btnA || btnX;
            m_controllerState.button_x      = btnX;
            m_controllerState.button_b      = btnB;
            m_controllerState.button_y      = btnY;
            m_controllerState.button_menu   = btnMenu || btnY;

            m_triggerPressed = (m_controllerState.trigger_value > 0.50f) || btnA || btnX;
            m_gripPressed    = (m_controllerState.grip_value > 0.50f);

            if (m_thumbstickAction) {
                float sx = 0.0f, sy = 0.0f;
                for (int h = 0; h < 2; ++h) {
                    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
                    gi.action = reinterpret_cast<XrAction>(m_thumbstickAction);
                    gi.subactionPath = m_handPaths[h];
                    XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
                    if (XR_SUCCEEDED(xrGetActionStateVector2f(xrSess, &gi, &st)) && st.isActive) {
                        if (std::abs(st.currentState.x) > std::abs(sx)) sx = st.currentState.x;
                        if (std::abs(st.currentState.y) > std::abs(sy)) sy = st.currentState.y;
                    }
                }
                m_controllerState.thumbstick_x = sx;
                m_controllerState.thumbstick_y = sy;
                m_thumbstickX = sx;
                m_thumbstickY = sy;
            }

            // Locate 6DoF poses for BOTH Left (hand=0) and Right (hand=1) controllers in LOCAL space
            XrSpace aimSpaces[2] = {
                reinterpret_cast<XrSpace>(m_leftAimSpace),
                reinterpret_cast<XrSpace>(m_rightAimSpace)
            };
            XrControllerPose* handPoses[2] = { &m_leftControllerPose, &m_rightControllerPose };

            for (int h = 0; h < 2; ++h) {
                handPoses[h]->is_tracked = false;
                if (!aimSpaces[h]) continue;
                XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
                if (XR_SUCCEEDED(xrLocateSpace(aimSpaces[h], localSp,
                                               static_cast<XrTime>(m_lastPredictedDisplayTimeNs), &loc))) {
                    if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                        (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                        handPoses[h]->position[0] = loc.pose.position.x;
                        handPoses[h]->position[1] = loc.pose.position.y;
                        handPoses[h]->position[2] = loc.pose.position.z;
                        handPoses[h]->orientation[0] = loc.pose.orientation.x;
                        handPoses[h]->orientation[1] = loc.pose.orientation.y;
                        handPoses[h]->orientation[2] = loc.pose.orientation.z;
                        handPoses[h]->orientation[3] = loc.pose.orientation.w;
                        handPoses[h]->is_tracked = true;
                    }
                }
            }

            // Active hand selection: switch to Left if Left trigger/X pressed or only Left tracked; else Right
            if ((leftTrig > 0.50f || btnX) && m_leftControllerPose.is_tracked) {
                m_activeHand = 0;
            } else if ((rightTrig > 0.50f || btnA) && m_rightControllerPose.is_tracked) {
                m_activeHand = 1;
            } else if (m_rightControllerPose.is_tracked && !m_leftControllerPose.is_tracked) {
                m_activeHand = 1;
            } else if (m_leftControllerPose.is_tracked && !m_rightControllerPose.is_tracked) {
                m_activeHand = 0;
            }

            const XrControllerPose& activePose = (m_activeHand == 0) ? m_leftControllerPose : m_rightControllerPose;
            if (activePose.is_tracked) {
                m_controllerState.ray_origin[0] = activePose.position[0];
                m_controllerState.ray_origin[1] = activePose.position[1];
                m_controllerState.ray_origin[2] = activePose.position[2];

                const float qx = activePose.orientation[0];
                const float qy = activePose.orientation[1];
                const float qz = activePose.orientation[2];
                const float qw = activePose.orientation[3];

                // Forward vector (-Z) rotated by quaternion q
                m_controllerState.ray_direction[0] = -2.0f * (qx * qz + qw * qy);
                m_controllerState.ray_direction[1] = -2.0f * (qy * qz - qw * qx);
                m_controllerState.ray_direction[2] = -(1.0f - 2.0f * (qx * qx + qy * qy));
                m_controllerState.is_active = true;
            } else {
                // If no controller is turned on yet, use head gaze ray from m_headPose
                m_controllerState.ray_origin[0] = m_headPose.position[0];
                m_controllerState.ray_origin[1] = m_headPose.position[1];
                m_controllerState.ray_origin[2] = m_headPose.position[2];
                const float qx = m_headPose.orientation[0];
                const float qy = m_headPose.orientation[1];
                const float qz = m_headPose.orientation[2];
                const float qw = m_headPose.orientation[3];
                m_controllerState.ray_direction[0] = -2.0f * (qx * qz + qw * qy);
                m_controllerState.ray_direction[1] = -2.0f * (qy * qz - qw * qx);
                m_controllerState.ray_direction[2] = -(1.0f - 2.0f * (qx * qx + qy * qy));
                m_controllerState.is_active = true;
            }
        }
    }
#endif

    m_controllerState.trigger_clicked = (m_controllerState.trigger_value > 0.50f) ||
                                        m_controllerState.button_a ||
                                        m_controllerState.button_x;
    m_controllerState.grip_clicked    = (m_controllerState.grip_value > 0.50f);
    m_triggerPressed = m_controllerState.trigger_clicked;
    m_gripPressed    = m_controllerState.grip_clicked;

    if (out_state) {
        *out_state = m_controllerState;
    }
    return m_controllerState.is_active;
}

void VxrOpenXrClient::SetControllerInputState(const XrControllerInputState& state) {
    m_controllerState = state;
    m_controllerState.trigger_clicked = state.trigger_clicked ||
                                        (state.trigger_value > 0.50f) ||
                                        state.button_a ||
                                        state.button_x;
    m_controllerState.grip_clicked = state.grip_clicked || (state.grip_value > 0.50f);
    m_triggerPressed = m_controllerState.trigger_clicked;
    m_gripPressed    = m_controllerState.grip_clicked;
    m_thumbstickX    = state.thumbstick_x;
    m_thumbstickY    = state.thumbstick_y;
}

bool VxrOpenXrClient::ComputeDesktopPointerUV(float screenDistM, float* outU, float* outV) const {
    if (m_realXrActive) {
        return ComputeDesktopPointerUV(m_quadDistM, m_quadWidthM, m_quadHeightM, m_screenCenterY, outU, outV);
    }
    return ComputeDesktopPointerUV(screenDistM, 2.0f, 1.125f, m_screenCenterY, outU, outV);
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

    const float planeZ = (screenDistM > 0.0f) ? -screenDistM : screenDistM;
    const float dirZ = m_controllerState.ray_direction[2];
    if (std::abs(dirZ) < 1e-6f) {
        *outU = 0.5f;
        *outV = 0.5f;
        m_hasLaserHit = false;
        return false;
    }

    const float t = (planeZ - m_controllerState.ray_origin[2]) / dirZ;
    if (t <= 0.0f) {
        *outU = 0.5f;
        *outV = 0.5f;
        m_hasLaserHit = false;
        return false;
    }

    const float hitX = m_controllerState.ray_origin[0] + t * m_controllerState.ray_direction[0];
    const float hitY = m_controllerState.ray_origin[1] + t * m_controllerState.ray_direction[1];

    m_laserHitPoint[0] = hitX;
    m_laserHitPoint[1] = hitY;
    m_laserHitPoint[2] = planeZ;

    const float halfW = screenWidthM * 0.5f;
    const float halfH = screenHeightM * 0.5f;
    const float minX = -halfW;
    const float maxX =  halfW;
    const float minY = screenCenterY - halfH;
    const float maxY = screenCenterY + halfH;

    const float rawU = (hitX - minX) / (maxX - minX);
    const float rawV = (maxY - hitY) / (maxY - minY);

    const bool inside = (rawU >= 0.0f && rawU <= 1.0f && rawV >= 0.0f && rawV <= 1.0f);
    m_hasLaserHit = inside;
    *outU = (std::max)(0.0f, (std::min)(1.0f, rawU));
    *outV = (std::max)(0.0f, (std::min)(1.0f, rawV));
    return inside;
}

std::string VxrOpenXrClient::GetDetectedHeadsetModel() const {
#if defined(__ANDROID__)
    char devProp[PROP_VALUE_MAX] = {};
    char modProp[PROP_VALUE_MAX] = {};
    __system_property_get("ro.product.device", devProp);
    __system_property_get("ro.product.model", modProp);

    std::string dev(devProp);
    std::string mod(modProp);
    for (char& c : dev) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }

    if (dev.find("eureka") != std::string::npos)    return "Meta Quest 3";
    if (dev.find("panther") != std::string::npos)   return "Meta Quest 3S";
    if (dev.find("hollywood") != std::string::npos) return "Meta Quest 2";
    if (dev.find("seacliff") != std::string::npos)  return "Meta Quest Pro";
    if (dev.find("monterey") != std::string::npos)  return "Oculus Quest";

    if (!m_xrSystemName.empty()) return m_xrSystemName;
    if (!mod.empty()) return mod;
    return "Meta Quest Headset";
#else
    if (!m_xrSystemName.empty()) return m_xrSystemName;
    return "Meta Quest 3";
#endif
}

uint32_t VxrOpenXrClient::GetDeviceBatteryPercent() const {
#if defined(__ANDROID__)
    static const char* kBatteryPaths[] = {
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/bms/capacity"
    };
    for (const char* path : kBatteryPaths) {
        std::ifstream f(path);
        if (f.is_open()) {
            int pct = 0;
            if (f >> pct) {
                if (pct < 1) pct = 1;
                if (pct > 100) pct = 100;
                return static_cast<uint32_t>(pct);
            }
        }
    }
#endif
    return 100u;
}

std::string VxrOpenXrClient::GetLocalClockString() const {
    std::time_t now = std::time(nullptr);
    std::tm localTm = {};
#if defined(_WIN32)
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif
    int hour12 = localTm.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = (localTm.tm_hour >= 12) ? "PM" : "AM";
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, localTm.tm_min, ampm);
    return std::string(buf);
}

bool VxrOpenXrClient::LoadCustomEnvironmentGlb(const std::string& glbPath) {
    VxrGltfLoader loader;
    return loader.LoadFromFile(glbPath, &m_customEnvModel);
}

void VxrOpenXrClient::Cleanup() {
    EndSession();
#if defined(__ANDROID__)
    if (m_realXrActive) {
        DestroyStereoSceneResources();
        if (m_rightGripSpace) { xrDestroySpace(reinterpret_cast<XrSpace>(m_rightGripSpace)); m_rightGripSpace = nullptr; }
        if (m_leftGripSpace)  { xrDestroySpace(reinterpret_cast<XrSpace>(m_leftGripSpace));  m_leftGripSpace = nullptr; }
        if (m_rightAimSpace)  { xrDestroySpace(reinterpret_cast<XrSpace>(m_rightAimSpace));  m_rightAimSpace = nullptr; }
        if (m_leftAimSpace)   { xrDestroySpace(reinterpret_cast<XrSpace>(m_leftAimSpace));   m_leftAimSpace = nullptr; }
        if (m_actionSet)      { xrDestroyActionSet(reinterpret_cast<XrActionSet>(m_actionSet)); m_actionSet = nullptr; }
        if (m_quadSwapchain)  { xrDestroySwapchain(reinterpret_cast<XrSwapchain>(m_quadSwapchain)); m_quadSwapchain = nullptr; }
        if (m_skySwapchain)   { xrDestroySwapchain(reinterpret_cast<XrSwapchain>(m_skySwapchain));  m_skySwapchain = nullptr; }
        if (m_localSpace)     { xrDestroySpace(reinterpret_cast<XrSpace>(m_localSpace)); m_localSpace = nullptr; }
        if (m_viewSpace)      { xrDestroySpace(reinterpret_cast<XrSpace>(m_viewSpace));  m_viewSpace = nullptr; }
        if (m_session)        { xrDestroySession(reinterpret_cast<XrSession>(m_session)); m_session = nullptr; }
        if (m_instance)       { xrDestroyInstance(reinterpret_cast<XrInstance>(m_instance)); m_instance = nullptr; }
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
