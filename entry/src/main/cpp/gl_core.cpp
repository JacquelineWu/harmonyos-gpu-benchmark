#include "gl_core.h"

#include <hilog/log.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// 注意：hilog/log.h 里已把 LOG_TAG 定义成宏（NULL），这里必须换名，否则 -Wmacro-redefined
#define BENCH_TAG "Bench3D"
#define LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, BENCH_TAG, __VA_ARGS__)
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xFF00, BENCH_TAG, __VA_ARGS__)

namespace {

constexpr float PI = 3.14159265358979323846f;

// ---------- 极简 4x4 矩阵（列主序，与 OpenGL 一致） ----------
void MatMul(float *out, const float *a, const float *b)
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + r] * b[c * 4 + k];
            }
            out[c * 4 + r] = s;
        }
    }
}

void MatIdentity(float *m)
{
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void MatPerspective(float *m, float fovDeg, float aspect, float near, float far)
{
    std::memset(m, 0, sizeof(float) * 16);
    float f = 1.0f / std::tan(fovDeg * PI / 360.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

// 右手系 lookAt（列主序）：f = 视线方向，s = right，u = 真实上向量
void MatLookAt(float *m, float ex, float ey, float ez, float cx, float cy, float cz)
{
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = 1.0f / std::sqrt(fx * fx + fy * fy + fz * fz);
    fx *= fl; fy *= fl; fz *= fl;

    // s = cross(f, up)，up = (0,1,0)
    float sx = fy * 0.0f - fz * 1.0f;
    float sy = fz * 0.0f - fx * 0.0f;
    float sz = fx * 1.0f - fy * 0.0f;
    float sl = 1.0f / std::sqrt(sx * sx + sy * sy + sz * sz);
    sx *= sl; sy *= sl; sz *= sl;

    // u = cross(s, f)
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    m[0] = sx;      m[4] = sy;      m[8] = sz;      m[12] = -(sx * ex + sy * ey + sz * ez);
    m[1] = ux;      m[5] = uy;      m[9] = uz;      m[13] = -(ux * ex + uy * ey + uz * ez);
    m[2] = -fx;     m[6] = -fy;     m[10] = -fz;    m[14] = (fx * ex + fy * ey + fz * ez);
    m[3] = 0.0f;    m[7] = 0.0f;    m[11] = 0.0f;   m[15] = 1.0f;
}

void MatRotY(float *m, float rad)
{
    MatIdentity(m);
    float c = std::cos(rad), s = std::sin(rad);
    m[0] = c;  m[2] = -s;
    m[8] = s;  m[10] = c;
}

void MatRotX(float *m, float rad)
{
    MatIdentity(m);
    float c = std::cos(rad), s = std::sin(rad);
    m[5] = c;  m[6] = s;
    m[9] = -s; m[10] = c;
}

// ---------- 着色器 ----------
// 毛发甜甜圈：一次 instanced draw 画完「实体 + N 层毛发壳」，
// 层号 t = gl_InstanceID / uLayers，沿法线向外挤出并随机剔除成毛丝。
const char *VERT_SRC = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uFurLength;
uniform float uLayers;
uniform float uTime;

out vec3 vNormal;
out vec3 vWorld;
out vec2 vUV;
out float vLayer;

void main() {
    float t = float(gl_InstanceID) / max(uLayers, 1.0);
    vec3 n = normalize(aNormal);
    // 毛尖随风摆动：越靠外层摆幅越大（t 的平方衰减到根部为零）
    float sway = sin(uTime * 1.15 + aUV.x * 24.0 + aUV.y * 15.0) * 0.30 * t * t;
    vec3 dir = normalize(n + vec3(sway * 0.7, sway * 0.25, sway * 0.55));
    vec3 p = aPos + dir * (t * uFurLength);

    vWorld = (uModel * vec4(p, 1.0)).xyz;
    vNormal = mat3(uModel) * n;
    vUV = aUV;
    vLayer = t;
    gl_Position = uMVP * vec4(p, 1.0);
}
)GLSL";

// 片元着色器里的 uIter 循环是「计算负载旋钮」：
// 每层每像素都要跑 uIter 次 value-noise，层与层之间还会互相 overdraw，
// 于是 furLayers × shaderIterations 近似线性地决定 GPU 的 ALU 压力。
const char *FRAG_SRC = R"GLSL(#version 300 es
precision highp float;

in vec3 vNormal;
in vec3 vWorld;
in vec2 vUV;
in float vLayer;

uniform vec3 uCamPos;
uniform float uTime;
uniform int uIter;
uniform float uDensity;

out vec4 fragColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main() {
    // 毛发造型：按 uv 网格取随机数，越外层保留的毛丝越少
    if (vLayer > 0.001) {
        vec2 cell = floor(vUV * uDensity);
        float rnd = hash21(cell + 7.13);
        float keep = 1.0 - vLayer;
        if (rnd > keep) {
            discard;
        }
    }

    // ---- 计算负载：FBM 噪声迭代 ----
    float acc = 0.0;
    vec2 q = vUV * 12.0 + vLayer * 3.0 + uTime * 0.05;
    int iters = uIter;
    for (int i = 0; i < iters; ++i) {
        acc += vnoise(q + float(i) * 0.618);
        q = q * 1.07 + vec2(0.13, -0.07);
    }
    acc /= float(iters > 0 ? iters : 1);

    // ---- 光照 ----
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorld);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    vec3 L1 = normalize(vec3(0.55, 0.78, 0.42));
    vec3 L2 = normalize(vec3(-0.62, 0.18, -0.55));
    float d1 = max(dot(N, L1), 0.0);
    float d2 = max(dot(N, L2), 0.0);
    vec3 H = normalize(L1 + V);
    float spec = pow(max(dot(N, H), 0.0), 44.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.6);

    vec3 root = vec3(0.40, 0.15, 0.05);
    vec3 tip = vec3(1.00, 0.74, 0.38);
    vec3 base = mix(root, tip, pow(vLayer, 0.7));
    base *= (0.70 + 0.60 * acc);

    vec3 col = base * (0.20 + 0.88 * d1 + 0.30 * d2);
    col += vec3(1.0, 0.94, 0.84) * spec * (1.0 - vLayer) * 0.55;
    col += vec3(0.22, 0.34, 0.62) * rim * 0.35;

    float alpha = 1.0 - 0.30 * vLayer;
    fragColor = vec4(col, alpha);
}
)GLSL";

