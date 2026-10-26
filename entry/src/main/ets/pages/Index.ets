import bench3d from 'libbench3d.so';
import { BenchSnapshot, BenchPreset, EMPTY_SNAPSHOT, PRESETS } from '../model/BenchModel';

// 单个统计格：用 @Prop 保证随状态实时刷新（@Builder 按值传参不会刷新）
@Component
struct StatCell {
  @Prop label: string;
  @Prop value: string;
  @Prop accent: string;

  build() {
    Column() {
      Text(this.label)
        .fontSize(10)
        .fontColor('#8A94A6')
        .width('100%')
      Text(this.value)
        .fontSize(15)
        .fontWeight(FontWeight.Medium)
        .fontColor(this.accent)
        .margin({ top: 2 })
        .width('100%')
    }
    .padding(8)
    .borderRadius(10)
    .backgroundColor('#151A24')
    .layoutWeight(1)
  }
}

// 参数滑块行
@Component
struct ParamSlider {
  @Prop label: string;
  @Prop value: number;
  @Prop min: number;
  @Prop max: number;
  @Prop step: number;
  @Prop suffix: string;
  onValueChange: (v: number) => void = (): void => {};

  build() {
    Column() {
      Row() {
        Text(this.label)
          .fontSize(12)
          .fontColor('#9AA4B6')
        Blank()
        Text(`${this.value}${this.suffix}`)
          .fontSize(12)
          .fontColor('#E8ECF3')
      }
      .width('100%')

      Slider({
        value: this.value,
        min: this.min,
        max: this.max,
        step: this.step,
        style: SliderStyle.OutSet
      })
        .width('100%')
        .blockColor('#FF7A2F')
        .selectedColor('#FF7A2F')
        .trackColor('#2A3240')
        .trackThickness(4)
        .onChange((val: number, mode: SliderChangeMode) => {
          this.onValueChange(Math.round(val));
        })
    }
    .width('100%')
    .margin({ top: 6 })
  }
}

@Entry
@Component
struct Index {
  @State snap: BenchSnapshot = EMPTY_SNAPSHOT;
  @State layerCount: number = 24;
  @State iterCount: number = 8;
  @State segCount: number = 128;
  @State presetIndex: number = 1;
  @State burning: boolean = false;
  @State wideLayout: boolean = true;
  @State surfaceReady: boolean = false;
  private timerId: number = -1;

  aboutToAppear(): void {
    this.timerId = setInterval(() => {
      this.pullStats();
    }, 400);
  }

  aboutToDisappear(): void {
    if (this.timerId >= 0) {
      clearInterval(this.timerId);
      this.timerId = -1;
    }
    bench3d.stop();
  }

  // ---------- native 交互 ----------

  private pullStats(): void {
    let raw: string = '';
    try {
      raw = bench3d.getStats();
    } catch (e) {
      return;
    }
    if (raw.length === 0) {
      return;
    }
    try {
      const parsed: BenchSnapshot = JSON.parse(raw) as BenchSnapshot;
      // native 侧字段缺失/截断时不要拿半截对象覆盖状态，否则 UI 里 .toFixed 会读 undefined
      if (typeof parsed.fps !== 'number' || typeof parsed.score !== 'number') {
        return;
      }
      this.snap = parsed;
    } catch (e) {
      // 解析失败就保留上一次的读数，不打断烤机
    }
  }

  private applyConfig(): void {
    bench3d.setConfig(this.layerCount, this.iterCount, this.segCount);
  }

  private toggleBurn(): void {
    if (this.burning) {
      bench3d.stop();
      this.burning = false;
      return;
    }
    this.applyConfig();
    bench3d.start();
    this.burning = true;
  }

  private usePreset(item: BenchPreset, index: number): void {
    this.layerCount = item.layers;
    this.iterCount = item.iterations;
    this.segCount = item.segments;
    this.presetIndex = index;
    this.applyConfig();
  }

  // ---------- 展示辅助 ----------

  private fpsColor(): string {
    const f: number = this.snap.fps;
    if (f >= 60) {
      return '#4ADE80';
    }
    if (f >= 30) {
      return '#FACC15';
    }
    return '#F87171';
  }

