#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <napi/native_api.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "gl_core.h"

namespace {

constexpr int BENCH_DOMAIN = 0xFF00;
constexpr const char *TAG = "Bench3D";

// 关键：OH_NativeXComponent_RegisterCallback 的实现是 `str x1, [x8, #936]`，
// 即 ACE 只把 OH_NativeXComponent_Callback 的**指针**存进 XComponent，不拷贝内容。
// 因此该结构体必须是静态存储期；写成 Init() 里的栈上局部变量，Init 返回后
// 指针即悬垂，等到 OnSurfaceCreated 触发时函数指针已是垃圾 -> SIGSEGV(SEGV_ACCERR)。
OH_NativeXComponent_Callback g_xcompCallback = {};
std::atomic<bool> g_callbackRegistered{false};

// ---------------- XComponent 生命周期回调 ----------------

void OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window)
{
    (void)component;
    OH_LOG_Print(LOG_APP, LOG_INFO, BENCH_DOMAIN, TAG, "OnSurfaceCreatedCB");
    BenchRenderer::Instance().OnSurfaceCreated(window);
}

void OnSurfaceChangedCB(OH_NativeXComponent *component, void *window)
{
    (void)component;
    BenchRenderer::Instance().OnSurfaceChanged(window);
}

void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window)
{
    (void)component;
    (void)window;
    OH_LOG_Print(LOG_APP, LOG_INFO, BENCH_DOMAIN, TAG, "OnSurfaceDestroyedCB");
    BenchRenderer::Instance().OnSurfaceDestroyed();
}

void DispatchTouchEventCB(OH_NativeXComponent *component, void *window)
{
    (void)component;
    (void)window;
}

// ---------------- JS 接口 ----------------

napi_value MakeUndefined(napi_env env)
{
    napi_value ret = nullptr;
    napi_get_undefined(env, &ret);
    return ret;
}

napi_value JsStart(napi_env env, napi_callback_info info)
{
    (void)info;
    BenchRenderer::Instance().Start();
    return MakeUndefined(env);
}

napi_value JsStop(napi_env env, napi_callback_info info)
{
    (void)info;
    BenchRenderer::Instance().Stop();
    return MakeUndefined(env);
}

napi_value JsSetConfig(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3] = {nullptr, nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int layers = 24;
    int iterations = 8;
    int segments = 128;
    if (argc > 0 && args[0] != nullptr) {
        napi_get_value_int32(env, args[0], &layers);
    }
    if (argc > 1 && args[1] != nullptr) {
        napi_get_value_int32(env, args[1], &iterations);
    }
    if (argc > 2 && args[2] != nullptr) {
        napi_get_value_int32(env, args[2], &segments);
    }
    BenchRenderer::Instance().SetConfig(layers, iterations, segments);
    return MakeUndefined(env);
}

napi_value JsGetStats(napi_env env, napi_callback_info info)
{
    (void)info;
    BenchStats s = BenchRenderer::Instance().GetStats();
    char buf[640];
    int n = snprintf(buf, sizeof(buf),
                     "{\"running\":%s,\"ready\":%s,\"fps\":%.2f,\"frameMs\":%.3f,\"avgFps\":%.2f,"
                     "\"minFps\":%.2f,\"maxFps\":%.2f,\"elapsedSec\":%.2f,\"score\":%.1f,"
                     "\"frames\":%lld,\"width\":%d,\"height\":%d,\"triangles\":%d,"
                     "\"layers\":%d,\"iterations\":%d}",
                     s.running ? "true" : "false", s.ready ? "true" : "false", s.fps, s.frameMs,
                     s.avgFps, s.minFps, s.maxFps, s.elapsedSec, s.score, s.frames, s.width,
                     s.height, s.triangles, s.furLayers, s.iterations);
    if (n < 0) {
        return MakeUndefined(env);
    }
    napi_value ret = nullptr;
    napi_create_string_utf8(env, buf, NAPI_AUTO_LENGTH, &ret);
    return ret;
}

// ---------------- 模块注册 ----------------

napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"start", nullptr, JsStart, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stop", nullptr, JsStop, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setConfig", nullptr, JsSetConfig, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getStats", nullptr, JsGetStats, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    // XComponent 会在 exports 上挂 __NATIVE_XCOMPONENT_OBJ__，取它注册回调
    // 注意：本模块会被加载两次（ArkTS `import` 一次、XComponent `libraryname` 一次），
    // 只有 XComponent 那次才带实例。这条不是错误，用 INFO 记。
    napi_value xcompObj = nullptr;
    if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &xcompObj) != napi_ok ||
        xcompObj == nullptr) {
        OH_LOG_Print(LOG_APP, LOG_INFO, BENCH_DOMAIN, TAG,
                     "no %{public}s on exports (plain import path)", OH_NATIVE_XCOMPONENT_OBJ);
        return exports;
    }

    // SDK 26 已移除 OH_NativeXComponent_GetXComponent：实例由 ACE 用 napi_wrap 挂在
    // exports 属性上，只能用 napi_unwrap 取回。
    OH_NativeXComponent *xcomp = nullptr;
    napi_status ust = napi_unwrap(env, xcompObj, reinterpret_cast<void **>(&xcomp));
    if (ust != napi_ok || xcomp == nullptr) {
        OH_LOG_Print(LOG_APP, LOG_INFO, BENCH_DOMAIN, TAG,
                     "napi_unwrap failed status=%{public}d (ignored, waiting for XComponent load)",
                     static_cast<int>(ust));
        return exports;
    }

    // 校验指针：能读出 id 说明确实是 XComponent 实例而不是野指针
    char id[128] = {0};
    uint64_t idSize = sizeof(id);
    int32_t idRc = OH_NativeXComponent_GetXComponentId(xcomp, id, &idSize);
    OH_LOG_Print(LOG_APP, LOG_INFO, BENCH_DOMAIN, TAG,
                 "unwrap ok, GetXComponentId rc=%{public}d id=%{public}s", idRc, id);

    g_xcompCallback.OnSurfaceCreated = OnSurfaceCreatedCB;
    g_xcompCallback.OnSurfaceChanged = OnSurfaceChangedCB;
    g_xcompCallback.OnSurfaceDestroyed = OnSurfaceDestroyedCB;
    g_xcompCallback.DispatchTouchEvent = DispatchTouchEventCB;
    int32_t rc = OH_NativeXComponent_RegisterCallback(xcomp, &g_xcompCallback);
    g_callbackRegistered = (rc == OH_NATIVEXCOMPONENT_RESULT_SUCCESS);
    OH_LOG_Print(LOG_APP, LOG_INFO, BENCH_DOMAIN, TAG,
                 "RegisterCallback rc=%{public}d (callback=%{public}p)", rc, &g_xcompCallback);
    return exports;
}

}  // namespace

static napi_module g_benchModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "bench3d",
    .nm_priv = nullptr,
    .reserved = {nullptr, nullptr, nullptr, nullptr},
};

extern "C" __attribute__((constructor)) void RegisterBench3DModule(void)
{
    napi_module_register(&g_benchModule);
}
