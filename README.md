# 甜甜圈烤机（3D Benchmark）

> 镜像仓库：
> GitHub · https://github.com/JacquelineWu/harmonyos-gpu-benchmark ·
> Gitee · https://gitee.com/flyercat/harmonyos-gpu-benchmark
>
> ```bash
> git clone https://github.com/JacquelineWu/harmonyos-gpu-benchmark.git
> # 或（国内加速）
> git clone https://gitee.com/flyercat/harmonyos-gpu-benchmark.git
> ```

面向 **鸿蒙 PC（2in1）/ 平板 / 手机** 的 GPU 压力与效能测试应用，
复刻 FurMark 的经典「大毛毛甜甜圈」：一个不断旋转、长满毛发的甜甜圈，
把 GPU 的**顶点吞吐、片元 ALU、过度绘制（overdraw）**同时压满。

- **渲染管线**：XComponent（SURFACE）+ EGL + **OpenGL ES 3.0**，C++ 原生层独立渲染线程
- **几何**：参数化圆环（torus），主环/管半径 1.0 / 0.42，法线解析求出
- **毛发**：一次 `glDrawElementsInstanced` 画完「实体层 + N 层毛发壳」，
  层号 `t = gl_InstanceID / uLayers`，沿法线向外挤出并按噪声随机剔除成毛丝
- **负载旋钮**：毛发层数、片元着色器迭代次数、甜甜圈细分数（详见下）
- **度量**：实时帧率、帧时间、平均/最低/最高帧率、渲染分辨率、每帧三角形数、综合分数

## 为什么用原生 OpenGL ES，而不是 Canvas 或 ArkGraphics 3D

| 方案 | 能否烧 GPU |
|---|---|
| Canvas 2D 软渲染 | ❌ 只吃 CPU，测的是 JS 与光栅化循环 |
| `@kit.ArkGraphics3D`（Scene3D） | ⚠️ 走 GPU，但材质/着色器由引擎内置，负载不可调 |
| **XComponent + EGL + GLES3（本工程）** | ✅ 着色器自写，负载强度可连续调节 |

烤机工具的核心诉求是「**能把负载调到设备跑不动为止**」，
所以片元着色器里那段 FBM 噪声循环必须自己掌握 —— 这就是本工程走原生层的原因。

## 三个负载旋钮

| 旋钮 | 范围 | 压的是什么 |
|---|---|---|
| 毛发层数 | 4 – 128 | 几何实例数 × overdraw：每层都要重画整个甜甜圈，层间互相遮挡 |
| 着色器迭代 | 1 – 64 | 片元 ALU：每像素每层跑 N 次 value-noise，这是最狠的一项 |
| 甜甜圈细分 | 48 – 320 | 顶点/三角形吞吐：主环 × 管截面网格密度 |

四档预设：轻量 / 标准 / 压力 / 地狱。
「地狱」档（96 层 × 48 次迭代 × 256 细分）在多数移动 GPU 上会直接掉到个位数帧率。

**综合分数** = 平均帧率 × 负载复杂度，
复杂度 = `(层数/16) × (迭代/8) × (细分/128)`，
即「标准档」跑 60 FPS 约等于 60 分，可在不同档位之间横向比较。

## 目录结构

```
3d-benchmark/
├── AppScope/                       应用级配置（bundleName 等）
├── entry/src/main/
│   ├── cpp/
│   │   ├── CMakeLists.txt          原生库构建（链接 EGL / GLESv3 / ace_ndk / ace_napi）
│   │   ├── gl_core.h/.cpp          EGL 上下文、圆环网格、毛发着色器、渲染循环与统计
│   │   └── napi_bridge.cpp         XComponent 生命周期回调 + start/stop/setConfig/getStats
│   ├── ets/
│   │   ├── entryability/EntryAbility.ets
│   │   ├── model/libbench3d.d.ets  原生接口的 ArkTS 声明
│   │   ├── model/BenchModel.ets    统计结构、四档预设、复杂度公式
│   │   └── pages/Index.ets         渲染舞台 + 控制面板
│   └── resources/                  图标、字符串、配色
├── scripts/
│   ├── gen_icon.py                 生成 app_icon.png（纯 zlib 手写 PNG，无依赖）
│   ├── build_hap.sh                命令行构建（与 DevEco Studio 同一套工具链）
│   └── open-in-deveco.sh           唤起本机 DevEco Studio 打开本工程
└── build-profile.json5             HarmonyOS SDK 26（compileSdkVersion "26.0.0"）
```

## 打开与运行

1. **DevEco Studio 26.0.0+** 打开本目录（File > Open）。
2. **File > Sync and Refresh Project** 同步。
3. 右上角连接真机（鸿蒙 PC / 平板 / 手机），或直接用命令行：

```sh
sh scripts/build_hap.sh          # 产出 entry/build/default/outputs/default/*.hap
sh scripts/open-in-deveco.sh     # 唤起本机 DevEco Studio 打开本工程
```

> 签名：交付版 `build-profile.json5` 的 `signingConfigs` 留空，
> 交给 DevEco 的「自动签名」（登录华为账号后自动完成）。
> 命令行构建无签名配置时会产出 *unsigned* HAP，属正常现象。

## 已知注意事项

- 烤机时 GPU 满载、功耗与温度明显上升，笔记本建议接电源并保证散热。
- 关闭了 vsync（`eglSwapInterval(0)`），因此帧率可能远高于屏幕刷新率 —— 这是有意为之。
- 首帧前后约 1.5 秒属于着色器编译与缓存预热，不计入最低/最高帧率统计。
- 毛发层关闭了背面剔除（`glDisable(GL_CULL_FACE)`）并做双面光照，
  既让毛发更蓬松，也不依赖索引绕序。
- `deviceTypes` 含 `phone`，若只在 PC 上用可删掉；PC 对应的枚举值是 `2in1`。

## XComponent 回调的生死线（重要）

`OH_NativeXComponent_RegisterCallback` **只保存 `OH_NativeXComponent_Callback` 的指针，不拷贝内容**
（libace_ndk 里的实现就是 `str x1, [x8, #936]`）。因此该结构体**必须是静态存储期**。

```cpp
// ❌ 必崩：Init() 返回后栈帧被覆盖，OnSurfaceCreated 触发时函数指针已是垃圾
OH_NativeXComponent_Callback callback;  // 栈上局部
OH_NativeXComponent_RegisterCallback(xcomp, &callback);
//                                          ^^^^^^^^^ 悬垂

// ✅ 正确：匿名 namespace 全局量
namespace { OH_NativeXComponent_Callback g_xcompCallback = {}; }
```

踩中后的崩溃特征（供日后对照）：

```
Reason:Signal:SIGSEGV(SEGV_ACCERR)@0x0000005ab24f1648
LastFatalMessage:[OnSurfaceCreated] crash occured on callback: 0
#00 pc ... /system/lib64/chipset-sdk-sp/libc++.so     ← pc 落在只读段
#01 pc ... libace_compatible.z.so(XComponentPattern::OnSurfaceCreated()+1248)
```

其它两条本机踩实的经验：

- SDK 26 **没有** `OH_NativeXComponent_GetXComponent`，只能用
  `napi_unwrap` 从 exports 的 `__NATIVE_XCOMPONENT_OBJ__` 取实例；
  `OH_NativeXComponent_GetNativeXComponent(ArkUI_NodeHandle)` 是 ArkUI 原生节点那套，别混用。
- `hilog/log.h` 自带 `#define LOG_TAG NULL`，自定义宏要改名，否则 `-Wmacro-redefined`。
