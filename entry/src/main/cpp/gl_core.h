#ifndef BENCH3D_GL_CORE_H
#define BENCH3D_GL_CORE_H

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <mutex>
#include <thread>

// 烤机参数：三个旋钮共同决定 GPU 负载
// - furLayers        : 毛发壳层数（几何实例数，直接放大 overdraw 与顶点吞吐）
// - shaderIterations : 片元着色器内 FBM 噪声迭代次数（纯 ALU 负载，最狠的旋钮）
// - segments         : 甜甜圈主环细分（三角形数量）
struct BenchConfig {
    int furLayers = 24;
    int shaderIterations = 8;
    int segments = 128;
    float furLength = 0.22f;
    float furDensity = 240.0f;
};

struct BenchStats {
    bool running = false;
    bool ready = false;
    double fps = 0.0;        // 最近 1 秒实时帧率
    double frameMs = 0.0;    // 最近平均帧时间
    double avgFps = 0.0;     // 本次烤机全程平均
    double minFps = 0.0;
    double maxFps = 0.0;
    double elapsedSec = 0.0;
    double score = 0.0;
    long long frames = 0;
    int width = 0;
    int height = 0;
    int triangles = 0;
    int furLayers = 0;
    int iterations = 0;
};

class BenchRenderer {
public:
    static BenchRenderer &Instance();

    // XComponent 生命周期回调（运行在 UI 线程）
    void OnSurfaceCreated(void *window);
    void OnSurfaceChanged(void *window);
    void OnSurfaceDestroyed();

    // 对外控制
    void Start();
    void Stop();
    void SetConfig(int furLayers, int shaderIterations, int segments);
    bool IsRunning();
    BenchStats GetStats();

private:
    BenchRenderer() = default;
    ~BenchRenderer() = default;
    BenchRenderer(const BenchRenderer &) = delete;
    BenchRenderer &operator=(const BenchRenderer &) = delete;

    void RenderLoop();
    bool InitEGL();
    void DestroyEGL();
    bool InitGLResources();
    void DestroyGLResources();
    void BuildTorusMesh(int segments);
    void RebuildInstanceCount(int layers);
    void RenderFrame(double timeSec);
    void UpdateStats(double frameMs);

    // EGL
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLConfig config_ = nullptr;

    // GL 资源
    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint vao_ = 0;
    GLsizei indexCount_ = 0;
    int triangles_ = 0;
    int meshSegments_ = 0;
    int instanceCount_ = 1;

    // 状态
    std::atomic<bool> running_{false};    // 渲染线程是否存活
    std::atomic<bool> enabled_{false};    // 是否在烤机（false 时线程低频空转待命）
    std::atomic<bool> surfaceReady_{false};
    std::atomic<bool> configDirty_{false};
    std::atomic<bool> meshDirty_{false};
    std::mutex windowMutex_;
    std::mutex statsMutex_;
    std::thread thread_;
    void *window_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    BenchConfig cfg_;
    BenchConfig pendingCfg_;
    BenchStats stats_;
    BenchStats liveStats_;
};

#endif  // BENCH3D_GL_CORE_H
