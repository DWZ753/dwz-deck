#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <cmath>
#include <EEPROM.h>
#include "menu.h"
#include "display.h"

/* ================================================================
 *  DWZ Pad v3.0 固件
 *  平台: Raspberry Pi Pico 2 (RP2350 RISC-V, earlephilhower core)
 *  功能: 菜单系统 / FN层 / 系统音量 / 计算器 / 4种灯效 / 屏保
 * ================================================================ */

/* ================================================================
 *  SECTION 1: 引脚与常量定义
 * ================================================================ */

/* 编码器 */
#define EC11_CLK      11
#define EC11_DT       10
#define ENCODER_BTN   12   /* EC11 自带按键 */

/* OLED (I2C) */
#define SDA_PIN       8
#define SCL_PIN       9
#define OLED_ADDR     0x3C

/* RGB 灯带 */
#define LED_PIN1      27   /* 16颗主键底灯 */
#define LED_PIN2      14   /* DEL 键单灯 */
#define NUM_LEDS1     16
#define NUM_LEDS2     1

/* 按键矩阵: 17个机械键帽 (GPIO 12 已移除，归编码器) */
#define NUM_KEYS      17

/* 时序常量 */
#define LONG_PRESS_MS    800
#define KEY_FEEDBACK_MS  50

/* Flash 设置存储 */
#define SETTINGS_MAGIC  0xEDA1
#define EEPROM_SIZE     64

/* ================================================================
 *  SECTION 2: 枚举与结构体
 * ================================================================ */

/* 消费类 HID 报告 (音量/媒体键) */
typedef struct {
    uint16_t usage;
} ConsumerReport;

/* 键盘 HID 报告 */
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} KeyboardReport;

/* ================================================================
 *  SECTION 3: 全局对象
 * ================================================================ */

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_NeoPixel   strip1(NUM_LEDS1, LED_PIN1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel   strip2(NUM_LEDS2, LED_PIN2, NEO_GRB + NEO_KHZ800);
Adafruit_USBD_HID   usb_hid;

/* ================================================================
 *  SECTION 4: 按键矩阵定义
 *
 *  物理布局:              数组索引:
 *  [7] [8] [9] [+]        [1]  [2]  [3]  [4]
 *  [4] [5] [6] [-]        [6]  [7]  [8]  [9]
 *  [1] [2] [3] [x]        [10] [11] [12] [13]
 *  [.] [0] [=] [/]        [14] [15] [16] [17]
 *      [DEL]              [0]
 * ================================================================ */

const uint8_t keyPins[NUM_KEYS] = {
    13,              /* [ 0] DEL        */
    7, 6, 5, 4,      /* [ 1] 7  [ 2] 8  [ 3] 9  [ 4] + */
    20, 21, 22, 26,  /* [ 6] 4  [ 7] 5  [ 8] 6  [ 9] - */
    3, 2, 1, 0,      /* [10] 1  [11] 2  [12] 3  [13] x */
    16, 17, 18, 19   /* [14] .  [15] 0  [16] =  [17] / */
};

/* 按键索引 → strip1 LED 位置映射
 * DEL (idx 0) 走 strip2, 此处填 -1;
 * 其余 16 键走 strip1 LED 0-15, 按物理行列顺序排列
 * 如灯珠顺序不同, 调整此数组即可 */
const int8_t key_to_led[NUM_KEYS] = {
    -1,              /* [ 0] DEL → strip2 */
    0,  1,  2,  3,   /* [ 1] 7  [ 2] 8  [ 3] 9  [ 4] + */
    7,  6,  5,  4,   /* [ 6] 4  [ 7] 5  [ 8] 6  [ 9] - */
    8,  9,  10, 11,  /* [10] 1  [11] 2  [12] 3  [13] x */
    15, 14, 13, 12   /* [14] .  [15] 0  [16] =  [17] / */
};

/* 默认层: 数字小键盘 HID 码 */
const uint8_t keyCodes[NUM_KEYS] = {
    0x2A,             /* DEL → Backspace */
    0x5F, 0x60, 0x61, 0x57,  /* 7 8 9 + (numpad) */
    0x5C, 0x5D, 0x5E, 0x56,  /* 4 5 6 - */
    0x59, 0x5A, 0x5B, 0x55,  /* 1 2 3 * */
    0x62, 0x63, 0x58, 0x54   /* . 0 = / */
};

/* FN 层: 键盘 HID 码 (0x00 = 走 Consumer 或特殊处理) */
const uint8_t keyCodesFN[NUM_KEYS] = {
    0x00,             /* DEL → 切回数字层 (特殊处理) */
    0x00, 0x52, 0x00, /* 7→媒体  8→↑  9→媒体 */
    0x00,             /* + → Vol+ (Consumer) */
    0x50, 0x51, 0x4F, /* 4→←  5→↓  6→→ */
    0x00,             /* - → Vol- (Consumer) */
    0x4A, 0x4D, 0x29, /* 1→Home  2→End  3→Esc */
    0x2B,             /* x → Tab */
    0x4B, 0x4E, 0x28, /* .→PgUp  0→PgDn  =→Enter */
    0x00              /* / → 无 */
};

/* FN 层: Consumer HID 码 (0x0000 = 走键盘码) */
const uint16_t keyConsFN[NUM_KEYS] = {
    0x0000,                                /* DEL */
    0x00B6, 0x0000, 0x00B5,                /* 7=⏮  8=无  9=⏭ */
    0x00E9,                                /* + = Vol+ */
    0x0000, 0x0000, 0x0000,                /* ← ↓ → (键盘码) */
    0x00EA,                                /* - = Vol- */
    0x0000, 0x0000, 0x0000,                /* Home End Esc (键盘码) */
    0x0000,                                /* Tab */
    0x0000, 0x0000, 0x0000,                /* PgUp PgDn Enter (键盘码) */
    0x0000                                 /* / */
};

/* 计算器模式按键标签 */
const String calcLabels[NUM_KEYS] = {
    "DEL",
    "7", "8", "9", "+",
    "4", "5", "6", "-",
    "1", "2", "3", "x",
    ".", "0", "=", "/"
};

/* ================================================================
 *  SECTION 5: HID 报告描述符 (键盘 + 消费类)
 * ================================================================ */

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};

