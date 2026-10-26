// libbench3d.so 的 ArkTS 侧声明（与 cpp/napi_bridge.cpp 导出的四个函数一一对应）
declare function start(): void;
declare function stop(): void;
declare function setConfig(furLayers: number, shaderIterations: number, segments: number): void;
declare function getStats(): string;

export default {
  start,
  stop,
  setConfig,
  getStats
};