  // 拖动旋钮后 presetIndex 会置 -1（自定义负载），索引越界会拿到 undefined，
  // 必须显式判空，否则 Text(...desc) 直接抛 TypeError 把整个页面打崩
  private presetDesc(): string {
    if (this.presetIndex >= 0 && this.presetIndex < PRESETS.length) {
      return PRESETS[this.presetIndex].desc;
    }
    return '自定义负载：拖动下方旋钮自由组合';
  }

  private formatDuration(sec: number): string {
    const total: number = Math.floor(sec);
    const m: number = Math.floor(total / 60);
    const s: number = total % 60;
    const ss: string = s < 10 ? `0${s}` : `${s}`;
    return `${m}:${ss}`;
  }

  private loadLabel(): string {
    const frames: number = this.snap.triangles;
    if (frames <= 0) {
      return '—';
    }
    if (frames >= 1000000) {
      return `${(frames / 1000000).toFixed(2)} M`;
    }
    return `${Math.round(frames / 1000)} K`;
  }

  // ---------- 布局 ----------

  @Builder
  renderStage() {
    Stack({ alignContent: Alignment.TopStart }) {
      XComponent({
        id: 'benchSurface',
        type: XComponentType.SURFACE,
        libraryname: 'bench3d'
      })
        .width('100%')
        .height('100%')
        .onLoad(() => {
          this.surfaceReady = true;
          this.applyConfig();
        })
        .onDestroy(() => {
          this.surfaceReady = false;
          this.burning = false;
        })

      Column() {
        Text(this.burning ? this.snap.fps.toFixed(0) : '--')
          .fontSize(52)
          .fontWeight(FontWeight.Bold)
          .fontColor(this.fpsColor())
        Text('FPS')
          .fontSize(12)
          .fontColor('#8A94A6')
          .margin({ top: 2 })
        Text(`帧时间 ${this.snap.frameMs.toFixed(2)} ms`)
          .fontSize(12)
          .fontColor('#8A94A6')
          .margin({ top: 10 })
      }
      .padding(22)
      .hitTestBehavior(HitTestMode.Transparent)

      Column() {
        if (!this.surfaceReady) {
          Text('正在初始化 GPU 表面…')
            .fontSize(14)
            .fontColor('#8A94A6')
        }
      }
      .width('100%')
      .height('100%')
      .alignItems(HorizontalAlign.Center)
      .justifyContent(FlexAlign.Center)
      .hitTestBehavior(HitTestMode.Transparent)
    }
    .layoutWeight(1)
    .width('100%')
    .backgroundColor('#05070B')
  }