GLuint CompileShader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024] = {0};
        glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
        LOGE("shader compile failed: %{public}s", buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

}  // namespace

BenchRenderer &BenchRenderer::Instance()
{
    static BenchRenderer instance;
    return instance;
}

// ---------------- EGL ----------------

bool BenchRenderer::InitEGL()
{
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display_, &major, &minor)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                              EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                              EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLint numConfig = 0;
    if (!eglChooseConfig(display_, attribs, &config_, 1, &numConfig) || numConfig < 1) {
        const EGLint fallback[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 16,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
        if (!eglChooseConfig(display_, fallback, &config_, 1, &numConfig) || numConfig < 1) {
            LOGE("eglChooseConfig failed");
            return false;
        }
    }

    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctxAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return false;
    }

    // EGLNativeWindowType 在 OHOS 上是整型，指针只能 reinterpret_cast
    surface_ = eglCreateWindowSurface(display_, config_,
                                      reinterpret_cast<EGLNativeWindowType>(window_), nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: %{public}d", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }
    // 关掉 vsync：让 GPU 跑满，这才是烤机
    eglSwapInterval(display_, 0);

    EGLint w = 0, h = 0;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) {
        width_ = w;
        height_ = h;
    }
    LOGI("EGL ready %{public}d x %{public}d, ES %{public}d.%{public}d", width_, height_, major, minor);
    return true;
}

