#include "android_native_app_glue.h"
#include <android/log.h>
#include <android/window.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>


static const char* TAG = "hello_quest";

#define error(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef NDEBUG
#define info(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
#endif // NDEBUG

#define XRCMD(cmd) \
{                  \
    int code = cmd;               \
    if (!XR_SUCCEEDED(code)) { \
        error("%s failed: %i", #cmd, code);               \
    }              \
}

#define GL(stmt) \
{    \
    stmt;\
    GLenum err = glGetError();\
    if (err != GL_NO_ERROR) {\
        error("OpenGL error %08x, at %s:%i - for %s", err, __FILE__, __LINE__, #stmt);\
    }\
}

//#define GL(stmt) #stmt

typedef struct XrMatrix4x4f {
    float m[16];
} XrMatrix4x4f;

inline static void XrMatrix4x4f_CreateProjection(XrMatrix4x4f* result, const float tanAngleLeft, const float tanAngleRight,
                                                 const float tanAngleUp, float const tanAngleDown,
                                                 const float nearZ, const float farZ) {
    const float tanAngleWidth = tanAngleRight - tanAngleLeft;
    const float tanAngleHeight = (tanAngleUp - tanAngleDown);
    const float offsetZ = nearZ;

    if (farZ <= nearZ) {
        // place the far plane at infinity
        result->m[0] = 2.0f / tanAngleWidth;
        result->m[4] = 0.0f;
        result->m[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
        result->m[12] = 0.0f;

        result->m[1] = 0.0f;
        result->m[5] = 2.0f / tanAngleHeight;
        result->m[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
        result->m[13] = 0.0f;

        result->m[2] = 0.0f;
        result->m[6] = 0.0f;
        result->m[10] = -1.0f;
        result->m[14] = -(nearZ + offsetZ);

        result->m[3] = 0.0f;
        result->m[7] = 0.0f;
        result->m[11] = -1.0f;
        result->m[15] = 0.0f;
    } else {
        // normal projection
        result->m[0] = 2.0f / tanAngleWidth;
        result->m[4] = 0.0f;
        result->m[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
        result->m[12] = 0.0f;

        result->m[1] = 0.0f;
        result->m[5] = 2.0f / tanAngleHeight;
        result->m[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
        result->m[13] = 0.0f;

        result->m[2] = 0.0f;
        result->m[6] = 0.0f;
        result->m[10] = -(farZ + offsetZ) / (farZ - nearZ);
        result->m[14] = -(farZ * (nearZ + offsetZ)) / (farZ - nearZ);

        result->m[3] = 0.0f;
        result->m[7] = 0.0f;
        result->m[11] = -1.0f;
        result->m[15] = 0.0f;
    }
}

inline static void XrMatrix4x4f_CreateProjectionFov(XrMatrix4x4f* result, const XrFovf fov, const float nearZ, const float farZ) {
    const float tanLeft = tanf(fov.angleLeft);
    const float tanRight = tanf(fov.angleRight);

    const float tanDown = tanf(fov.angleDown);
    const float tanUp = tanf(fov.angleUp);

    XrMatrix4x4f_CreateProjection(result, tanLeft, tanRight, tanUp, tanDown, nearZ, farZ);
}

inline static void XrMatrix4x4f_CreateScale(XrMatrix4x4f* result, const float x, const float y, const float z) {
    result->m[0] = x;
    result->m[1] = 0.0f;
    result->m[2] = 0.0f;
    result->m[3] = 0.0f;
    result->m[4] = 0.0f;
    result->m[5] = y;
    result->m[6] = 0.0f;
    result->m[7] = 0.0f;
    result->m[8] = 0.0f;
    result->m[9] = 0.0f;
    result->m[10] = z;
    result->m[11] = 0.0f;
    result->m[12] = 0.0f;
    result->m[13] = 0.0f;
    result->m[14] = 0.0f;
    result->m[15] = 1.0f;
}

inline static void XrMatrix4x4f_CreateFromQuaternion(XrMatrix4x4f* result, const XrQuaternionf* quat) {
    const float x2 = quat->x + quat->x;
    const float y2 = quat->y + quat->y;
    const float z2 = quat->z + quat->z;

    const float xx2 = quat->x * x2;
    const float yy2 = quat->y * y2;
    const float zz2 = quat->z * z2;

    const float yz2 = quat->y * z2;
    const float wx2 = quat->w * x2;
    const float xy2 = quat->x * y2;
    const float wz2 = quat->w * z2;
    const float xz2 = quat->x * z2;
    const float wy2 = quat->w * y2;

    result->m[0] = 1.0f - yy2 - zz2;
    result->m[1] = xy2 + wz2;
    result->m[2] = xz2 - wy2;
    result->m[3] = 0.0f;

    result->m[4] = xy2 - wz2;
    result->m[5] = 1.0f - xx2 - zz2;
    result->m[6] = yz2 + wx2;
    result->m[7] = 0.0f;

    result->m[8] = xz2 + wy2;
    result->m[9] = yz2 - wx2;
    result->m[10] = 1.0f - xx2 - yy2;
    result->m[11] = 0.0f;

    result->m[12] = 0.0f;
    result->m[13] = 0.0f;
    result->m[14] = 0.0f;
    result->m[15] = 1.0f;
}

inline static void XrMatrix4x4f_CreateTranslation(XrMatrix4x4f* result, const float x, const float y, const float z) {
    result->m[0] = 1.0f;
    result->m[1] = 0.0f;
    result->m[2] = 0.0f;
    result->m[3] = 0.0f;
    result->m[4] = 0.0f;
    result->m[5] = 1.0f;
    result->m[6] = 0.0f;
    result->m[7] = 0.0f;
    result->m[8] = 0.0f;
    result->m[9] = 0.0f;
    result->m[10] = 1.0f;
    result->m[11] = 0.0f;
    result->m[12] = x;
    result->m[13] = y;
    result->m[14] = z;
    result->m[15] = 1.0f;
}

inline static void XrMatrix4x4f_Multiply(XrMatrix4x4f* result, const XrMatrix4x4f* a, const XrMatrix4x4f* b) {
    result->m[0] = a->m[0] * b->m[0] + a->m[4] * b->m[1] + a->m[8] * b->m[2] + a->m[12] * b->m[3];
    result->m[1] = a->m[1] * b->m[0] + a->m[5] * b->m[1] + a->m[9] * b->m[2] + a->m[13] * b->m[3];
    result->m[2] = a->m[2] * b->m[0] + a->m[6] * b->m[1] + a->m[10] * b->m[2] + a->m[14] * b->m[3];
    result->m[3] = a->m[3] * b->m[0] + a->m[7] * b->m[1] + a->m[11] * b->m[2] + a->m[15] * b->m[3];

    result->m[4] = a->m[0] * b->m[4] + a->m[4] * b->m[5] + a->m[8] * b->m[6] + a->m[12] * b->m[7];
    result->m[5] = a->m[1] * b->m[4] + a->m[5] * b->m[5] + a->m[9] * b->m[6] + a->m[13] * b->m[7];
    result->m[6] = a->m[2] * b->m[4] + a->m[6] * b->m[5] + a->m[10] * b->m[6] + a->m[14] * b->m[7];
    result->m[7] = a->m[3] * b->m[4] + a->m[7] * b->m[5] + a->m[11] * b->m[6] + a->m[15] * b->m[7];

    result->m[8] = a->m[0] * b->m[8] + a->m[4] * b->m[9] + a->m[8] * b->m[10] + a->m[12] * b->m[11];
    result->m[9] = a->m[1] * b->m[8] + a->m[5] * b->m[9] + a->m[9] * b->m[10] + a->m[13] * b->m[11];
    result->m[10] = a->m[2] * b->m[8] + a->m[6] * b->m[9] + a->m[10] * b->m[10] + a->m[14] * b->m[11];
    result->m[11] = a->m[3] * b->m[8] + a->m[7] * b->m[9] + a->m[11] * b->m[10] + a->m[15] * b->m[11];

    result->m[12] = a->m[0] * b->m[12] + a->m[4] * b->m[13] + a->m[8] * b->m[14] + a->m[12] * b->m[15];
    result->m[13] = a->m[1] * b->m[12] + a->m[5] * b->m[13] + a->m[9] * b->m[14] + a->m[13] * b->m[15];
    result->m[14] = a->m[2] * b->m[12] + a->m[6] * b->m[13] + a->m[10] * b->m[14] + a->m[14] * b->m[15];
    result->m[15] = a->m[3] * b->m[12] + a->m[7] * b->m[13] + a->m[11] * b->m[14] + a->m[15] * b->m[15];
}

inline static void XrMatrix4x4f_CreateTranslationRotationScale(XrMatrix4x4f* result, const XrVector3f* translation,
                                                               const XrQuaternionf* rotation, const XrVector3f* scale) {
    XrMatrix4x4f scaleMatrix;
    XrMatrix4x4f_CreateScale(&scaleMatrix, scale->x, scale->y, scale->z);

    XrMatrix4x4f rotationMatrix;
    XrMatrix4x4f_CreateFromQuaternion(&rotationMatrix, rotation);

    XrMatrix4x4f translationMatrix;
    XrMatrix4x4f_CreateTranslation(&translationMatrix, translation->x, translation->y, translation->z);

    XrMatrix4x4f combinedMatrix;
    XrMatrix4x4f_Multiply(&combinedMatrix, &rotationMatrix, &scaleMatrix);
    XrMatrix4x4f_Multiply(result, &translationMatrix, &combinedMatrix);
}

inline static void XrMatrix4x4f_InvertRigidBody(XrMatrix4x4f* result, const XrMatrix4x4f* src) {
    result->m[0] = src->m[0];
    result->m[1] = src->m[4];
    result->m[2] = src->m[8];
    result->m[3] = 0.0f;
    result->m[4] = src->m[1];
    result->m[5] = src->m[5];
    result->m[6] = src->m[9];
    result->m[7] = 0.0f;
    result->m[8] = src->m[2];
    result->m[9] = src->m[6];
    result->m[10] = src->m[10];
    result->m[11] = 0.0f;
    result->m[12] = -(src->m[0] * src->m[12] + src->m[1] * src->m[13] + src->m[2] * src->m[14]);
    result->m[13] = -(src->m[4] * src->m[12] + src->m[5] * src->m[13] + src->m[6] * src->m[14]);
    result->m[14] = -(src->m[8] * src->m[12] + src->m[9] * src->m[13] + src->m[10] * src->m[14]);
    result->m[15] = 1.0f;
}

static const char*
egl_get_error_string(EGLint error)
{
    switch (error) {
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            abort();
    }
}

static const char* gl_get_framebuffer_status_string(GLenum status) {
    switch (status) {
        case GL_FRAMEBUFFER_UNDEFINED:
            return "GL_FRAMEBUFFER_UNDEFINED";
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
        case GL_FRAMEBUFFER_UNSUPPORTED:
            return "GL_FRAMEBUFFER_UNSUPPORTED";
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
        default:
            abort();
    }
}

void gl_debug_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                                 GLsizei length, const GLchar* message, const void* userParam) {
    info("GL CALLBACK: type = 0x%x, severity = 0x%x, message = %s\n", type, severity, message);
}

#define VIEW_COUNT 2
#define MSAA 4

struct egl {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLConfig config;
};

struct framebuffer {
    XrSwapchain swapchain;
    int swapchain_length;
    int width;
    int height;
    XrSwapchainImageOpenGLESKHR* color_texture_swap_chain;
    GLuint* framebuffers;
};

enum attrib {
    ATTRIB_BEGIN,
    ATTRIB_POSITION = ATTRIB_BEGIN,
    ATTRIB_COLOR,
    ATTRIB_END,
};

enum uniform {
    UNIFORM_BEGIN,
    UNIFORM_MODEL_MATRIX = UNIFORM_BEGIN,
    UNIFORM_VIEW_MATRIX,
    UNIFORM_PROJECTION_MATRIX,
    UNIFORM_END,
};

struct program {
    GLuint program;
    GLint uniform_locations[UNIFORM_END];
};

static const char* ATTRIB_NAMES[ATTRIB_END] = {
        "aPosition", "aColor",
};

static const char* UNIFORM_NAMES[UNIFORM_END] = {
        "uModelMatrix", "uViewMatrix", "uProjectionMatrix",
};

static const char VERTEX_SHADER[] =
        "#version 300 es\n"
        "\n"
        "in vec3 aPosition;\n"
        "in vec3 aColor;\n"
        "uniform mat4 uModelMatrix;\n"
        "uniform mat4 uViewMatrix;\n"
        "uniform mat4 uProjectionMatrix;\n"
        "\n"
        "out vec3 vColor;\n"
        "void main()\n"
        "{\n"
        "	gl_Position = uProjectionMatrix * ( uViewMatrix * ( uModelMatrix * vec4( "
        "aPosition * 0.1, 1.0 ) ) );\n"
        "	vColor = aColor;\n"
        "}\n";

static const char FRAGMENT_SHADER[] = "#version 300 es\n"
                                      "\n"
                                      "in lowp vec3 vColor;\n"
                                      "out lowp vec4 outColor;\n"
                                      "void main()\n"
                                      "{\n"
                                      "	outColor = vec4(vColor, 1.0);\n"
                                      "}\n";

struct attrib_pointer {
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    const GLvoid* pointer;
};

struct vertex {
    float position[4];
    float color[4];
};

struct geometry {
    GLuint vertex_array;
    GLuint vertex_buffer;
    GLuint index_buffer;
};

static const struct attrib_pointer ATTRIB_POINTERS[ATTRIB_END] = {
        { 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                (const GLvoid*)offsetof(struct vertex, position) },
        { 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                (const GLvoid*)offsetof(struct vertex, color) },
};

static const struct vertex VERTICES[] = {
        { { -1.0, +1.0, -1.0 }, { 1.0, 0.0, 1.0 } },
        { { +1.0, +1.0, -1.0 }, { 0.0, 1.0, 0.0 } },
        { { +1.0, +1.0, +1.0 }, { 0.0, 0.0, 1.0 } },
        { { -1.0, +1.0, +1.0 }, { 1.0, 0.0, 0.0 } },
        { { -1.0, -1.0, -1.0 }, { 0.0, 0.0, 1.0 } },
        { { -1.0, -1.0, +1.0 }, { 0.0, 1.0, 0.0 } },
        { { +1.0, -1.0, +1.0 }, { 1.0, 0.0, 1.0 } },
        { { +1.0, -1.0, -1.0 }, { 1.0, 0.0, 0.0 } },
};

static const unsigned short INDICES[] = {
        0, 2, 1, 2, 0, 3,
        4, 6, 5, 6, 4, 7,
        2, 6, 7, 7, 1, 2,
        0, 4, 5, 5, 3, 0,
        3, 5, 6, 6, 2, 3,
        0, 1, 7, 7, 4, 0,
};

static const GLsizei NUM_INDICES = sizeof(INDICES) / sizeof(INDICES[0]);

struct app {
    struct egl egl;
    bool resumed;
    ANativeWindow* window;
    uint64_t frame_index;
    struct framebuffer framebuffers[VIEW_COUNT];
    struct program program;
    struct geometry geometry;
};

XrFormFactor app_config_form = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
XrViewConfigurationType app_config_view = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

const XrPosef xr_pose_identity = { {0,0,0,1}, {0,0,0} };
XrInstance xr_instance = XR_NULL_HANDLE;
XrSystemId xr_system_id = XR_NULL_SYSTEM_ID;
XrSession xr_session = XR_NULL_HANDLE;
bool xr_running = false;
XrSessionState xr_session_state = XR_SESSION_STATE_UNKNOWN;
XrSpace xr_app_space = XR_NULL_HANDLE;
XrEnvironmentBlendMode xr_blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;//standard VR blend mode
XrView views[VIEW_COUNT];
XrViewConfigurationView view_configs[VIEW_COUNT];
XrDebugUtilsMessengerEXT xr_debug;

PFN_xrGetOpenGLESGraphicsRequirementsKHR ext_xrGetOpenGLESGraphicsRequirementsKHR = NULL;
PFN_xrCreateDebugUtilsMessengerEXT ext_xrCreateDebugUtilsMessengerEXT = NULL;

static void egl_create(struct egl* egl) {
    info("get EGL display");
    egl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl->display == EGL_NO_DISPLAY) {
        error("can't get EGL display: %s", egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("initialize EGL display");
    if (eglInitialize(egl->display, NULL, NULL) == EGL_FALSE) {
        error("can't initialize EGL display: %s",
              egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("get number of EGL configs");
    EGLint num_configs = 0;
    if (eglGetConfigs(egl->display, NULL, 0, &num_configs) == EGL_FALSE) {
        error("can't get number of EGL configs: %s",
              egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("allocate EGL configs");
    EGLConfig* configs = malloc(num_configs * sizeof(EGLConfig));
    if (configs == NULL) {
        error("cant allocate EGL configs: %s",
              egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("get EGL configs");
    if (eglGetConfigs(egl->display, configs, num_configs, &num_configs) ==
        EGL_FALSE) {
        error("can't get EGL configs: %s", egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("choose EGL config");
    static const EGLint CONFIG_ATTRIBS[] = {
            EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
            EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
            EGL_SAMPLES,    MSAA, EGL_NONE,
    };
    EGLConfig found_config = NULL;
    for (int i = 0; i < num_configs; ++i) {
        EGLConfig config = configs[i];

        info("get EGL config renderable type");
        EGLint renderable_type = 0;
        if (eglGetConfigAttrib(egl->display, config, EGL_RENDERABLE_TYPE,
                               &renderable_type) == EGL_FALSE) {
            error("can't get EGL config renderable type: %s",
                  egl_get_error_string(eglGetError()));
            exit(EXIT_FAILURE);
        }
        if ((renderable_type & EGL_OPENGL_ES3_BIT_KHR) == 0) {
            continue;
        }

        info("get EGL config surface type");
        EGLint surface_type = 0;
        if (eglGetConfigAttrib(egl->display, config, EGL_SURFACE_TYPE,
                               &surface_type) == EGL_FALSE) {
            error("can't get EGL config surface type: %s",
                  egl_get_error_string(eglGetError()));
            exit(EXIT_FAILURE);
        }
        if ((renderable_type & EGL_PBUFFER_BIT) == 0) {
            continue;
        }
        if ((renderable_type & EGL_WINDOW_BIT) == 0) {
            continue;
        }

        const EGLint* attrib = CONFIG_ATTRIBS;
        while (attrib[0] != EGL_NONE) {
            info("get EGL config attrib");
            EGLint value = 0;
            if (eglGetConfigAttrib(egl->display, config, attrib[0], &value) ==
                EGL_FALSE) {
                error("can't get EGL config attrib: %s",
                      egl_get_error_string(eglGetError()));
                exit(EXIT_FAILURE);
            }
            if (value != attrib[1]) {
                break;
            }
            attrib += 2;
        }
        if (attrib[0] != EGL_NONE) {
            continue;
        }

        found_config = config;
        break;
    }
    if (found_config == NULL) {
        error("can't choose EGL config");
        exit(EXIT_FAILURE);
    }
    egl->config = found_config;

    info("free EGL configs");
    free(configs);

    info("create EGL context");
    static const EGLint CONTEXT_ATTRIBS[] = { EGL_CONTEXT_CLIENT_VERSION, 3,
                                              EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
                                              EGL_NONE };

    egl->context = eglCreateContext(egl->display, egl->config, EGL_NO_CONTEXT, CONTEXT_ATTRIBS);
    if (egl->context == EGL_NO_CONTEXT) {
        error("can't create EGL context: %s",
              egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("create EGL surface");
    static const EGLint SURFACE_ATTRIBS[] = {
            EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE,
    };
    egl->surface = eglCreatePbufferSurface(egl->display, egl->config, SURFACE_ATTRIBS);
    if (egl->surface == EGL_NO_SURFACE) {
        error("can't create EGL pixel buffer surface: %s",
              egl_get_error_string(eglGetError()));
        exit(EXIT_FAILURE);
    }

    info("make EGL context current");
    if (eglMakeCurrent(egl->display, egl->surface, egl->surface,
                       egl->context) == EGL_FALSE) {
        error("can't make EGL context current: %s",
              egl_get_error_string(eglGetError()));
    }

    info("setup opengl debug output");
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
    glDebugMessageCallbackKHR(gl_debug_message, NULL);
}

static void egl_destroy(struct egl* egl) {
    info("make EGL context no longer current");
    eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    info("destroy EGL surface");
    eglDestroySurface(egl->display, egl->surface);

    info("destroy EGL context");
    eglDestroyContext(egl->display, egl->context);

    info("terminate EGL display");
    eglTerminate(egl->display);
}

static void geometry_create(struct geometry* geometry) {
    glGenVertexArrays(1, &geometry->vertex_array);
    glBindVertexArray(geometry->vertex_array);
    glGenBuffers(1, &geometry->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, geometry->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VERTICES), VERTICES, GL_STATIC_DRAW);
    for (enum attrib attrib = ATTRIB_BEGIN; attrib != ATTRIB_END; ++attrib) {
        struct attrib_pointer attrib_pointer = ATTRIB_POINTERS[attrib];
        glEnableVertexAttribArray(attrib);
        glVertexAttribPointer(attrib, attrib_pointer.size, attrib_pointer.type,
                              attrib_pointer.normalized, attrib_pointer.stride,
                              attrib_pointer.pointer);
    }
    glGenBuffers(1, &geometry->index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(INDICES), INDICES,
                 GL_STATIC_DRAW);
    glBindVertexArray(0);
}

static void geometry_destroy(struct geometry* geometry) {
    glDeleteBuffers(1, &geometry->index_buffer);
    glDeleteBuffers(1, &geometry->vertex_buffer);
    glDeleteVertexArrays(1, &geometry->vertex_array);
}

static GLuint compile_shader(GLenum type, const char* string) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &string, NULL);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        char* log = malloc(length);
        glGetShaderInfoLog(shader, length, NULL, log);
        error("can't compile shader: %s", log);
        exit(EXIT_FAILURE);
    }
    return shader;
}

static void program_create(struct program* program) {
    program->program = glCreateProgram();
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
    glAttachShader(program->program, vertex_shader);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    glAttachShader(program->program, fragment_shader);
    for (enum attrib attrib = ATTRIB_BEGIN; attrib != ATTRIB_END; ++attrib) {
        glBindAttribLocation(program->program, attrib, ATTRIB_NAMES[attrib]);
    }
    glLinkProgram(program->program);
    GLint status = 0;
    glGetProgramiv(program->program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program->program, GL_INFO_LOG_LENGTH, &length);
        char* log = malloc(length);
        glGetProgramInfoLog(program->program, length, NULL, log);
        error("can't link program: %s", log);
        exit(EXIT_FAILURE);
    }
    for (enum uniform uniform = UNIFORM_BEGIN; uniform != UNIFORM_END; ++uniform) {
        program->uniform_locations[uniform] =
                glGetUniformLocation(program->program, UNIFORM_NAMES[uniform]);
    }
}

static void program_destroy(struct program* program) {
    glDeleteProgram(program->program);
}

static void app_on_cmd(struct android_app* android_app, int32_t cmd) {
    struct app* app = (struct app*)android_app->userData;
    switch (cmd) {
        case APP_CMD_START:
            info("onStart()");
            break;
        case APP_CMD_RESUME:
            info("onResume()");
            app->resumed = true;
            break;
        case APP_CMD_PAUSE:
            info("onPause()");
            app->resumed = false;
            break;
        case APP_CMD_STOP:
            info("onStop()");
            break;
        case APP_CMD_DESTROY:
            info("onDestroy()");
            app->window = NULL;
            break;
        case APP_CMD_INIT_WINDOW:
            info("surfaceCreated()");
            app->window = android_app->window;
            break;
        case APP_CMD_TERM_WINDOW:
            info("surfaceDestroyed()");
            app->window = NULL;
            break;
        default:
            break;
    }
}

void openxr_poll_events() {
    XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };

    while (xrPollEvent(xr_instance, &event_buffer) == XR_SUCCESS) {
        if (event_buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            info("XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED");
            XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged*)&event_buffer;
            xr_session_state = changed->state;

            switch (xr_session_state) {
                case XR_SESSION_STATE_READY:
                    info("XR_SESSION_STATE_READY");
                    XrSessionBeginInfo begin_info = {XR_TYPE_SESSION_BEGIN_INFO};
                    begin_info.primaryViewConfigurationType = app_config_view;
                    xrBeginSession(xr_session, &begin_info);
                    xr_running = true;
                    break;
                case XR_SESSION_STATE_STOPPING:
                    info("XR_SESSION_STATE_STOPPING");
                    xr_running = false;
                    xrEndSession(xr_session);
                    break;
                case XR_SESSION_STATE_EXITING:
                    info("XR_SESSION_STATE_EXITING");
                    //exit = true;
                    break;
                case XR_SESSION_STATE_LOSS_PENDING:
                    info("XR_SESSION_STATE_LOSS_PENDING");
                    //exit = true;
                    break;
                default:
                    info("DEFAULT");
                    break;
            }
        } else if (event_buffer.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            info("XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING");
        }

        event_buffer = (XrEventDataBuffer) { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

PFN_xrDebugUtilsMessengerCallbackEXT openxr_debug_message(XrDebugUtilsMessageSeverityFlagsEXT severity,
                                                          XrDebugUtilsMessageTypeFlagsEXT types,
                                                          const XrDebugUtilsMessengerCallbackDataEXT* msg,
                                                          void* userData) {
    info("%s: %s\n", msg->functionName, msg->message);
    return XR_FALSE;
}

void openxr_init(struct android_app *android_app, struct app *app) {
    PFN_xrInitializeLoaderKHR initializeLoader = NULL;
    XRCMD(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)(&initializeLoader)));

    XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid;
    memset(&loaderInitInfoAndroid, 0, sizeof(loaderInitInfoAndroid));
    loaderInitInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loaderInitInfoAndroid.next = NULL;
    loaderInitInfoAndroid.applicationVM = android_app->activity->vm;
    loaderInitInfoAndroid.applicationContext = android_app->activity->clazz;
    initializeLoader((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfoAndroid);

    uint32_t apilayer_count = 0;
    xrEnumerateApiLayerProperties(0, &apilayer_count, NULL);
    info("api layers %u", apilayer_count);

    XrApiLayerProperties apilayer_properties[apilayer_count];
    xrEnumerateApiLayerProperties(apilayer_count, &apilayer_count, &apilayer_properties[0]);
    for (int i = 0; i < apilayer_count; i++) {
        XrApiLayerProperties *apilayer_prop = &apilayer_properties[i];
        info("Api layer: %s", apilayer_prop->layerName);
    }

    uint32_t ext_count = 0;
    xrEnumerateInstanceExtensionProperties(NULL, 0, &ext_count, NULL);
    XrExtensionProperties exts[ext_count];
    char *ext_names[ext_count];
    for (int i = 0; i < ext_count; i++) {
        XrExtensionProperties* ext_prop = &exts[i];
        ext_prop->type = XR_TYPE_EXTENSION_PROPERTIES;
        ext_prop->next = NULL;
    }
    xrEnumerateInstanceExtensionProperties(NULL, ext_count, &ext_count, &exts[0]);

    info("extensions %u", ext_count);

    info("extensions:\n");
    for (size_t i = 0; i < ext_count; i++) {
        info("- %s\n", exts[i].extensionName);
        ext_names[i] = exts[i].extensionName;
    }

    XrInstanceCreateInfoAndroidKHR createInfoAndroid = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    createInfoAndroid.applicationVM = android_app->activity->vm;
    createInfoAndroid.applicationActivity = android_app->activity->clazz;

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.next = (XrBaseInStructure*) &createInfoAndroid;
    createInfo.enabledExtensionCount = ext_count;
    createInfo.enabledExtensionNames = &ext_names[0];
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strcpy(createInfo.applicationInfo.applicationName, "hello_quest_openxr");
    XRCMD(xrCreateInstance(&createInfo, &xr_instance));

    XRCMD(xrGetInstanceProcAddr(xr_instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)(&ext_xrGetOpenGLESGraphicsRequirementsKHR)));
    XRCMD(xrGetInstanceProcAddr(xr_instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrCreateDebugUtilsMessengerEXT)));

    XrDebugUtilsMessengerCreateInfoEXT debug_info = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    debug_info.messageTypes =
            XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
            XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
            XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    debug_info.messageSeverities =
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_info.userCallback = (PFN_xrDebugUtilsMessengerCallbackEXT) &openxr_debug_message;

    if (ext_xrCreateDebugUtilsMessengerEXT) {
        info("openxr debug messenger ON");
        ext_xrCreateDebugUtilsMessengerEXT(xr_instance, &debug_info, &xr_debug);
    } else {
        info("openxr debug messenger OFF");
    }

    XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = app_config_form;
    xrGetSystem(xr_instance, &systemInfo, &xr_system_id);

    XrGraphicsRequirementsOpenGLESKHR requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    ext_xrGetOpenGLESGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirement);
    info("gl es %hu.%hu.%u",
         XR_VERSION_MAJOR(requirement.maxApiVersionSupported),
         XR_VERSION_MINOR(requirement.maxApiVersionSupported),
         XR_VERSION_PATCH(requirement.maxApiVersionSupported));

    XrGraphicsBindingOpenGLESAndroidKHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    binding.display = app->egl.display;
    binding.context = app->egl.context;
    binding.config = app->egl.config;

    XrSessionCreateInfo session_info = { XR_TYPE_SESSION_CREATE_INFO };
    session_info.next = &binding;
    session_info.systemId = xr_system_id;
    XRCMD(xrCreateSession(xr_instance, &session_info, &xr_session));

    XrReferenceSpaceCreateInfo ref_space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    ref_space.poseInReferenceSpace = xr_pose_identity;
    ref_space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XRCMD(xrCreateReferenceSpace(xr_session, &ref_space, &xr_app_space));

    uint32_t view_count = 0;
    XRCMD(xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, app_config_view, VIEW_COUNT, &view_count, &view_configs[0]));

    uint32_t swapchain_format_count;
    XRCMD(xrEnumerateSwapchainFormats(xr_session, 0, &swapchain_format_count, NULL));
    int64_t swapchain_formats[swapchain_format_count];
    XRCMD(xrEnumerateSwapchainFormats(xr_session, swapchain_format_count, &swapchain_format_count,
                                      &swapchain_formats[0]));
    info("num swapchain formats %i", swapchain_format_count);

    // openxr swapchain formats are in order of priority but we will be explicit=
    GLint swapchain_format = GL_RGBA8;

    for (int i = 0; i < VIEW_COUNT; i++) {
        XrViewConfigurationView view = view_configs[i];
        info("make view %i (%u %u)", i, view.recommendedImageRectWidth, view.recommendedImageRectHeight);

        XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        XrSwapchain swapchain;
        swapchain_info.arraySize = 1;
        swapchain_info.mipCount = 1;
        swapchain_info.faceCount = 1;
        swapchain_info.format = swapchain_format;
        swapchain_info.width = view.recommendedImageRectWidth;
        swapchain_info.height = view.recommendedImageRectHeight;
        swapchain_info.sampleCount = view.recommendedSwapchainSampleCount;
        swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        XRCMD(xrCreateSwapchain(xr_session, &swapchain_info, &swapchain));
        info("create swapchain (sample count %i)", view.recommendedSwapchainSampleCount);

        uint32_t swapchain_length = 0;
        XRCMD(xrEnumerateSwapchainImages(swapchain, 0, &swapchain_length, NULL));
        info("swapchain length %i", swapchain_length);

        struct framebuffer framebuffer = {};
        framebuffer.swapchain = swapchain;
        framebuffer.swapchain_length = swapchain_length;
        framebuffer.width = swapchain_info.width;
        framebuffer.height = swapchain_info.height;

        framebuffer.color_texture_swap_chain = malloc(sizeof(XrSwapchainImageOpenGLESKHR) * swapchain_length);
        for (int j = 0; j < swapchain_length; j++) {
            framebuffer.color_texture_swap_chain[j] = (XrSwapchainImageOpenGLESKHR) { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR };
        }
        XRCMD(xrEnumerateSwapchainImages(swapchain, swapchain_length, &swapchain_length, (XrSwapchainImageBaseHeader*)framebuffer.color_texture_swap_chain));

        framebuffer.framebuffers = malloc(framebuffer.swapchain_length * sizeof(GLuint));
        GL(glGenFramebuffers(framebuffer.swapchain_length, framebuffer.framebuffers));
        for (int i = 0; i < framebuffer.swapchain_length; ++i) {
            GLuint color_texture = framebuffer.color_texture_swap_chain[i].image;
            info("color texture %d (%d)", i, color_texture);
            GL(glBindTexture(GL_TEXTURE_2D, color_texture));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL(glBindTexture(GL_TEXTURE_2D, 0));

            info("create depth texture %d", i);
            GLuint depth_texture;
            GL(glGenTextures(1, &depth_texture));
            GL(glBindTexture(GL_TEXTURE_2D, depth_texture));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, framebuffer.width, framebuffer.height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0));

            info("create framebuffer %d", i);
            GL(glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.framebuffers[i]));
            GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0));
            GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture, 0));
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                error("can't create framebuffer %d, %s", i, gl_get_framebuffer_status_string(status));
                exit(EXIT_FAILURE);
            }
            GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        }

        app->framebuffers[i] = framebuffer;
    }
}

