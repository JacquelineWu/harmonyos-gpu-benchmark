// 与 cpp/gl_core.h 的 BenchStats 对应的 JSON 结构（native 侧以 JSON 字符串回传）
export interface BenchSnapshot {
  running: boolean;
  ready: boolean;
  fps: number;
  frameMs: number;
  avgFps: number;
  minFps: number;
  maxFps: number;
  elapsedSec: number;
  score: number;
  frames: number;
  width: number;
  height: number;
  triangles: number;
  layers: number;
  iterations: number;
}

export const EMPTY_SNAPSHOT: BenchSnapshot = {
  running: false,
  ready: false,
  fps: 0,
  frameMs: 0,
  avgFps: 0,
  minFps: 0,
  maxFps: 0,
  elapsedSec: 0,
  score: 0,
  frames: 0,
  width: 0,
  height: 0,
  triangles: 0,
  layers: 24,
  iterations: 8
};

// 四档预设：从「能跑」到「把 GPU 按在地上摩擦」
export interface BenchPreset {
  label: string;
  desc: string;
  layers: number;
  iterations: number;
  segments: number;
}

export const PRESETS: BenchPreset[] = [
  { label: '轻量', desc: '低负载，看甜甜圈长什么样', layers: 8, iterations: 2, segments: 64 },
  { label: '标准', desc: '日常压力，多数设备能跑满', layers: 24, iterations: 8, segments: 128 },
  { label: '压力', desc: '重载，风扇开始转', layers: 48, iterations: 20, segments: 192 },
  { label: '地狱', desc: '极限烤机，看它能撑多久', layers: 96, iterations: 48, segments: 256 }
];

export function complexityOf(layers: number, iterations: number, segments: number): number {
  const c = (layers / 16) * (iterations / 8) * (segments / 128);
  return c < 0.05 ? 0.05 : c;
}