void BenchRenderer::DestroyEGL()
{
    if (display_ == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
    if (context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
        context_ = EGL_NO_CONTEXT;
    }
    eglTerminate(display_);
    display_ = EGL_NO_DISPLAY;
}

// ---------------- 几何 ----------------

void BenchRenderer::BuildTorusMesh(int segments)
{
    int majorSeg = segments;
    int minorSeg = segments / 2;
    if (majorSeg < 16) {
        majorSeg = 16;
    }
    if (minorSeg < 8) {
        minorSeg = 8;
    }

    constexpr float R = 1.0f;   // 主环半径
    constexpr float r = 0.42f;  // 管半径

    std::vector<float> verts;
    std::vector<uint32_t> idx;
    verts.reserve(static_cast<size_t>((majorSeg + 1) * (minorSeg + 1) * 8));
    idx.reserve(static_cast<size_t>(majorSeg * minorSeg * 6));

    for (int i = 0; i <= majorSeg; ++i) {
        float u = static_cast<float>(i) / majorSeg * 2.0f * PI;
        float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= minorSeg; ++j) {
            float v = static_cast<float>(j) / minorSeg * 2.0f * PI;
            float cv = std::cos(v), sv = std::sin(v);
            float nx = cv * cu, ny = sv, nz = cv * su;
            verts.push_back((R + r * cv) * cu);
            verts.push_back(r * sv);
            verts.push_back((R + r * cv) * su);
            verts.push_back(nx);
            verts.push_back(ny);
            verts.push_back(nz);
            verts.push_back(static_cast<float>(i) / majorSeg);
            verts.push_back(static_cast<float>(j) / minorSeg);
        }
    }
    for (int i = 0; i < majorSeg; ++i) {
        for (int j = 0; j < minorSeg; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (minorSeg + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = a + static_cast<uint32_t>(minorSeg + 1);
            uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(6 * sizeof(float)));
    glBindVertexArray(0);

    indexCount_ = static_cast<GLsizei>(idx.size());
    triangles_ = static_cast<int>(idx.size() / 3);
    meshSegments_ = segments;
    LOGI("mesh rebuilt: segments=%{public}d tris=%{public}d", segments, triangles_);
}

// ---------------- GL 资源 ----------------

bool BenchRenderer::InitGLResources()
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, VERT_SRC);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
    if (vs == 0 || fs == 0) {
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char buf[1024] = {0};
        glGetProgramInfoLog(program_, sizeof(buf), nullptr, buf);
        LOGE("link failed: %{public}s", buf);
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    BuildTorusMesh(cfg_.segments);
    RebuildInstanceCount(cfg_.furLayers);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);  // 毛发要双面可见，且不依赖索引绕序
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.031f, 0.043f, 0.063f, 1.0f);
    return true;
}

void BenchRenderer::DestroyGLResources()
{
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void BenchRenderer::RebuildInstanceCount(int layers)
{
    if (layers < 1) {
        layers = 1;
    }
    // 实例 0 是实体甜甜圈，1..layers 是毛发壳
    instanceCount_ = layers + 1;
}

// ---------------- 渲染 ----------------

void BenchRenderer::RenderFrame(double t)
{
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (height_ > 0) ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;

    float proj[16], view[16], rotY[16], rotX[16], model[16], mv[16], mvp[16];
    MatPerspective(proj, 42.0f, aspect, 0.1f, 60.0f);
    MatLookAt(view, 0.0f, 0.85f, 4.15f, 0.0f, 0.0f, 0.0f);
    MatRotY(rotY, static_cast<float>(t) * 0.55f);
    MatRotX(rotX, 0.40f + 0.18f * std::sin(static_cast<float>(t) * 0.31f));
    MatMul(model, rotY, rotX);
    MatMul(mv, view, model);
    MatMul(mvp, proj, mv);

    glUseProgram(program_);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uMVP"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uModel"), 1, GL_FALSE, model);
    glUniform3f(glGetUniformLocation(program_, "uCamPos"), 0.0f, 0.85f, 4.15f);
    glUniform1f(glGetUniformLocation(program_, "uFurLength"), cfg_.furLength);
    glUniform1f(glGetUniformLocation(program_, "uLayers"), static_cast<float>(cfg_.furLayers));
    glUniform1f(glGetUniformLocation(program_, "uTime"), static_cast<float>(t));
    glUniform1i(glGetUniformLocation(program_, "uIter"), cfg_.shaderIterations);
    glUniform1f(glGetUniformLocation(program_, "uDensity"), cfg_.furDensity);

    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, instanceCount_);
    glBindVertexArray(0);
}