/* ================================================================
 *  SECTION 7: 全局运行时状态
 * ================================================================ */

/* 双核间共享 (Core 0 写, Core 1 读) */
volatile AppMode     g_mode       = MODE_CALCULATOR;
volatile KeyLayer    g_layer      = LAYER_NUMPAD;
volatile LEDMode     g_led_mode   = LED_RAINBOW;
volatile int         g_led_speed  = 50;      /* 0-100%, 越高越快 */
volatile int         g_brightness = 20;      /* 0-100% */
volatile bool        g_usb_mounted = false;

/* 屏保: Core 0 更新活动时间, Core 1 检测触发 */
volatile unsigned long g_activity_ms  = 0;
volatile bool          g_screensaver  = false;
volatile int           g_cube_speed   = 50;
volatile int           g_cube_mode    = 0;   /* 按面数升序: 0=Tetra 1=Cube 2=Octa 3=Dodeca 4=Icosa 5=Random */
volatile int           g_logo_style   = 0;   /* 0=流星雨 1=烟花 2=随机 */
volatile int           g_oled_bright  = 100; /* OLED 对比度 0-100% */
volatile bool          g_contrast_dirty = false; /* Core0 置位, Core1 写屏 */
volatile int           g_saver_timeout = 0;  /* 0=10s 1=30s 2=60s 3=关闭 */
volatile bool          g_replay_logo  = false;

/* Flash 存储脏标志 */
volatile bool        settings_dirty      = false;
unsigned long        settings_dirty_time = 0;

/* Core 0 按键状态 */
bool      key_pressed[NUM_KEYS] = {false};
bool      encoder_btn_down       = false;
unsigned long encoder_btn_press_ms = 0;
bool      long_press_fired       = false;

/* 计算器状态 (Core 0) */
String calc_expression  = "";
String calc_display     = "0";
String calc_num_buf     = "";
bool   calc_needs_clear = false;

/* 按键反馈时间戳 (Core 0 写, Core 1 读) */
volatile unsigned long key_feedback_time = 0;

/* 逐键反馈: 按下后灯珠亮起随机色 → 渐灭
 * Core 0 写入 g_fb_pending, Core 1 消费后触发动画 */
#define MAX_FEEDBACKS    4
volatile int g_fb_pending = -1;  /* -1=无, >=0=待反馈的 key_idx */

struct KeyFB {
    int          led;        /* strip1 LED 索引, -1=strip2 */
    uint32_t     color;      /* 全亮颜色 (RGB) */
    unsigned long start_ms;
    bool         active;
    bool         on_strip2;  /* true=DEL 键 (strip2), false=strip1 */
};

KeyFB g_fb[MAX_FEEDBACKS];

/* HID 报告缓存 */
KeyboardReport  kb_report  = {0};
ConsumerReport  cons_report = {0};

/* 灯效动画状态 */
unsigned long led_anim_timer = 0;
int           marquee_pos    = 0;
float         breath_phase   = 0.0f;
int           meteor_dots[4] = {-1, -1, -1, -1};  /* 流星位置 */
unsigned long meteor_timers[4] = {0};

/* ================================================================
/* 弹窗动画重置 (进入弹窗时调用) */
/* 屏保: 旋转 wireframe 多面体 — 多轴旋转 + 弹跳 */
/* ================================================================
 *  SECTION 9: LED 灯效
 * ================================================================ */

