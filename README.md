# DWZ Pad v3.0

基于 Raspberry Pi Pico 2 (RP2350 RISC-V) 的多功能桌面控制器固件，衍生自 EDA-Keypad 开源项目，UI 与动画全面重写。

![Platform](https://img.shields.io/badge/board-Raspberry%20Pi%20Pico%202-green)
![Core](https://img.shields.io/badge/core-earlephilhower%20RISC--V-blue)
![Display](https://img.shields.io/badge/display-SSD1306%20128%C3%9732-orange)

## 硬件

| 部件 | 规格 | 引脚 |
|------|------|------|
| 主控 | RP2350 RISC-V @ 150MHz | — |
| OLED | SSD1306 128×32 (I2C) | SDA=8, SCL=9 |
| 编码器 | EC11 旋转+按键 | CLK=11, DT=10, BTN=12 |
| 键帽 | 17 个机械键 | 13 / 7,6,5,4 / 20,21,22,26 / 3,2,1,0 / 16,17,18,19 |
| 灯带 | WS2812B ×16 主键底灯 | 27 |
| 灯珠 | WS2812B ×1 (DEL 键) | 14 |

按键物理布局（顶部从左到右：OLED 屏幕 / EC11 编码器 / DEL 键）：

```
                [DEL]
[7] [8] [9] [+]
[4] [5] [6] [-]
[1] [2] [3] [×]
[0] [.] [Enter] [÷]
```

## 功能

- **双核架构** — Core 0: 输入/HID/按键/编码器/计算器; Core 1: 显示/灯效/屏保
- **动画菜单系统** — 光标滑动、平滑滚屏、滚动条、弹窗滑块、页面转场动画
- **开屏动画** — 流星雨 / 烟花 / 随机（每次重抽），黑幕揭示标题
- **屏保防烧屏** — 空闲超时进入（10s/30s/60s/关 可配置），旋转多面体线框（立方体/正四面体/正八面体/正二十面体/正十二面体/随机定时换形）+ 屏幕弹跳，进场过渡动画
- **5 种 RGB 灯效** — 彩虹 / 跑马灯 / 呼吸 / 流星 / 按键闪烁（逐键按压反馈）
- **双键层** — Numpad 层（数字小键盘）/ FN 层（媒体控制、方向键、音量）
- **计算器** — USB 未挂载时自动切换，17 键完整计算
- **Flash 设置存储** — 所有配置断电保持

## 操作

| 操作 | 键盘模式 | 计算器模式 | 菜单模式 |
|------|---------|-----------|---------|
| 旋转编码器 | 系统音量 | 亮度调节 | 导航 / 调值 |
| 短按编码器 | 静音 | AC 清除 | 确认 |
| 长按编码器 | 进入菜单 | 进入菜单 | 退出菜单 |

## 菜单树

```
Main        LED             Animation       Screen Saver       Startup Logo
├ LED ──── ├ Effect ────── ├ Screen Saver  ├ Speed(滑块)      ├ Replay
├ Animation  ├ Bright(滑块)  └ Startup Logo ├ Shape(预览)      └ Style
├ Key Layer  ├ OLED(滑块)                   ├ Timeout(10s/30s/60s/Off)  ├ Meteor
└ System Info └ Speed(滑块)                  └ Back                       ├ Firework
                                                                        ├ Random
                                                                        └ Back
```

- 形状预览页用编码器直接切换多面体，`●` 标记当前已保存的形状
- 选择类页面以 `>` 前缀标记当前生效选项，导航类页面行首有 8×8 图标
- OLED 亮度调节实时生效

## 构建

依赖 PlatformIO（依赖自动从注册表拉取）：

```bash
pio run -t upload
```

构建配置见 `platformio.ini`：
- 平台：Raspberry Pi Pico 2 (RP2350)，earlephilhower Arduino core，RISC-V
- 依赖：Adafruit TinyUSB / NeoPixel / U8g2

## 目录结构

```
src/
  main.cpp    引脚 / 全局状态 / HID / 按键 / 编码器 / 灯效 / 计算器 / 设置 / 双核调度
  menu.h      共享枚举 / MenuState / 菜单接口
  menu.cpp    菜单页数据 + 导航逻辑 + 弹窗动画
  display.h   绘制接口 + U8g2 extern
  display.cpp 全部 OLED 绘制 (主屏/菜单/弹窗/屏保/开屏动画)
```

## 致谢

- 衍生自 EDA-Keypad 开源项目
- UI 设计参考 licorice-UI 与 [Astra UI](https://github.com/AstraThreshold/oled-ui-astra) 开源项目