static void framebuffer_destroy(struct framebuffer* framebuffer) {
    info("destroy framebuffers");
    glDeleteFramebuffers(framebuffer->swapchain_length,
                         framebuffer->framebuffers);

    info("free framebuffers");
    free(framebuffer->framebuffers);
}

void gl_render(struct app *app, struct framebuffer *framebuffer, XrCompositionLayerProjectionView layer_view,
               uint32_t swapchain_image_index) {
    XrPosef pose = layer_view.pose;
    XrMatrix4x4f proj;
    XrMatrix4x4f_CreateProjectionFov(&proj, layer_view.fov, 0.05f, 100.0f);
    XrMatrix4x4f toView;
    XrVector3f scale = {1.f, 1.f, 1.f};
    XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scale);

    XrMatrix4x4f view;
    XrMatrix4x4f_InvertRigidBody(&view, &toView);
    XrMatrix4x4f vp;
    XrMatrix4x4f_Multiply(&vp, &proj, &view);

    XrMatrix4x4f model;
    XrMatrix4x4f_CreateTranslation(&model, 0.f, 0.f, -1.f);

    GL(glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->framebuffers[swapchain_image_index]));

    GL(glEnable(GL_CULL_FACE));
    GL(glEnable(GL_DEPTH_TEST));
    GL(glEnable(GL_SCISSOR_TEST));
    GL(glViewport(layer_view.subImage.imageRect.offset.x,
               layer_view.subImage.imageRect.offset.y,
               layer_view.subImage.imageRect.extent.width,
               layer_view.subImage.imageRect.extent.height));

    GL(glScissor(0, 0, framebuffer->width, framebuffer->height));
    GL(glClearColor(1.0, 1.0, 1.0, 1.0));

    GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
    GL(glUseProgram(app->program.program));
    GL(glUniformMatrix4fv(
            app->program.uniform_locations[UNIFORM_MODEL_MATRIX], 1,
            GL_FALSE, (const GLfloat*)&model));
    GL(glUniformMatrix4fv(
            app->program.uniform_locations[UNIFORM_VIEW_MATRIX], 1,
            GL_FALSE, (const GLfloat*)&view));
    GL(glUniformMatrix4fv(
            app->program.uniform_locations[UNIFORM_PROJECTION_MATRIX], 1,
            GL_FALSE, (const GLfloat*)&proj));
    GL(glBindVertexArray(app->geometry.vertex_array));
    GL(glDrawElements(GL_TRIANGLES, NUM_INDICES, GL_UNSIGNED_SHORT, NULL));
    GL(glBindVertexArray(0));
    GL(glUseProgram(0));

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glScissor(0, 0, 1, framebuffer->height);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(framebuffer->width - 1, 0, 1, framebuffer->height);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0, 0, framebuffer->width, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0, framebuffer->height - 1, framebuffer->width, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    static const GLenum ATTACHMENTS[] = { GL_DEPTH_ATTACHMENT };
    static const GLsizei NUM_ATTACHMENTS =
            sizeof(ATTACHMENTS) / sizeof(ATTACHMENTS[0]);
    GL(glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, NUM_ATTACHMENTS, ATTACHMENTS));
    GL(glFlush());
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void openxr_render_frame(struct app *app) {
    XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
    XRCMD(xrWaitFrame(xr_session, NULL, &frame_state));

    XRCMD(xrBeginFrame(xr_session, NULL));

    XrCompositionLayerBaseHeader *layers[1];

    int num_rendered_layers = 0;
    XrCompositionLayerProjectionView proj_views[VIEW_COUNT];

    if (frame_state.shouldRender) {
        num_rendered_layers++;

        XrViewLocateInfo view_locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
        view_locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        view_locate_info.displayTime = frame_state.predictedDisplayTime;
        view_locate_info.space = xr_app_space;

        XrViewState view_state = { XR_TYPE_VIEW_STATE };
        XrView views[2] = { {XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t viewCountOutput;
        XRCMD(xrLocateViews(xr_session, &view_locate_info, &view_state, VIEW_COUNT, &viewCountOutput, &views[0]));

        for (int i = 0; i < VIEW_COUNT; i++) {
            struct framebuffer *framebuffer = &app->framebuffers[i];

            uint32_t swapchain_image_index;
            XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            XRCMD(xrAcquireSwapchainImage(framebuffer->swapchain, &acquire_info, &swapchain_image_index));

            XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            wait_info.timeout = XR_INFINITE_DURATION;
            XRCMD(xrWaitSwapchainImage(framebuffer->swapchain, &wait_info));

            proj_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            proj_views[i].pose = views[i].pose;
            proj_views[i].fov = views[i].fov;
            proj_views[i].subImage.swapchain = framebuffer->swapchain;
            proj_views[i].subImage.imageRect.offset = (XrOffset2Di) { 0, 0 };
            proj_views[i].subImage.imageRect.extent = (XrExtent2Di) { framebuffer->width, framebuffer->height };

            gl_render(app, framebuffer, proj_views[i], swapchain_image_index);

            XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            XRCMD(xrReleaseSwapchainImage(framebuffer->swapchain, &release_info));
        }

        XrCompositionLayerProjection layer_proj;
        layer_proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer_proj.space = xr_app_space;
        layer_proj.viewCount = VIEW_COUNT;
        layer_proj.views = &proj_views[0];
        layers[0] = (XrCompositionLayerBaseHeader*) &layer_proj;
    }

    XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
    end_info.displayTime = frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = xr_blend;
    end_info.layerCount = num_rendered_layers;
    end_info.layers = (const XrCompositionLayerBaseHeader *const *)&layers[0];
    XRCMD(xrEndFrame(xr_session, &end_info));
}

static void app_create(struct android_app* android_app, struct app* app) {
    egl_create(&app->egl);
    openxr_init(android_app, app);
    program_create(&app->program);
    geometry_create(&app->geometry);
    app->resumed = false;
}

static void app_destroy(struct app* app) {
    geometry_destroy(&app->geometry);
    program_destroy(&app->program);
    for (int i = 0; i < VIEW_COUNT; ++i) {
        framebuffer_destroy(&app->framebuffers[i]);
    }
    egl_destroy(&app->egl);
}

void android_main(struct android_app* android_app) {
    ANativeActivity_setWindowFlags(android_app->activity,
                                   AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    info("hello");

    struct app app;
    app_create(android_app, &app);

    info("running...");
    android_app->userData = &app;
    android_app->onAppCmd = app_on_cmd;
    while (!android_app->destroyRequested) {
        for (;;) {
            int events = 0;
            struct android_poll_source* source = NULL;
            if (ALooper_pollAll(
                    android_app->destroyRequested,
                    NULL, &events, (void**)&source) < 0) {
                break;
            }
            if (source != NULL) {
                source->process(android_app, source);
            }
        }

        openxr_poll_events();

        if (xr_running) {
            openxr_render_frame(&app);
        }
    }

    app_destroy(&app);
}