/* 色轮 (彩虹) */
uint32_t wheel(byte pos)
{
    pos = 255 - pos;
    if (pos < 85)
        return strip1.Color(255 - pos * 3, 0, pos * 3);
    if (pos < 170) {
        pos -= 85;
        return strip1.Color(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return strip1.Color(pos * 3, 255 - pos * 3, 0);
}

/* 获取灯效更新间隔: 0=最慢 255=最快 */
unsigned long led_interval_ms()
{
    return map(g_led_speed, 0, 100, 120, 10);
}

/* 彩虹轮播 (非阻塞) */
void effect_rainbow()
{
    static byte offset = 0;
    unsigned long now = millis();

    if (now - led_anim_timer < led_interval_ms()) return;
    led_anim_timer = now;

    for (int i = 0; i < NUM_LEDS1; i++) {
        strip1.setPixelColor(i, wheel((i + offset) & 255));
    }
    strip1.setBrightness(map(g_brightness, 0, 100, 0, 255));
    strip1.show();
    offset++;
}

/* 跑马灯 (非阻塞) */
void effect_marquee()
{
    unsigned long now = millis();

    if (now - led_anim_timer < map(g_led_speed, 0, 100, 500, 60)) return;
    led_anim_timer = now;

    strip1.clear();
    strip1.setPixelColor(marquee_pos, 0xFF0000);
    strip1.setPixelColor((marquee_pos + 4) % NUM_LEDS1, 0x00FF00);
    strip1.setPixelColor((marquee_pos + 8) % NUM_LEDS1, 0x0000FF);
    strip1.setBrightness(map(g_brightness, 0, 100, 0, 255));
    strip1.show();

    marquee_pos = (marquee_pos + 1) % NUM_LEDS1;
}

/* 呼吸灯 (非阻塞, 正弦波, 固定高速刷新保证平滑) */
void effect_breathing()
{
    static unsigned long breath_timer = 0;
    unsigned long now = millis();

    /* 固定 15ms 刷新确保平滑, 不受全局速度影响 */
    if (now - breath_timer < 15) return;
    breath_timer = now;

    /* 速度控制呼吸周期: 0=最慢 255=最快 */
    float step = map(g_led_speed, 0, 100, 1, 8) / 100.0f;

    breath_phase += step;
    if (breath_phase > TWO_PI) breath_phase -= TWO_PI;

    byte b = (byte)((sin(breath_phase) + 1.0f) * 127.5f);
    byte effective = (b * g_brightness) / 100;

    for (int i = 0; i < NUM_LEDS1; i++) {
        strip1.setPixelColor(i, strip1.Color(0, 180, 255));
    }
    strip1.setBrightness(effective);
    strip1.show();
}

/* 流星雨 (非阻塞) */
void effect_meteor()
{
    unsigned long now = millis();
    int base = map(g_led_speed, 0, 100, 120, 25);
    int meteor_speed[] = {base, base + 15, base + 30, base + 10};

    for (int m = 0; m < 4; m++) {
        if (now - meteor_timers[m] < meteor_speed[m]) continue;
        meteor_timers[m] = now;

        /* 旧位置渐灭 */
        if (meteor_dots[m] >= 0) {
            strip1.setPixelColor(meteor_dots[m], 0);
        }

        /* 新随机位置 */
        meteor_dots[m] = random(NUM_LEDS1);
        uint32_t color = wheel(random(256));
        strip1.setPixelColor(meteor_dots[m], color);
    }
    strip1.setBrightness(map(g_brightness, 0, 100, 0, 255));
    strip1.show();
}

/* 灯效分发 */
void run_led_effect()
{
    static LEDMode prev = LED_MODE_COUNT;

    switch (g_led_mode) {
        case LED_RAINBOW:   effect_rainbow();   break;
        case LED_MARQUEE:   effect_marquee();   break;
        case LED_BREATHING: effect_breathing();  break;
        case LED_METEOR:    effect_meteor();     break;
        case LED_KEY_FLASH: {
            /* 切入 Flash 时清屏, 之后交给 feedback 系统 */
            if (prev != LED_KEY_FLASH) {
                strip1.clear();
                strip1.show();
            }
            break;
        }
    }

    prev = g_led_mode;
}

/* 按键反馈: strip2 短暂闪白 */
void led_key_feedback()
{
    strip2.setPixelColor(0, 0xFFFFFF);
    strip2.setBrightness(200);
    strip2.show();
    key_feedback_time = millis();
}

/* 清除按键反馈 (每帧调用) */
void led_feedback_clear()
{
    if (key_feedback_time == 0) return;
    if (millis() - key_feedback_time < KEY_FEEDBACK_MS) return;

    strip2.clear();
    strip2.show();
    key_feedback_time = 0;
}

/* Core 0 调用: 通知 Core 1 有按键按下, 传 key_idx */
static void trigger_key_feedback(int key_idx)
{
    g_fb_pending = key_idx;
}

/* Core 1 调用: 消费 g_fb_pending 并启动动画 */
static void feedback_consume()
{
    if (g_led_mode != LED_KEY_FLASH) return;

    int idx = g_fb_pending;
    if (idx < 0) return;
    g_fb_pending = -1;  /* 取走信号 */

    int led = key_to_led[idx];
    bool on_s2 = (led < 0);

    /* 同 LED 已有动画则重置, 不同 LED 找空闲槽位 */
    for (int i = 0; i < MAX_FEEDBACKS; i++) {
        if (g_fb[i].active && g_fb[i].led == led && g_fb[i].on_strip2 == on_s2) {
            g_fb[i].color    = strip1.ColorHSV(random(65536), 255, 255);
            g_fb[i].start_ms = millis();
            return;
        }
    }
    for (int i = 0; i < MAX_FEEDBACKS; i++) {
        if (!g_fb[i].active) {
            g_fb[i].led       = led;
            g_fb[i].on_strip2 = on_s2;
            g_fb[i].color     = strip1.ColorHSV(random(65536), 255, 255);
            g_fb[i].start_ms  = millis();
            g_fb[i].active    = true;
            break;
        }
    }
}

/* 每帧调用: 渲染活跃反馈的渐灭动画
 * 渐灭时长跟随 LED 速度: fast=300ms, med=600ms, slow=1200ms */
static void update_key_feedback()
{
    static unsigned long fb_timer = 0;
    if (millis() - fb_timer < 14) return;  /* ~70fps */
    fb_timer = millis();

    unsigned long dur = map(g_led_speed, 0, 100, 1500, 200);

    bool s1 = false, s2 = false;

    for (int i = 0; i < MAX_FEEDBACKS; i++) {
        if (!g_fb[i].active) continue;

        unsigned long t = millis() - g_fb[i].start_ms;
        if (t >= dur) {
            /* 渐灭结束, 熄灯; strip2 恢复亮度让指示器接管 */
            if (g_fb[i].on_strip2) {
                strip2.setPixelColor(0, 0);
                strip2.setBrightness(0);
                s2 = true;
            } else {
                strip1.setPixelColor(g_fb[i].led, 0);
                s1 = true;
            }
            g_fb[i].active = false;
        } else {
            /* 线性渐灭: 255 → 0 */
            byte b = 255 - (byte)(t * 255 / dur);
            uint32_t c = g_fb[i].color;
            byte r = ((c >> 16) & 0xFF) * b / 255;
            byte g = ((c >>  8) & 0xFF) * b / 255;
            byte bl = (c & 0xFF) * b / 255;

            if (g_fb[i].on_strip2) {
                strip2.setBrightness(80);
                strip2.setPixelColor(0, r, g, bl);
                s2 = true;
            } else {
                strip1.setPixelColor(g_fb[i].led, r, g, bl);
                s1 = true;
            }
        }
    }

    if (s1) strip1.show();
    if (s2) strip2.show();
}

/* 检查 strip2 是否有反馈动画进行中 */
static bool fb_on_strip2()
{
    for (int i = 0; i < MAX_FEEDBACKS; i++) {
        if (g_fb[i].active && g_fb[i].on_strip2) return true;
    }
    return false;
}

/* 更新 DEL 键指示灯 (FN 层亮红) */
void led_update_del_indicator()
{
    /* 反馈动画进行中时不覆盖 strip2 */
    if (fb_on_strip2()) return;

    if (g_layer == LAYER_FN && g_usb_mounted) {
        strip2.setPixelColor(0, 0xFF0000);
        strip2.setBrightness(40);
        strip2.show();
    } else {
        strip2.setPixelColor(0, 0);
        strip2.setBrightness(0);
        strip2.show();
    }
}

/* ================================================================
/* ================================================================
 *  SECTION 11: 编码器处理 (Core 0)
 * ================================================================ */

void encoder_init()
{
    pinMode(EC11_CLK, INPUT_PULLUP);
    pinMode(EC11_DT,  INPUT_PULLUP);
    pinMode(ENCODER_BTN, INPUT_PULLUP);
}

/* 返回: -1=逆时针, 0=无转动, 1=顺时针
 * 完整 4 态正交解码, 必须经过 00 中间态才判定为有效脉冲.
 * EC11 触点反弹只会在 11/01 或 11/10 间跳变, 不会到达 00,
 * 因此强制要求 00 即可滤除所有反弹误触发.
 * 1 detent = 1 脉冲, 方向自动识别. */
int encoder_read_rotation()
{
    static uint8_t       prev     = 0b11;
    static bool          saw_00   = false;
    static uint8_t       dir_hint = 0;      /* 0=无 1=CW 2=CCW */
    static unsigned long last_t   = 0;

    uint8_t s = (digitalRead(EC11_CLK) << 1) | digitalRead(EC11_DT);
    if (s == prev) return 0;

    unsigned long now = millis();
    if (now - last_t < 5) {
        prev = s;
        return 0;
    }

    int dir = 0;

    if (s == 0b11 && prev != 0b11) {
        /* 回到静止态: 仅当经历了 00 中间态才计数 */
        if (saw_00 && dir_hint != 0) {
            dir = (dir_hint == 1) ? 1 : -1;
        }
        saw_00   = false;
        dir_hint = 0;
    } else if (s == 0b00) {
        saw_00 = true;                       /* 到达中间态, 确认有效转动 */
    } else if (prev == 0b11) {
        /* 离开静止态: 记录初始方向 */
        if (s == 0b10)      dir_hint = 1;    /* CW */
        else if (s == 0b01) dir_hint = 2;    /* CCW */
    }

    last_t = now;
    prev   = s;
    return dir;
}

void encoder_handle_rotation()
{
    int dir = encoder_read_rotation();
    if (dir == 0) return;

    g_activity_ms = millis();
    g_screensaver = false;

    if (g_mode == MODE_MENU) {
        menu_navigate(dir);
        return;
    }

    if (g_usb_mounted) {
        /* 键盘模式: 每 detent 发一次瞬时音量脉冲 (press+release)
         * 必须间隔 10ms 以上, 否则 TinyUSB/Host 可能把两次报告
         * 合并处理, 导致主机认为按键持续按住 → 触发重复暴冲 */
        static unsigned long last_vol = 0;
        if (millis() - last_vol < 15) return;

        if (!usb_hid.ready()) return;

        ConsumerReport cr;
        cr.usage = (dir > 0) ? 0x00E9 : 0x00EA;
        usb_hid.sendReport(2, &cr, sizeof(cr));
        delay(10);
        cr.usage = 0;
        usb_hid.sendReport(2, &cr, sizeof(cr));
        led_key_feedback();
        last_vol = millis();
    } else {
        /* 计算器模式: LED 亮度 */
        int b = g_brightness + dir * 2;
        if (b < 0)   b = 0;
        if (b > 100) b = 100;
        g_brightness = b;
        settings_mark_dirty();
    }
}

void encoder_handle_button()
{
    bool pressed = (digitalRead(ENCODER_BTN) == LOW);

    if (pressed && !encoder_btn_down) {
        /* 按下 */
        encoder_btn_down     = true;
        encoder_btn_press_ms = millis();
        long_press_fired     = false;
        g_activity_ms        = millis();
        g_screensaver        = false;
    }
    else if (pressed && encoder_btn_down && !long_press_fired) {
        /* 持续按住: 检测长按 */
        if (millis() - encoder_btn_press_ms >= LONG_PRESS_MS) {
            long_press_fired = true;
            /* 长按: 菜单中→回主屏 / 主屏中→进菜单 */
            if (g_mode == MODE_MENU) {
                menu_close();
            } else {
                menu_open();
                g_mode = MODE_MENU;
            }
        }
    }
    else if (!pressed && encoder_btn_down) {
        /* 松开: 短按 */
        encoder_btn_down = false;
        if (long_press_fired) return;  /* 长按已触发, 忽略短按 */

        if (g_mode == MODE_MENU) {
            menu_select();
        }
        else if (g_usb_mounted) {
            /* 键盘模式: 静音 */
            cons_report.usage = 0x00E2;  /* Mute */
            usb_hid.sendReport(2, &cons_report, sizeof(cons_report));
            memset(&cons_report, 0, sizeof(cons_report));
            usb_hid.sendReport(2, &cons_report, sizeof(cons_report));
        }
        else {
            /* 计算器模式: AC 清除 */
            calc_display     = "0";
            calc_expression  = "";
            calc_num_buf     = "";
            calc_needs_clear = false;
        }
    }
}

/* ================================================================
 *  SECTION 11.5: Flash 设置存储
 * ================================================================ */

struct Settings {
    uint16_t magic;
    uint8_t  led_mode;
    uint8_t  led_speed;
    uint8_t  brightness;
    uint8_t  key_layer;
    uint8_t  cube_speed;
    uint8_t  cube_mode;
    uint8_t  logo_style;
    uint8_t  oled_bright;
    uint8_t  saver_timeout;
};

static void settings_load()
{
    EEPROM.begin(EEPROM_SIZE);

    Settings s;
    EEPROM.get(0, s);

    if (s.magic == SETTINGS_MAGIC) {
        g_led_mode   = (LEDMode)s.led_mode;
        g_led_speed  = s.led_speed;
        g_brightness = (s.brightness <= 100) ? s.brightness : 20;
        g_layer      = (KeyLayer)s.key_layer;
        g_cube_speed = (s.cube_speed <= 100) ? s.cube_speed : 50;
        g_cube_mode  = (s.cube_mode  <= 5)   ? s.cube_mode  : 0;
        g_logo_style = (s.logo_style <= 2)   ? s.logo_style : 0;
        g_oled_bright  = (s.oled_bright  <= 100) ? s.oled_bright  : 100;
        g_saver_timeout = (s.saver_timeout <= 3) ? s.saver_timeout : 0;
    }
    /* Magic 不匹配则保留编译期默认值 */
}

static void settings_save()
{
    Settings s;
    s.magic      = SETTINGS_MAGIC;
    s.led_mode   = (uint8_t)g_led_mode;
    s.led_speed  = (uint8_t)g_led_speed;
    s.brightness = (uint8_t)g_brightness;
    s.key_layer  = (uint8_t)g_layer;
    s.cube_speed = (uint8_t)g_cube_speed;
    s.cube_mode  = (uint8_t)g_cube_mode;
    s.logo_style = (uint8_t)g_logo_style;
    s.oled_bright  = (uint8_t)g_oled_bright;
    s.saver_timeout = (uint8_t)g_saver_timeout;

    EEPROM.put(0, s);
    EEPROM.commit();
}

void settings_mark_dirty()
{
    settings_dirty      = true;
    settings_dirty_time = millis();
}

/* ================================================================
 *  SECTION 12: 计算器引擎 (Core 0)
 * ================================================================ */

float calc_evaluate(String expr)
{
    expr.replace(" ", "");
    float result      = 0;
    float current_term = 0;
    char  op           = '+';
    String num_str;

    for (int i = 0; i < (int)expr.length(); i++) {
        char c = expr[i];
        if (isdigit(c) || c == '.' ||
            (c == '-' && (i == 0 || expr[i-1] == 'x' ||
                          expr[i-1] == '/' || expr[i-1] == '+' ||
                          expr[i-1] == '-'))) {
            num_str += c;
        } else if (num_str.length() > 0) {
            float num = num_str.toFloat();
            num_str = "";
            switch (op) {
                case '+': current_term  = num;             break;
                case '-': current_term  = -num;            break;
                case 'x': current_term *= num;             break;
                case '/':
                    if (num == 0) return NAN;
                    current_term /= num;
                    break;
            }
            if (c == '+' || c == '-') {
                result += current_term;
                current_term = 0;
            }
            op = c;
        }
    }

    if (num_str.length() > 0) {
        float num = num_str.toFloat();
        switch (op) {
            case '+': current_term  = num;  break;
            case '-': current_term  = -num; break;
            case 'x': current_term *= num;  break;
            case '/':
                if (num == 0) return NAN;
                current_term /= num;
                break;
        }
    }
    return result + current_term;
}

void calc_process_key(const String& key)
{
    if (key == "=") {
        if (calc_expression.length() > 0) {
            float result = calc_evaluate(calc_expression);
            if (isnan(result)) {
                calc_display = "Error";
            } else {
                calc_display = String(result, 5);
            }
            calc_needs_clear = true;
            calc_expression  = String(result, 5);
            calc_num_buf     = "";
        }
    }
    else if (key == "DEL") {
        if (calc_expression.length() > 0) {
            calc_expression.remove(calc_expression.length() - 1);
            calc_num_buf.remove(calc_num_buf.length() - 1);
            calc_display = calc_num_buf;
            if (calc_display.length() == 0) calc_display = "0";
        }
    }
    else if (key == "+" || key == "-" || key == "x" || key == "/") {
        calc_expression += key;
        calc_num_buf    = key;
        calc_display    = key;
    }
    else {
        /* 数字/小数点 */
        if (calc_needs_clear) {
            calc_needs_clear = false;
        }
        calc_expression += key;
        calc_num_buf    += key;
        calc_display    = calc_num_buf;
    }
}

/* ================================================================
 *  SECTION 13: 按键扫描 (Core 0)
 * ================================================================ */

/* 发送键盘 HID 报告 (仅按下, 不释放; 释放由 scan_keys 管理) */
static void kb_press(uint8_t code)
{
    if (!usb_hid.ready()) return;
    kb_report.keycode[0] = code;
    usb_hid.sendReport(1, &kb_report, sizeof(kb_report));
}

static void kb_release()
{
    if (!usb_hid.ready()) return;
    memset(&kb_report, 0, sizeof(kb_report));
    usb_hid.sendReport(1, &kb_report, sizeof(kb_report));
}

/* 发送消费类 HID 报告 (单次: press + release) */
static void hid_send_consumer(uint16_t usage)
{
    if (!usb_hid.ready()) return;
    cons_report.usage = usage;
    usb_hid.sendReport(2, &cons_report, sizeof(cons_report));
    delay(10);
    memset(&cons_report, 0, sizeof(cons_report));
    usb_hid.sendReport(2, &cons_report, sizeof(cons_report));
}

/* 当前按住的按键索引 (-1 = 无) */
static int held_key = -1;

void scan_keys()
{
    for (int i = 0; i < NUM_KEYS; i++) {
        bool down = (digitalRead(keyPins[i]) == LOW);

        if (down && !key_pressed[i]) {
            key_pressed[i] = true;
            g_activity_ms  = millis();
            g_screensaver  = false;
            trigger_key_feedback(i);

            if (g_usb_mounted) {
                /* USB 键盘模式 - 按下不释放, 主机自动重复 */
                if (g_layer == LAYER_NUMPAD) {
                    held_key = i;
                    kb_press(keyCodes[i]);
                } else {
                    /* FN 层: 消费类键立即释放, 键盘键持续按住 */
                    if (i == 0) {
                        g_layer = LAYER_NUMPAD;
                    }
                    else if (keyConsFN[i] != 0) {
                        hid_send_consumer(keyConsFN[i]);
                    }
                    else if (keyCodesFN[i] != 0) {
                        held_key = i;
                        kb_press(keyCodesFN[i]);
                    }
                }
            } else {
                /* 计算器模式 */
                calc_process_key(calcLabels[i]);
            }
        }
        else if (!down && key_pressed[i]) {
            key_pressed[i] = false;
            /* 释放按键 */
            if (g_usb_mounted && i == held_key) {
                kb_release();
                held_key = -1;
            }
        }
    }
}

/* ================================================================
 *  SECTION 14: Core 0 — 入口与主循环
 *  (HID 初始化 + 编码器 + 按键扫描)
 * ================================================================ */

void setup()
{
    /* 从 Flash 加载设置 */
    settings_load();

    /* 按键矩阵 */
    for (int i = 0; i < NUM_KEYS; i++) {
        pinMode(keyPins[i], INPUT_PULLUP);
    }

    /* 编码器 */
    encoder_init();

    /* USB HID: 键盘 + 消费类 */
    usb_hid.setBootProtocol(false);
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.setPollInterval(2);
    usb_hid.begin();
}

void loop()
{
    /* 检测 USB 挂载状态 */
    g_usb_mounted = TinyUSBDevice.mounted();

    /* 编码器旋转 */
    encoder_handle_rotation();

    /* 编码器按键 */
    encoder_handle_button();

    /* Flash 存储 — 延迟 2 秒写入, 减少擦写磨损 */
    if (settings_dirty && (millis() - settings_dirty_time > 2000)) {
        settings_save();
        settings_dirty = false;
    }

    /* 菜单模式: 不发送 HID, 但 Flash 灯效时仍扫描反馈 */
    if (g_mode == MODE_MENU) {
        if (g_led_mode == LED_KEY_FLASH) {
            for (int i = 0; i < NUM_KEYS; i++) {
                bool down = (digitalRead(keyPins[i]) == LOW);
                if (down && !key_pressed[i]) {
                    key_pressed[i] = true;
                    trigger_key_feedback(i);
                }
                else if (!down && key_pressed[i]) {
                    key_pressed[i] = false;
                }
            }
        }
        delay(5);
        return;
    }

    /* 自动模式切换 */
    g_mode = g_usb_mounted ? MODE_KEYBOARD : MODE_CALCULATOR;

    /* 按键扫描 */
    scan_keys();
    delay(5);
}

/* ================================================================
 *  SECTION 15: Core 1 — 显示与灯效
 * ================================================================ */

void setup1()
{
    /* OLED */
    Wire.setSDA(SDA_PIN);
    Wire.setSCL(SCL_PIN);
    Wire.begin();
    u8g2.begin();
    /* I2C 默认 100kHz: 整屏 512B 传输 ~46ms (~21fps), 400k 后 ~12ms (~77fps)
     * setBusClock 写入 u8x8 结构, U8g2 每次传输前自动应用 */
    u8g2.setBusClock(400000);
    u8g2.setContrast(map(g_oled_bright, 0, 100, 0, 255));
    oled_logo_animation();
    /* 开屏动画不计入屏保空闲时间 */
    g_activity_ms = millis();

    /* LED */
    strip1.begin();
    strip1.setBrightness(map(g_brightness, 0, 100, 0, 255));
    strip1.clear();
    strip1.show();

    strip2.begin();
    strip2.setBrightness(40);
    strip2.clear();
    strip2.show();

    /* 菜单初始状态 */
    menu.is_open = false;
    menu.slider_active = false;
}

void loop1()
{
    /* OLED 对比度脏标志 (Core 0 置位): 渲染线程内写屏, 避免总线竞争 */
    if (g_contrast_dirty) {
        g_contrast_dirty = false;
        u8g2.setContrast(map(g_oled_bright, 0, 100, 0, 255));
    }

    /* 重放开屏动画 (调试用, 从菜单触发) */
    if (g_replay_logo) {
        g_replay_logo = false;
        oled_logo_animation();
        /* 重放不计入屏保空闲时间 */
        g_activity_ms = millis();
    }

    /* 菜单超时检查 */
    menu_check_timeout();

    /* 如果菜单关闭且有挂载状态变化, 同步模式 */
    if (!menu.is_open) {
        if (g_usb_mounted && g_mode != MODE_KEYBOARD)
            g_mode = MODE_KEYBOARD;
        else if (!g_usb_mounted && g_mode != MODE_CALCULATOR)
            g_mode = MODE_CALCULATOR;
    }

    /* 屏保检测: 非菜单模式下空闲超时触发 (可配置: 10s/30s/60s/关) */
    static const unsigned long SAVER_TIMEOUTS[] = {10000, 30000, 60000};
    static bool  saver_entering = false;   /* 进场过渡进行中 */
    static float saver_slide    = 0.0f;    /* 主屏下落偏移 0→32 */
    static float saver_zoom     = 0.2f;    /* 多边形缩放 0.2→1 (独立缓动, 慢于下落) */
    static bool  saver_hold     = false;   /* 定格展示: 放大完成后暂停漂移 */
    static unsigned long saver_hold_until = 0;
    if (!menu.is_open && !menu.animating) {
        if (!g_screensaver
            && g_saver_timeout < 3
            && (millis() - g_activity_ms > SAVER_TIMEOUTS[g_saver_timeout])
            && g_usb_mounted) {
            g_screensaver  = true;
            saver_entering = true;
            saver_slide    = 0.0f;
            saver_zoom     = 0.2f;
            oled_cubesaver_reset();   /* 形状归位屏幕中心再登场 */
        }
    }

    /* 屏保被活动打断时复位过渡状态, 下次触发重新进场 */
    if (!g_screensaver) {
        saver_entering = false;
        saver_hold     = false;
    }

    /* 显示 */
    if (g_screensaver) {
        if (saver_entering) {
            /* 进场过渡: 屏保场景先画 (清屏) → 主屏叠画下落 (不清) → 一次推屏
             * 主屏占据行 off..32, 多边形在行 0..off 处被逐渐揭示
             * 缩放独立缓动: 主屏落过 1/3 后开始放大, 漂移暂停
             * 缩放目标定在 1.3 (超出满尺寸): 满尺寸在缓动中段即穿过,
             * 尾段爬行落在不可见的 1.0→1.3 区间 (同幕布目标屏外手法) */
            anim_ease(&saver_slide, 32.0f, 65.0f);
            if (saver_slide > 10.0f)
                anim_ease(&saver_zoom, 1.3f, 200.0f);
            int off = (int)saver_slide;
            float zoom = (saver_zoom > 1.0f) ? 1.0f : saver_zoom;

            oled_draw_cubesaver(false, zoom, false);
            if (g_usb_mounted)
                oled_show_keyboard(false, off, false);
            else
                oled_show_calculator(false, off, false);
            u8g2.sendBuffer();

            if (saver_slide > 31.5f && saver_zoom >= 1.0f) {
                saver_slide      = 0.0f;
                saver_entering   = false;
                saver_hold       = true;
                saver_hold_until = millis() + 500;  /* 定格展示再开始漂移 */
            }
        } else if (saver_hold) {
            /* 定格: 原地旋转展示, 不漂移 */
            oled_draw_cubesaver(true, 1.0f, false);
            if (millis() >= saver_hold_until) saver_hold = false;
        } else {
            oled_draw_cubesaver();
        }
    } else if (menu.animating) {
        float target = menu.entering ? 0.0f : 32.0f;
        anim_ease(&menu.slide_offset, target, 65.0f);  /* 入场/退场同速 */

        if (menu.entering) {
            /* 入场: 菜单从下方滑入 (纯菜单) */
            oled_show_menu();
            if (menu.slide_offset < 0.5f) {
                menu.slide_offset = 0.0f;
                menu.animating    = false;
            }
        } else {
            /* 退场: 主屏从顶部下落 (同开屏 logo 下落)
             * 主屏底边骑在菜单顶边: 整体位移 slide-32 → 0
             * 绘制顺序: 主屏(下落) → 菜单(下方, 未遮挡部分涂黑) */
            int line  = (int)menu.slide_offset;
            int y_off = line - 32;   /* -32 → 0 */
            if (g_usb_mounted)
                oled_show_keyboard(false, y_off);
            else
                oled_show_calculator(false, y_off);

            if (line < 32) {
                u8g2.setDrawColor(0);
                u8g2.drawBox(0, line, 128, 32 - line);
                u8g2.setDrawColor(1);
            }

            const MenuItem* items = menuPages[menu.active_page].items;
            int cnt = menuPages[menu.active_page].count;
            oled_draw_menu(items, cnt, menu.cursor,
                           menu.scroll_offset, false, false,
                           -1, menu.active_page);

            u8g2.sendBuffer();

            if (menu.slide_offset > 31.5f) {
                menu.slide_offset = 0.0f;
                menu.animating    = false;
                menu.is_open      = false;
            }
        }
    } else if (menu.is_open) {
        oled_show_menu();
    } else if (g_mode == MODE_KEYBOARD) {
        oled_show_keyboard();
    } else {
        oled_show_calculator();
    }

    /* 灯效 */
    run_led_effect();

    /* 逐键反馈: 消费 Core 0 信号 → 渐灭动画 */
    feedback_consume();
    update_key_feedback();

    /* DEL 键指示灯 */
    led_update_del_indicator();
}