import UIAbility from '@ohos.app.ability.UIAbility';
import hilog from '@ohos.hilog';
import window from '@ohos.window';
import display from '@ohos.display';
import { BusinessError } from '@ohos.base';
import AbilityConstant from '@ohos.app.ability.AbilityConstant';
import Want from '@ohos.app.ability.Want';

const TAG = 'bench3d';
const DOMAIN = 0x0000;

// 默认启动窗口尺寸（vp）。鸿蒙 PC 上默认窗口偏小，侧栏控件会被截断，
// 这里主动给一个「一屏放得下」的初始尺寸，省得每次都要手动最大化
const PREFERRED_WIDTH_VP = 1120;
const PREFERRED_HEIGHT_VP = 780;
// 小于这个宽度说明是手机一类全屏窗口，resize 会被系统拒绝，交给系统决定
const MIN_FREE_WINDOW_WIDTH_VP = 720;

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(DOMAIN, TAG, '%{public}s', 'Ability onCreate');
  }

  onDestroy(): void {
    hilog.info(DOMAIN, TAG, 'Ability onDestroy');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    hilog.info(DOMAIN, TAG, '%{public}s', 'Ability onWindowStageCreate');

    windowStage.loadContent('pages/Index', (err, data) => {
      if (err.code) {
        hilog.error(DOMAIN, TAG, 'Failed to load content: %{public}s', JSON.stringify(err));
        return;
      }
      hilog.info(DOMAIN, TAG, '%{public}s', 'Succeeded in loading content');
      this.applyPreferredWindowSize(windowStage);
    });
  }

  private applyPreferredWindowSize(windowStage: window.WindowStage): void {
    windowStage.getMainWindow().then((win: window.Window) => {
      try {
        const d: display.Display = display.getDefaultDisplaySync();
        const density: number = d.densityPixels > 0 ? d.densityPixels : 1;
        const screenW: number = d.width / density;
        const screenH: number = d.height / density;
        if (screenW < MIN_FREE_WINDOW_WIDTH_VP) {
          hilog.info(DOMAIN, TAG, '%{public}s', 'small screen, keep system window size');
          return;
        }
        const wVp: number = Math.min(PREFERRED_WIDTH_VP, screenW);
        const hVp: number = Math.min(PREFERRED_HEIGHT_VP, screenH);
        win.resize(Math.round(wVp * density), Math.round(hVp * density))
          .then(() => {
            hilog.info(DOMAIN, TAG, 'window resized to %{public}d x %{public}d vp', Math.round(wVp), Math.round(hVp));
          })
          .catch((e: BusinessError) => {
            // 全屏/分屏等不可变尺寸的场景会失败，不影响使用（页面本身可滚动）
            hilog.warn(DOMAIN, TAG, 'resize failed: %{public}s', JSON.stringify(e));
          });
      } catch (e) {
        const err = e as BusinessError;
        hilog.warn(DOMAIN, TAG, 'skip resize: %{public}s', JSON.stringify(err));
      }
    }).catch((e: BusinessError) => {
      hilog.warn(DOMAIN, TAG, 'getMainWindow failed: %{public}s', JSON.stringify(e));
    });
  }

  onWindowStageDestroy(): void {
    hilog.info(DOMAIN, TAG, 'Ability onWindowStageDestroy');
  }

  onForeground(): void {
    hilog.info(DOMAIN, TAG, 'Ability onForeground');
  }

  onBackground(): void {
    hilog.info(DOMAIN, TAG, 'Ability onBackground');
  }
}