void BenchRenderer::RenderLoop()
{
    {
        std::lock_guard<std::mutex> lk(windowMutex_);
        if (!InitEGL()) {
            running_ = false;
            return;
        }
    }
    if (!InitGLResources()) {
        DestroyEGL();
        running_ = false;
        return;
    }
    surfaceReady_ = true;

    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastFrame = t0;
    std::chrono::steady_clock::time_point windowStart = t0;
    long long framesInWindow = 0;
    double sumFps = 0.0;
    long long samples = 0;
    double minFps = 1e18;
    double maxFps = 0.0;

    while (running_.load()) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();

        // 参数变更（只能在 GL 线程里重建网格）
        if (meshDirty_.exchange(false)) {
            cfg_ = pendingCfg_;
            BuildTorusMesh(cfg_.segments);
            RebuildInstanceCount(cfg_.furLayers);
            meshDirty_ = false;
        }
        if (configDirty_.exchange(false)) {
            cfg_ = pendingCfg_;
            RebuildInstanceCount(cfg_.furLayers);
        }

        if (enabled_.load()) {
            RenderFrame(elapsed);
            eglSwapBuffers(display_, surface_);
            framesInWindow++;
            liveStats_.frames++;

            double windowMs =
                std::chrono::duration<double, std::milli>(now - windowStart).count();
            if (windowMs >= 400.0) {
                double fps = static_cast<double>(framesInWindow) * 1000.0 / windowMs;
                std::lock_guard<std::mutex> lk(statsMutex_);
                liveStats_.fps = fps;
                liveStats_.frameMs = windowMs / static_cast<double>(framesInWindow);
                // 前 1.5 秒是着色器编译/缓存预热，不计入极值
                if (elapsed > 1.5) {
                    if (fps < minFps) minFps = fps;
                    if (fps > maxFps) maxFps = fps;
                    sumFps += fps;
                    samples++;
                    liveStats_.avgFps = sumFps / static_cast<double>(samples);
                    liveStats_.minFps = (minFps > 1e17) ? fps : minFps;
                    liveStats_.maxFps = maxFps;
                }
                liveStats_.elapsedSec = elapsed;
                double complexity = (cfg_.furLayers / 16.0) * (cfg_.shaderIterations / 8.0) * (cfg_.segments / 128.0);
                if (complexity < 0.05) complexity = 0.05;
                liveStats_.score = liveStats_.avgFps * complexity;
                liveStats_.width = width_;
                liveStats_.height = height_;
                liveStats_.triangles = triangles_ * instanceCount_;
                liveStats_.furLayers = cfg_.furLayers;
                liveStats_.iterations = cfg_.shaderIterations;
                liveStats_.running = true;
                liveStats_.ready = true;
                framesInWindow = 0;
                windowStart = now;
            }
        } else {
            // 待命：低频清屏，保持画面但不烤机
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            eglSwapBuffers(display_, surface_);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            framesInWindow = 0;
            windowStart = now;
            std::lock_guard<std::mutex> lk(statsMutex_);
            liveStats_.running = false;
            liveStats_.fps = 0.0;
            liveStats_.frameMs = 0.0;
            liveStats_.ready = true;
            liveStats_.width = width_;
            liveStats_.height = height_;
        }
        lastFrame = now;
    }

    DestroyGLResources();
    DestroyEGL();
    surfaceReady_ = false;
    (void)lastFrame;
}

// ---------------- 生命周期与控制 ----------------

void BenchRenderer::OnSurfaceCreated(void *window)
{
    std::lock_guard<std::mutex> lk(windowMutex_);
    window_ = window;
    if (running_.load()) {
        return;
    }
    running_ = true;
    liveStats_ = BenchStats{};
    thread_ = std::thread(&BenchRenderer::RenderLoop, this);
    LOGI("surface created, render thread started");
}

void BenchRenderer::OnSurfaceChanged(void *window)
{
    std::lock_guard<std::mutex> lk(windowMutex_);
    window_ = window;
    if (surface_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY) {
        EGLint w = 0, h = 0;
        eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
        if (w > 0 && h > 0) {
            width_ = w;
            height_ = h;
        }
    }
}

void BenchRenderer::OnSurfaceDestroyed()
{
    Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    window_ = nullptr;
    LOGI("surface destroyed");
}

void BenchRenderer::Start()
{
    if (!running_.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(statsMutex_);
        liveStats_ = BenchStats{};
        liveStats_.ready = surfaceReady_.load();
    }
    enabled_ = true;
    LOGI("bench start: layers=%{public}d iter=%{public}d seg=%{public}d", cfg_.furLayers,
         cfg_.shaderIterations, cfg_.segments);
}

void BenchRenderer::Stop()
{
    enabled_ = false;
    LOGI("bench stop");
}

void BenchRenderer::SetConfig(int furLayers, int shaderIterations, int segments)
{
    if (furLayers < 1) furLayers = 1;
    if (furLayers > 256) furLayers = 256;
    if (shaderIterations < 1) shaderIterations = 1;
    if (shaderIterations > 96) shaderIterations = 96;
    if (segments < 24) segments = 24;
    if (segments > 512) segments = 512;

    bool needMesh = (segments != cfg_.segments);
    pendingCfg_.furLayers = furLayers;
    pendingCfg_.shaderIterations = shaderIterations;
    pendingCfg_.segments = segments;
    pendingCfg_.furLength = cfg_.furLength;
    pendingCfg_.furDensity = cfg_.furDensity;
    if (!running_.load()) {
        cfg_ = pendingCfg_;
        RebuildInstanceCount(cfg_.furLayers);
        return;
    }
    if (needMesh) {
        meshDirty_ = true;
    } else {
        configDirty_ = true;
    }
}

bool BenchRenderer::IsRunning()
{
    return enabled_.load();
}

BenchStats BenchRenderer::GetStats()
{
    std::lock_guard<std::mutex> lk(statsMutex_);
    return liveStats_;
}