  // 侧栏内容。外层由 Scroll 包裹：窗口偏矮时也能滚动看到全部内容，而不是只显示上半截
  @Builder
  panelContent() {
    Column() {
      Text('甜甜圈烤机')
        .fontSize(19)
        .fontWeight(FontWeight.Bold)
        .fontColor('#F5F7FA')
        .width('100%')
        Text('OpenGL ES 3.0 · 毛发甜甜圈 · GPU 压力测试')
        .fontSize(11)
        .fontColor('#8A94A6')
        .margin({ top: 3 })
        .width('100%')

      // 分数
      Column() {
        Text(this.snap.score.toFixed(0))
          .fontSize(28)
          .fontWeight(FontWeight.Bold)
          .fontColor('#FF7A2F')
        Text('综合分数（帧率 × 负载复杂度）')
          .fontSize(11)
          .fontColor('#8A94A6')
          .margin({ top: 2 })
      }
      .width('100%')
      .padding(12)
      .borderRadius(12)
      .backgroundColor('#151A24')
      .margin({ top: 12 })

      // 统计网格
      Row() {
        StatCell({ label: '平均帧率', value: `${this.snap.avgFps.toFixed(1)}`, accent: '#E8ECF3' })
        StatCell({ label: '最低帧率', value: `${this.snap.minFps.toFixed(1)}`, accent: '#F87171' })
      }
      .width('100%')
      .margin({ top: 6 })

      Row() {
        StatCell({ label: '最高帧率', value: `${this.snap.maxFps.toFixed(1)}`, accent: '#4ADE80' })
        StatCell({ label: '已运行时长', value: this.formatDuration(this.snap.elapsedSec), accent: '#E8ECF3' })
      }
      .width('100%')
      .margin({ top: 6 })

      Row() {
        StatCell({ label: '渲染分辨率', value: `${this.snap.width}×${this.snap.height}`, accent: '#E8ECF3' })
        StatCell({ label: '每帧三角形', value: this.loadLabel(), accent: '#E8ECF3' })
      }
      .width('100%')
      .margin({ top: 6 })

      // 档位
      Text('压力档位')
        .fontSize(13)
        .fontColor('#C7D0DE')
        .margin({ top: 12 })
        .width('100%')
      Row() {
        ForEach(PRESETS, (item: BenchPreset, index: number) => {
          Button(item.label)
            .fontSize(13)
            .layoutWeight(1)
            .height(32)
            .backgroundColor(this.presetIndex === index ? '#FF7A2F' : '#1E2531')
            .fontColor(this.presetIndex === index ? '#FFFFFF' : '#B9C2D0')
            .onClick(() => {
              this.usePreset(item, index);
            })
        }, (item: BenchPreset) => item.label)
      }
      .width('100%')
      .margin({ top: 8 })

      Text(this.presetDesc())
        .fontSize(11)
        .fontColor('#8A94A6')
        .margin({ top: 6 })
        .width('100%')

      // 三个负载旋钮
      Text('负载旋钮')
        .fontSize(13)
        .fontColor('#C7D0DE')
        .margin({ top: 12 })
        .width('100%')

      ParamSlider({
        label: '毛发层数（几何 + overdraw）',
        value: this.layerCount,
        min: 4,
        max: 128,
        step: 4,
        suffix: ' 层',
        onValueChange: (v: number): void => {
          this.layerCount = v;
          this.presetIndex = -1;
          this.applyConfig();
        }
      })

      ParamSlider({
        label: '着色器迭代（片元 ALU 负载）',
        value: this.iterCount,
        min: 1,
        max: 64,
        step: 1,
        suffix: ' 次',
        onValueChange: (v: number): void => {
          this.iterCount = v;
          this.presetIndex = -1;
          this.applyConfig();
        }
      })

      ParamSlider({
        label: '甜甜圈细分（三角形数量）',
        value: this.segCount,
        min: 48,
        max: 320,
        step: 16,
        suffix: '',
        onValueChange: (v: number): void => {
          this.segCount = v;
          this.presetIndex = -1;
          this.applyConfig();
        }
      })

      Button(this.burning ? '■ 停止烤机' : '▶ 开始烤机')
        .width('100%')
        .height(42)
        .fontSize(16)
        .fontWeight(FontWeight.Medium)
        .backgroundColor(this.burning ? '#B03A3A' : '#FF7A2F')
        .fontColor('#FFFFFF')
        .margin({ top: 12 })
        .onClick(() => {
          this.toggleBurn();
        })

      Text('烤机会让 GPU 满载、功耗与温度明显上升。笔记本请接电源，注意散热。')
        .fontSize(10)
        .fontColor('#6B7688')
        .margin({ top: 10 })
        .width('100%')
    }
    .width('100%')
    .padding(14)
  }

  build() {
    Column() {
      if (this.wideLayout) {
        Row() {
          this.renderStage();
          // 侧栏可滚动：窗口默认高度装不下全部控件时，滚动即可看到底部按钮
          Scroll() {
            this.panelContent();
          }
          .width(320)
          .height('100%')
          .scrollBar(BarState.Auto)
          .edgeEffect(EdgeEffect.Spring)
          .backgroundColor('#0E1219')
        }
        .width('100%')
        .layoutWeight(1)
      } else {
        Column() {
          this.renderStage();
          Scroll() {
            this.panelContent();
          }
          .width('100%')
          .layoutWeight(1)
          .scrollBar(BarState.Auto)
          .edgeEffect(EdgeEffect.Spring)
          .backgroundColor('#0E1219')
        }
        .width('100%')
        .layoutWeight(1)
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#0B0E14')
    .onAreaChange((oldValue: Area, newValue: Area) => {
      const w: number = typeof newValue.width === 'number' ? newValue.width : 720;
      this.wideLayout = w >= 720;
    })
  }
}
