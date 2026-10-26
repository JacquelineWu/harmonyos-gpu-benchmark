// libbench3d.so 的类型声明（供 DevEco Studio 做 napi 校验与代码提示）
export declare function start(): void;
export declare function stop(): void;
export declare function setConfig(furLayers: number, shaderIterations: number, segments: number): void;
export declare function getStats(): string;
