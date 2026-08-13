#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <cmath>
#include <EEPROM.h>

/* ================================================================
 *  DWZ Deck v3.0 固件
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
#define MENU_TIMEOUT_MS  10000
#define KEY_FEEDBACK_MS  50

/* Flash 设置存储 */
#define SETTINGS_MAGIC  0xEDA1
#define EEPROM_SIZE     64

/* ================================================================
 *  SECTION 2: 枚举与结构体
 * ================================================================ */

/* 系统模式 */
enum AppMode {
    MODE_KEYBOARD,
    MODE_CALCULATOR,
    MODE_MENU
};

/* 按键层 */
enum KeyLayer {
    LAYER_NUMPAD,
    LAYER_FN
};

/* 灯效 */
enum LEDMode {
    LED_RAINBOW,
    LED_MARQUEE,
    LED_BREATHING,
    LED_METEOR,
    LED_KEY_FLASH,
    LED_MODE_COUNT
};

/* 菜单项类型 */
enum MenuItemType {
    MENU_ACTION,    /* 立即执行动作 */
    MENU_SUBMENU,   /* 进入子菜单 */
    MENU_SLIDER,    /* 旋转调节数值 */
    MENU_BACK       /* 返回上级 */
};

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
 *  SECTION 6: 菜单系统数据
 * ================================================================ */

/* 菜单 ID 枚举 */
enum MenuPageID {
    PAGE_MAIN,
    PAGE_LED,
    PAGE_LED_MODE,
    PAGE_LED_BRIGHTNESS,
    PAGE_LED_SPEED,
    PAGE_KEY_LAYER,
    PAGE_SYSINFO,
    PAGE_ANIM,
    PAGE_CUBE_MENU,
    PAGE_CUBE_SPEED,
    PAGE_SHAPE_PREVIEW,
    PAGE_LOGO_MENU,
    PAGE_LOGO_STYLE
};

/* 菜单项: 标签 + 类型 */
struct MenuItem {
    const char* label;
    MenuItemType type;
};

/* 各菜单页定义 */

const MenuItem menuMain[] = {
    {"LED",          MENU_SUBMENU},
    {"Animation",    MENU_SUBMENU},
    {"Key Layer",    MENU_SUBMENU},
    {"System Info",  MENU_SUBMENU},
    {"Back",         MENU_BACK},
};
#define MENU_MAIN_COUNT 5

const MenuItem menuLED[] = {
    {"Effect",       MENU_SUBMENU},
    {"Bright",       MENU_SUBMENU},
    {"Speed",        MENU_SUBMENU},
    {"Back",         MENU_BACK},
};
#define MENU_LED_COUNT 4

const MenuItem menuLEDMode[] = {
    {"Rainbow",    MENU_ACTION},
    {"Marquee",    MENU_ACTION},
    {"Breathing",  MENU_ACTION},
    {"Meteor",     MENU_ACTION},
    {"Flash",      MENU_ACTION},
    {"Back",       MENU_BACK},
};
#define MENU_LEDMODE_COUNT 6

const MenuItem menuKeyLayer[] = {
    {"Numpad Layer", MENU_ACTION},
    {"FN Layer",     MENU_ACTION},
    {"Back",         MENU_BACK},
};
#define MENU_LAYER_COUNT 3

const MenuItem menuAnim[] = {
    {"Screen Saver", MENU_SUBMENU},
    {"Startup Logo", MENU_SUBMENU},
    {"Back",         MENU_BACK},
};
#define MENU_ANIM_COUNT 3

const MenuItem menuLogo[] = {
    {"Replay",       MENU_ACTION},
    {"Style",        MENU_SUBMENU},
    {"Back",         MENU_BACK},
};
#define MENU_LOGO_COUNT 3

const MenuItem menuLogoStyle[] = {
    {"Meteor",       MENU_ACTION},
    {"Firework",     MENU_ACTION},
    {"Random",       MENU_ACTION},
    {"Back",         MENU_BACK},
};
#define MENU_LOGO_STYLE_COUNT 4

const MenuItem menuCube[] = {
    {"Speed",       MENU_SUBMENU},
    {"Shape",       MENU_SUBMENU},
    {"Back",        MENU_BACK},
};
#define MENU_CUBE_COUNT 3

const MenuItem menuSysInfo[] = {
    {"DWZ Deck v3.0",    MENU_ACTION},
    {"RP2350 RISC-V",    MENU_ACTION},
    {"Back",             MENU_BACK},
};
#define MENU_INFO_COUNT 3

/* 菜单页表 */
struct MenuPageDef {
    const MenuItem* items;
    int             count;
};

/* 导航栈最大深度: Main→LED→Effect 或 Main→Anim→Cube→预览 */
#define MENU_STACK_MAX 6
const MenuPageDef menuPages[] = {
    {menuMain,          MENU_MAIN_COUNT},      /* PAGE_MAIN */
    {menuLED,           MENU_LED_COUNT},       /* PAGE_LED */
    {menuLEDMode,       MENU_LEDMODE_COUNT},   /* PAGE_LED_MODE */
    {nullptr,           0},                    /* PAGE_LED_BRIGHTNESS */
    {nullptr,           0},                    /* PAGE_LED_SPEED */
    {menuKeyLayer,      MENU_LAYER_COUNT},     /* PAGE_KEY_LAYER */
    {menuSysInfo,       MENU_INFO_COUNT},      /* PAGE_SYSINFO */
    {menuAnim,          MENU_ANIM_COUNT},      /* PAGE_ANIM */
    {menuCube,          MENU_CUBE_COUNT},      /* PAGE_CUBE_MENU */
    {nullptr,           0},                    /* PAGE_CUBE_SPEED (弹窗) */
    {nullptr,           0},                    /* PAGE_SHAPE_PREVIEW */
    {menuLogo,          MENU_LOGO_COUNT},      /* PAGE_LOGO_MENU */
    {menuLogoStyle,     MENU_LOGO_STYLE_COUNT},/* PAGE_LOGO_STYLE */
};

/* 菜单运行时状态 */
struct MenuState {
    int     active_page;    /* 当前页 ID */
    int     cursor;         /* 光标位置 (0-based) */
    int     scroll_offset;  /* 滚动偏移 (列表顶部在数组中的位置) */
    bool    is_open;
    bool    slider_active;  /* 滑块模式: 旋转直接调值 */
    bool    popup_enter;    /* 弹窗入场动画进行中 */
    bool    popup_exit;     /* 弹窗退场动画进行中 */
    float   popup_offset;   /* 弹窗 Y 偏移 (32=隐藏, 0=完全显示) */
    float   cursor_y;       /* 光标条动画 Y 坐标 */
    float   selector_w;     /* 选择框动画宽度 (匹配文字长度) */
    float   camera_y;       /* 视口滚动动画偏移 (像素) */
    float   sbar_y;         /* 滚动条滑块动画 Y */
    float   sbar_h;         /* 滚动条滑块动画高度 */
    bool    animating;      /* 菜单退场/入场动画进行中 */
    float   slide_offset;   /* 滑出偏移: 退出 0→32 入场 32→0 */
    bool    entering;       /* true=入场 false=退场 */
    int     stack_page[MENU_STACK_MAX];   /* 导航栈: 进入子菜单时压栈 */
    int     stack_cursor[MENU_STACK_MAX];
    int     stack_depth;
    int     popup_ret_page;     /* 弹窗打开前的页 ID (退出弹窗返回) */
    int     popup_ret_cursor;   /* 弹窗打开前的光标位置 */
    int     preview_saved_mode; /* 进预览页时快照的已保存形状 */
    unsigned long last_activity_ms;
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
volatile int           g_cube_mode    = 0;   /* 0=Cube 1=Tetra 2=Octa 3=Icosa 4=Random */
volatile int           g_logo_style   = 0;   /* 0=流星雨 1=烟花 2=随机 */
volatile bool          g_replay_logo  = false;

/* Flash 存储脏标志 */
volatile bool        settings_dirty      = false;
unsigned long        settings_dirty_time = 0;

/* 菜单状态 (仅 Core 1 操作) */
MenuState menu;

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
 *  SECTION 8: OLED 辅助函数
 * ================================================================ */

/* U8g2 字体: 5x8 适合 4 行布局, 9x15 用于大号数字 */
#define FONT_SMALL  u8g2_font_5x8_tf
#define FONT_LARGE  u8g2_font_9x15_tf

/* 缓动函数: 先快后慢, 接近目标时减速 */
static void anim_ease(float *a, float target, float speed)
{
    if (*a == target) return;
    float diff = target - *a;
    if (fabs(diff) < 0.12f) {
        *a = target;
        return;
    }
    *a += diff / (speed / 10.0f);
}

/* 编码器加速: 根据旋转间隔动态调整步长
 * 连续快速旋转 (>3次/300ms) 步长从1逐步增大到5 */
static int accel_step(unsigned long now_ms)
{
    static unsigned long last_t = 0;
    static int           count  = 0;

    if (now_ms - last_t < 120) {
        count++;
        /* 每积累 4 次快速脉冲 → 步长 +1 */
        if (count >= 4 && count < 8)  return 2;
        if (count >= 8 && count < 12) return 3;
        if (count >= 12)              return 5;
    } else {
        /* 间隔太久, 重置 */
        count = 1;
    }
    last_t = now_ms;
    return 1;
}

/* 弹窗动画重置 (进入弹窗时调用) */
static void popup_anim_start()
{
    menu.popup_enter  = true;
    menu.popup_exit   = false;
    menu.popup_offset = 32.0f;  /* 从屏幕底部开始滑入 */
}

/* 弹窗退场 (确认/返回时调用, 退场后恢复 popup_ret_page/cursor) */
static void popup_anim_exit()
{
    menu.popup_enter  = false;
    menu.popup_exit   = true;
    /* popup_offset 从当前值开始往 32 动画 */
}

/* 每帧更新弹窗动画, 返回 true 表示动画进行中 */
static bool popup_anim_update()
{
    if (menu.popup_enter) {
        anim_ease(&menu.popup_offset, 0.0f, 40.0f);
        if (menu.popup_offset < 0.05f) {
            menu.popup_offset = 0.0f;
            menu.popup_enter  = false;
        }
        return true;
    }
    if (menu.popup_exit) {
        anim_ease(&menu.popup_offset, 32.0f, 50.0f);
        if (menu.popup_offset > 31.5f) {
            int cur    = menu.popup_ret_cursor;
            int scroll = (cur >= 4) ? (cur - 3) : 0;
            menu.popup_offset  = 32.0f;
            menu.popup_exit    = false;
            menu.slider_active = false;
            menu.active_page   = menu.popup_ret_page;
            menu.cursor        = cur;
            menu.cursor_y      = (cur - scroll) * 8.0f;
            menu.camera_y      = scroll * 8.0f;
            menu.selector_w    = 120.0f;
            menu.sbar_y        = 2.0f;
            menu.sbar_h        = 0.0f;
            menu.scroll_offset = scroll;
            return false;  /* 动画结束 */
        }
        return true;
    }
    return false;
}

/* 屏保: 旋转 wireframe 多面体 — 多轴旋转 + 弹跳 */
void oled_draw_cubesaver()
{
    static float ay = 0.0f, ax = 0.3f;
    static float cx = 64.0f, cy = 16.0f;
    static float vx = 0.18f, vy = 0.12f;
    const int   sz  = 9;             /* 与预览一致 */
    const int   pad = 16;            /* sz*1.73≈15.6, 安全边距 */

    /* 随机模式: 每次屏保触发时抽形, 播放中每 6s 换到另一随机形状
     * 用 millis 计时 (loop1 无固定帧率, 帧计数不准)
     * 调用间隔 >1s 视为新一轮屏保 */
    static int   saver_mode = -1;
    static unsigned long last_ms  = 0;   /* 上一帧时刻 (进入判定) */
    static unsigned long morph_ms = 0;   /* 下次换形时刻 */
    int shape = g_cube_mode;
    if (shape == 4) {
        unsigned long now = millis();
        if (now - last_ms > 1000) {
            saver_mode = random(0, 4);
            morph_ms   = now;
        }
        last_ms = now;
        if (now - morph_ms >= 6000) {
            morph_ms   = now;
            saver_mode  = (saver_mode + 1 + random(0, 3)) % 4;  /* 必换形 */
        }
        shape = saver_mode;
    } else {
        saver_mode  = -1;
        last_ms     = 0;
    }

    /* 顶点/边数据 (同预览) */
    const int8_t tet_v[4][3] = {{1,1,1},{1,-1,-1},{-1,1,-1},{-1,-1,1}};
    const uint8_t tet_e[6][2] = {{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
    const int8_t oct_v[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const uint8_t oct_e[12][2] = {{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},
                                   {1,4},{1,5},{2,4},{2,5},{3,4},{3,5}};
    const float gr = 1.618f;
    const float ico_raw[12][3] = {
        {0,1,gr},{0,1,-gr},{0,-1,gr},{0,-1,-gr},
        {1,gr,0},{1,-gr,0},{-1,gr,0},{-1,-gr,0},
        {gr,0,1},{gr,0,-1},{-gr,0,1},{-gr,0,-1}};
    const uint8_t ico_e[30][2] = {
        {0,2},{0,4},{0,6},{0,8},{0,10},{1,3},{1,4},{1,6},
        {1,9},{1,11},{2,5},{2,7},{2,8},{2,10},{3,5},{3,7},
        {3,9},{3,11},{4,6},{4,8},{4,9},{5,7},{5,8},{5,9},
        {6,10},{6,11},{7,10},{7,11},{8,9},{10,11}};

    const int nv = (shape==0)?8:(shape==1)?4:(shape==2)?6:12;
    const int ne = (shape==0)?12:(shape==1)?6:(shape==2)?12:30;

    float scale = (shape == 2 || shape == 3) ? 1.73f : 1.0f;

    float cya = cos(ay), sya = sin(ay);
    float cxa = cos(ax), sxa = sin(ax);

    int sx[12], sy[12];
    for (int i = 0; i < nv; i++) {
        float vx, vy, vz;
        if (shape == 0) {
            const int8_t cv[8][3] = {{-1,-1,-1},{-1,-1,1},{-1,1,-1},{-1,1,1},
                                     {1,-1,-1},{1,-1,1},{1,1,-1},{1,1,1}};
            vx=cv[i][0]; vy=cv[i][1]; vz=cv[i][2];
        } else if (shape == 1) { vx=tet_v[i][0]; vy=tet_v[i][1]; vz=tet_v[i][2]; }
        else if (shape == 2) { vx=oct_v[i][0]; vy=oct_v[i][1]; vz=oct_v[i][2]; }
        else {
            vx=ico_raw[i][0]; vy=ico_raw[i][1]; vz=ico_raw[i][2];
            float len = sqrt(vx*vx+vy*vy+vz*vz);
            vx/=len; vy/=len; vz/=len;
        }
        float rx = vx * cya + vz * sya;
        float rz = -vx * sya + vz * cya;
        float ty = vy * cxa - rz * sxa;
        sx[i] = (int)cx + (int)(rx * sz * scale);
        sy[i] = (int)cy - (int)(ty * sz * scale);
    }

    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    for (int e = 0; e < ne; e++) {
        int a, b;
        if (shape == 0) {
            const uint8_t ed[12][2] = {{0,1},{2,3},{0,2},{1,3},{4,5},{6,7},
                                       {4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
            a=ed[e][0]; b=ed[e][1];
        } else if (shape == 1) { a=tet_e[e][0]; b=tet_e[e][1]; }
        else if (shape == 2) { a=oct_e[e][0]; b=oct_e[e][1]; }
        else { a=ico_e[e][0]; b=ico_e[e][1]; }
        u8g2.drawLine(sx[a], sy[a], sx[b], sy[b]);
    }
    u8g2.sendBuffer();

    /* 速度: 0-100% → 每帧 0.01~0.06 弧度 */
    float spd = map(g_cube_speed, 0, 100, 40, 8) / 1000.0f;
    ay += spd;
    ax += spd * 0.37f;  /* X 轴慢 ~2.7 倍, 产生晃动摇摆感 */
    if (ay > TWO_PI) ay -= TWO_PI;
    if (ax > TWO_PI) ax -= TWO_PI;

    /* 弹跳漂移: 碰到边缘反弹 */
    cx += vx;
    cy += vy;
    if (cx < pad)       { cx = pad;       vx = -vx; }
    if (cx > 128 - pad) { cx = 128 - pad; vx = -vx; }
    if (cy < pad)       { cy = pad;       vy = -vy; }
    if (cy > 32 - pad)  { cy = 32 - pad;  vy = -vy; }
}

/* 形状预览: 居中旋转多面体 + 名称 + <> 指示 */
void oled_draw_shape_preview()
{
    static float ay = 0.0f, ax = 0.2f;
    const int   cx  = 64, cy = 18, sz = 9;    /* 适中大小 */

    const char* names[] = {"Cube","Tetra","Octa","Icosa","Random"};

    /* Random 项演示: 每 2s 自动换形; 浏览离开后下次进入重新随机
     * 用 millis 计时 (渲染循环无固定帧率) */
    static int  demo_mode = -1;
    static unsigned long demo_ms = 0;   /* 下次换形时刻 */
    int shape = g_cube_mode;
    if (shape == 4) {
        if (demo_mode < 0) {
            demo_mode = random(0, 4);
            demo_ms   = millis();
        }
        if (millis() - demo_ms >= 2000) {
            demo_ms   = millis();
            demo_mode = (demo_mode + 1 + random(0, 3)) % 4;  /* 必换形 */
        }
        shape = demo_mode;
    } else {
        demo_mode = -1;
    }

    int nv, ne;
    float cya = cos(ay), sya = sin(ay);
    float cxa = cos(ax), sxa = sin(ax);
    int sx[12], sy[12];

    const int8_t tet_v[4][3] = {{1,1,1},{1,-1,-1},{-1,1,-1},{-1,-1,1}};
    const uint8_t tet_e[6][2] = {{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
    const int8_t oct_v[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const uint8_t oct_e[12][2] = {{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},
                                   {1,4},{1,5},{2,4},{2,5},{3,4},{3,5}};
    const float gr = 1.618f;
    const float ico_raw[12][3] = {
        {0,1,gr},{0,1,-gr},{0,-1,gr},{0,-1,-gr},
        {1,gr,0},{1,-gr,0},{-1,gr,0},{-1,-gr,0},
        {gr,0,1},{gr,0,-1},{-gr,0,1},{-gr,0,-1}};
    const uint8_t ico_e[30][2] = {
        {0,2},{0,4},{0,6},{0,8},{0,10},{1,3},{1,4},{1,6},
        {1,9},{1,11},{2,5},{2,7},{2,8},{2,10},{3,5},{3,7},
        {3,9},{3,11},{4,6},{4,8},{4,9},{5,7},{5,8},{5,9},
        {6,10},{6,11},{7,10},{7,11},{8,9},{10,11}};

    nv = (shape == 0) ? 8 : (shape == 1) ? 4 : (shape == 2) ? 6 : 12;
    ne = (shape == 0) ? 12 : (shape == 1) ? 6 : (shape == 2) ? 12 : 30;

    float scale = (shape == 2 || shape == 3) ? 1.73f : 1.0f;

    for (int i = 0; i < nv; i++) {
        float vx, vy, vz;
        if (shape == 0) {
            const int8_t cv[8][3] = {{-1,-1,-1},{-1,-1,1},{-1,1,-1},{-1,1,1},
                                     {1,-1,-1},{1,-1,1},{1,1,-1},{1,1,1}};
            vx=cv[i][0]; vy=cv[i][1]; vz=cv[i][2];
        } else if (shape == 1) { vx=tet_v[i][0]; vy=tet_v[i][1]; vz=tet_v[i][2]; }
        else if (shape == 2) { vx=oct_v[i][0]; vy=oct_v[i][1]; vz=oct_v[i][2]; }
        else {
            vx=ico_raw[i][0]; vy=ico_raw[i][1]; vz=ico_raw[i][2];
            float len = sqrt(vx*vx+vy*vy+vz*vz);
            vx/=len; vy/=len; vz/=len;
        }
        float rx = vx * cya + vz * sya;
        float rz = -vx * sya + vz * cya;
        float ty = vy * cxa - rz * sxa;
        sx[i] = (int)cx + (int)(rx * sz * scale);
        sy[i] = (int)cy - (int)(ty * sz * scale);
    }

    u8g2.clearBuffer();
    u8g2.setFont(FONT_SMALL);
    u8g2.setDrawColor(1);

    /* 左上角: 形状名; 浏览到"已保存形状"时才画 ● (快照语义) */
    u8g2.setCursor(0, 7);
    u8g2.print(names[g_cube_mode]);
    if (g_cube_mode == menu.preview_saved_mode) {
        int nw = u8g2.getStrWidth(names[g_cube_mode]);
        u8g2.drawDisc(nw + 5, 4, 2);
    }

    /* 左箭头 */
    u8g2.setCursor(18, 22);
    u8g2.print("<");
    /* 右箭头 */
    u8g2.setCursor(106, 22);
    u8g2.print(">");
    for (int e = 0; e < ne; e++) {
        int a, b;
        if (shape == 0) {
            const uint8_t ed[12][2] = {{0,1},{2,3},{0,2},{1,3},{4,5},{6,7},
                                       {4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
            a=ed[e][0]; b=ed[e][1];
        } else if (shape == 1) { a=tet_e[e][0]; b=tet_e[e][1]; }
        else if (shape == 2) { a=oct_e[e][0]; b=oct_e[e][1]; }
        else { a=ico_e[e][0]; b=ico_e[e][1]; }
        u8g2.drawLine(sx[a], sy[a], sx[b], sy[b]);
    }

    u8g2.sendBuffer();

    ay += 0.025f; if (ay > TWO_PI) ay -= TWO_PI;
    ax += 0.008f; if (ax > TWO_PI) ax -= TWO_PI;
}

/* 绘制菜单: 平滑滚屏 + 选择框宽度动画 + 右侧滚动条
 * @param marked 当前选中项下标 (左侧 ">" 前缀), -1=无
 * @param show  false=仅写 buffer 不推屏, 供叠层绘制用 */
void oled_draw_menu(const MenuItem* items, int count,
                    int cursor, int scroll,
                    bool show = true, bool clear_first = true,
                    int marked = -1)
{
    if (clear_first) u8g2.clearBuffer();
    u8g2.setFont(FONT_SMALL);

    /* 视口目标偏移 = scroll * 8 */
    float cam_target = scroll * 8.0f;
    anim_ease(&menu.camera_y, cam_target, 45.0f);

    /* 光标目标 Y = cursor * 8 - camera_y */
    float target_y = cursor * 8.0f - menu.camera_y;
    anim_ease(&menu.cursor_y, target_y, 35.0f);

    /* 选择框目标宽度 = 精确文字宽 + margin */
    float target_w = u8g2.getStrWidth(items[cursor].label) + 14.0f;
    if (target_w < 36.0f) target_w = 36.0f;
    if (target_w > 122.0f) target_w = 122.0f;
    anim_ease(&menu.selector_w, target_w, 50.0f);

    /* 入场/退场滑动偏移 */
    float slide = menu.animating ? menu.slide_offset : 0.0f;

    /* ---- 绘制列表项 (视口偏移 + 滑动偏移) ---- */
    for (int i = 0; i < count; i++) {
        float orig_y = i * 8.0f - menu.camera_y;
        float yf     = orig_y + slide;

        /* 裁剪: 原始位置不在视口内的项不画 (防止退场时上方项滑入) */
        if (orig_y < -1.0f || orig_y > 31.0f) continue;
        /* 位移后完全在视口外也跳过 */
        if (yf < -7.0f || yf > 31.0f) continue;

        int y_top = (int)yf;
        int cy = y_top + 7;   /* baseline for 5x8 font */

        if (i == cursor) {
            /* 圆角选择框 + 反白文字 */
            float bar_y = menu.cursor_y + slide;
            u8g2.setDrawColor(1);
            u8g2.drawRBox(0, (int)bar_y, (int)menu.selector_w, 8, 2);
            u8g2.setDrawColor(0);
        } else {
            u8g2.setDrawColor(1);
        }

        /* 当前选中项: 左侧 ">" 前缀 (x=6), 文字后移留 2px 空隙;
         * 其余行文字保持 x=6, 不为标记预留空列 */
        int tx = (i == marked) ? 13 : 6;
        if (i == marked) {
            u8g2.setCursor(6, cy);
            u8g2.print(">");
        }
        u8g2.setCursor(tx, cy);
        u8g2.print(items[i].label);
    }

    /* ---- 右侧滚动条 (仅当 count > 4 时显示) ---- */
    if (count > 4) {
        const int trk_x  = 124;
        const int trk_w  = 3;
        const int trk_t  = 2;
        const int trk_b  = 30;
        const int trk_h  = trk_b - trk_t;  /* 28px */

        /* 滑块目标 */
        float sbar_target_h = (float)trk_h * 4.0f / (float)count;
        if (sbar_target_h < 6.0f) sbar_target_h = 6.0f;

        float sbar_target_y = trk_t
            + (float)(trk_h - (int)sbar_target_h)
            * (float)cursor / (float)(count - 1);

        /* 首帧跳过动画: 若未初始化则直接定位 */
        if (menu.sbar_h < 1.0f) {
            menu.sbar_h = sbar_target_h;
            menu.sbar_y = sbar_target_y;
        } else {
            anim_ease(&menu.sbar_h, sbar_target_h, 55.0f);
            anim_ease(&menu.sbar_y, sbar_target_y, 55.0f);
        }

        u8g2.setDrawColor(1);
        u8g2.drawVLine(trk_x + 1, trk_t, trk_h);
        u8g2.drawBox(trk_x, (int)menu.sbar_y, trk_w, (int)menu.sbar_h);
    } else {
        menu.sbar_h = 0.0f;  /* ≤4 项时隐藏 */
    }

    if (show) u8g2.sendBuffer();
}

/* 弹窗式数值调节器 — 从底部滑入的卡片, 统一处理亮度和速度
 * @param show false=仅写 buffer 不推屏, 供叠层绘制用 */
void oled_draw_popup(const char* title, int value,
                     bool clear_first = true, bool show = true)
{
    float y0 = menu.popup_offset;

    if (clear_first) u8g2.clearBuffer();

    int card_h = 32 - (int)y0;
    if (card_h > 0) {
        /* 标题 (baseline = y0 + 7, 5x8 font) */
        if (card_h > 4) {
            u8g2.setFont(FONT_SMALL);
            u8g2.setDrawColor(1);
            u8g2.setCursor(6, (int)y0 + 7);
            u8g2.print(title);
        }

        /* 大数字 (baseline = y0 + 22, 9x15 font) */
        if (card_h > 18) {
            u8g2.setFont(FONT_LARGE);
            char val_buf[8];
            snprintf(val_buf, sizeof(val_buf), "%d%%", value);
            int cx = (128 - (int)u8g2.getStrWidth(val_buf)) / 2;
            if (cx < 0) cx = 0;
            u8g2.setCursor(cx, (int)y0 + 22);
            u8g2.print(val_buf);
        }

        /* 进度条 (圆角) */
        if (card_h > 28) {
            int bar_w = map(value, 0, 100, 0, 114);
            u8g2.drawRFrame(4, (int)y0 + 27, 114, 4, 2);
            if (bar_w > 3)
                u8g2.drawRBox(5, (int)y0 + 28, bar_w - 2, 2, 1);
        }
    }

    if (show) u8g2.sendBuffer();
}

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
 *  SECTION 10: 菜单逻辑
 * ================================================================ */

/* 前向声明 (定义在 Section 11.5) */
static void settings_mark_dirty();

/* 进入子菜单前压栈: 记录当前页和光标 */
static void menu_push(int page, int cursor)
{
    if (menu.stack_depth >= MENU_STACK_MAX) return;
    menu.stack_page[menu.stack_depth]   = page;
    menu.stack_cursor[menu.stack_depth] = cursor;
    menu.stack_depth++;
}

/* 返回上一级: 弹栈恢复父页, 返回 false 表示栈已空 */
static bool menu_pop()
{
    if (menu.stack_depth <= 0) return false;
    menu.stack_depth--;
    int prev   = menu.stack_cursor[menu.stack_depth];
    int scroll = (prev >= 4) ? (prev - 3) : 0;
    menu.active_page   = menu.stack_page[menu.stack_depth];
    menu.cursor        = prev;
    menu.cursor_y      = (prev - scroll) * 8.0f;
    menu.camera_y      = scroll * 8.0f;
    menu.selector_w    = 120.0f;
    menu.sbar_y        = 2.0f;
    menu.sbar_h        = 0.0f;
    menu.scroll_offset = scroll;
    return true;
}

void menu_open()
{
    menu.active_page    = PAGE_MAIN;
    menu.cursor         = 0;
    menu.scroll_offset  = 0;
    menu.stack_depth    = 0;
    menu.popup_ret_page = PAGE_MAIN;
    menu.popup_ret_cursor = 0;
    menu.is_open        = true;
    menu.slider_active  = false;
    menu.popup_enter    = false;
    menu.popup_exit     = false;
    menu.popup_offset   = 32.0f;
    menu.cursor_y       = 0.0f;
    menu.selector_w     = 120.0f;
    menu.camera_y       = 0.0f;
    menu.sbar_y         = 2.0f;
    menu.sbar_h         = 0.0f;
    /* 入场动画: 菜单从下方滑入 */
    menu.animating     = true;
    menu.entering      = true;
    menu.slide_offset  = 32.0f;
    menu.last_activity_ms = millis();
}

void menu_close()
{
    if (menu.animating) return;
    /* 退场动画: 菜单下滑消失 (强制切回主菜单页避免 nullptr) */
    menu.active_page   = PAGE_MAIN;
    menu.cursor        = 0;
    menu.scroll_offset = 0;
    menu.stack_depth   = 0;
    menu.animating     = true;
    menu.entering      = false;
    menu.slide_offset  = 0.0f;
    menu.slider_active = false;
    menu.popup_enter   = false;
    menu.popup_exit    = false;
}

void menu_navigate(int delta)
{
    if (!menu.is_open) return;

    /* 形状预览页: 编码器旋转直接切换形状 */
    if (menu.active_page == PAGE_SHAPE_PREVIEW) {
        g_cube_mode = (g_cube_mode + delta + 5) % 5;
        menu.last_activity_ms = millis();
        return;
    }

    const MenuItem* items = menuPages[menu.active_page].items;
    int count = menuPages[menu.active_page].count;

    if (menu.slider_active) {
        /* 弹窗滑块模式: 旋转调节, 带加速 */
        if (menu.popup_enter || menu.popup_exit) return;

        int step = accel_step(millis());

        if (menu.active_page == PAGE_LED_SPEED) {
            int val = g_led_speed + delta * step;
            if (val < 0)   val = 0;
            if (val > 100) val = 100;
            g_led_speed = val;
        } else if (menu.active_page == PAGE_CUBE_SPEED) {
            int val = g_cube_speed + delta * step;
            if (val < 0)   val = 0;
            if (val > 100) val = 100;
            g_cube_speed = val;
        } else {
            int val = g_brightness + delta * step;
            if (val < 0)   val = 0;
            if (val > 100) val = 100;
            g_brightness = val;
        }
        settings_mark_dirty();
        menu.last_activity_ms = millis();
        return;
    }

    menu.cursor += delta;
    if (menu.cursor < 0) menu.cursor = count - 1;
    if (menu.cursor >= count) menu.cursor = 0;

    /* 自动滚屏 */
    if (menu.cursor < menu.scroll_offset)
        menu.scroll_offset = menu.cursor;
    if (menu.cursor >= menu.scroll_offset + 4)
        menu.scroll_offset = menu.cursor - 3;

    menu.last_activity_ms = millis();
}

void menu_select()
{
    if (!menu.is_open) return;

    /* 形状预览页: 确定→弹栈回 Cube 子菜单 */
    if (menu.active_page == PAGE_SHAPE_PREVIEW) {
        settings_mark_dirty();
        menu_pop();
        return;
    }

    /* 弹窗模式下特殊处理 (items 可能为 nullptr) */
    if (menu.slider_active) {
        settings_mark_dirty();
        popup_anim_exit();
        menu.last_activity_ms = millis();
        return;
    }

    const MenuItem* items = menuPages[menu.active_page].items;
    int count = menuPages[menu.active_page].count;
    const MenuItem* sel = &items[menu.cursor];

    menu.last_activity_ms = millis();

    switch (sel->type) {
        case MENU_SUBMENU: {
            if (menu.active_page == PAGE_LED
                && (menu.cursor == 1 || menu.cursor == 2)) {
                /* LED Bright / Speed: 弹窗式滑块
                 * 返回位置存 popup_ret, 不动 saved 父页链 */
                menu.popup_ret_page   = PAGE_LED;
                menu.popup_ret_cursor = menu.cursor;
                if (menu.cursor == 1)
                    menu.active_page = PAGE_LED_BRIGHTNESS;
                else
                    menu.active_page = PAGE_LED_SPEED;
                menu.cursor        = 0;
                menu.scroll_offset = 0;
                menu.slider_active = true;
                popup_anim_start();
            } else if (menu.active_page == PAGE_CUBE_MENU
                       && menu.cursor == 1) {
                /* Shape → 预览页 (快照当前已保存形状, 供 ● 标识) */
                menu_push(PAGE_CUBE_MENU, 1);
                menu.active_page        = PAGE_SHAPE_PREVIEW;
                menu.cursor             = 0;
                menu.scroll_offset      = 0;
                menu.preview_saved_mode = g_cube_mode;
            } else if (menu.active_page == PAGE_CUBE_MENU
                       && menu.cursor == 0) {
                /* Cube Speed: 弹窗 (不压栈, 返回位置单独记录) */
                menu.popup_ret_page   = PAGE_CUBE_MENU;
                menu.popup_ret_cursor = 0;
                menu.active_page   = PAGE_CUBE_SPEED;
                menu.cursor        = 0;
                menu.scroll_offset = 0;
                menu.slider_active = true;
                popup_anim_start();
            } else {
                /* 映射到子菜单页, 光标定位到当前值 */
                int target = -1;
                int init_cursor = 0;
                if (menu.active_page == PAGE_MAIN) {
                    if (menu.cursor == 0) {
                        target = PAGE_LED;
                    }
                    else if (menu.cursor == 1) {
                        target = PAGE_ANIM;
                    }
                    else if (menu.cursor == 2) {
                        target = PAGE_KEY_LAYER;
                        init_cursor = (int)g_layer;
                    }
                    else if (menu.cursor == 3) {
                        target = PAGE_SYSINFO;
                    }
                } else if (menu.active_page == PAGE_LED
                           && menu.cursor == 0) {
                    /* Effect 子菜单, 光标定位到当前灯效 */
                    target = PAGE_LED_MODE;
                    init_cursor = (int)g_led_mode;
                } else if (menu.active_page == PAGE_ANIM) {
                    if (menu.cursor == 0) {
                        target = PAGE_CUBE_MENU;
                    } else if (menu.cursor == 1) {
                        target = PAGE_LOGO_MENU;
                    }
                } else if (menu.active_page == PAGE_LOGO_MENU
                           && menu.cursor == 1) {
                    /* Style 子菜单, 光标定位到当前风格 */
                    target = PAGE_LOGO_STYLE;
                    init_cursor = g_logo_style;
                }
                if (target >= 0) {
                    menu_push(menu.active_page, menu.cursor);
                    int scroll = (init_cursor >= 4) ? (init_cursor - 3) : 0;
                    menu.active_page   = target;
                    menu.cursor        = init_cursor;
                    menu.cursor_y      = (init_cursor - scroll) * 8.0f;
                    menu.camera_y      = scroll * 8.0f;
                    menu.selector_w    = 120.0f;
                    menu.sbar_y        = 2.0f;
                    menu.sbar_h        = 0.0f;
                    menu.scroll_offset = scroll;
                }
            }
            break;
        }
        case MENU_BACK: {
            if (menu.active_page == PAGE_MAIN) {
                menu_close();
            } else {
                menu_pop();
            }
            break;
        }
        case MENU_ACTION: {
            /* 处理具体动作, 选完弹栈退回父页 */
            bool acted = false;
            if (menu.active_page == PAGE_LED_MODE) {
                if (menu.cursor < LED_MODE_COUNT) {
                    g_led_mode = (LEDMode)menu.cursor;
                    settings_mark_dirty();
                    acted = true;
                }
            }
            else if (menu.active_page == PAGE_KEY_LAYER) {
                g_layer = (menu.cursor == 0) ? LAYER_NUMPAD : LAYER_FN;
                settings_mark_dirty();
                acted = true;
            }
            else if (menu.active_page == PAGE_LOGO_MENU
                     && menu.cursor == 0) {
                /* Replay Logo: 触发动画重放, 播完回到本页 */
                g_replay_logo = true;
            }
            else if (menu.active_page == PAGE_LOGO_STYLE
                     && menu.cursor <= 2) {
                /* 选择开屏动画风格, 选完弹栈回 Logo 菜单 */
                g_logo_style = menu.cursor;
                settings_mark_dirty();
                acted = true;
            }
            if (acted) menu_pop();
            break;
        }
        case MENU_SLIDER: {
            if (menu.slider_active) {
                /* 已在弹窗中: 短按确认, 退场动画 → 恢复父页 */
                settings_mark_dirty();
                popup_anim_exit();
                menu.last_activity_ms = millis();
            } else {
                menu.slider_active = true;
                popup_anim_start();
            }
            break;
        }
    }
}

void menu_back()
{
    if (!menu.is_open) return;

    if (menu.slider_active) {
        /* 弹窗退场动画 → 恢复父页 */
        popup_anim_exit();
        menu.last_activity_ms = millis();
        return;
    }

    menu_select();  /* 复用 select 的 back 逻辑 */
}

/* 菜单超时自动退出 */
void menu_check_timeout()
{
    if (!menu.is_open || menu.animating) return;
    if (millis() - menu.last_activity_ms > MENU_TIMEOUT_MS) {
        menu_close();
    }
}

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
        g_cube_mode  = (s.cube_mode  <= 4)   ? s.cube_mode  : 0;
        g_logo_style = (s.logo_style <= 2)   ? s.logo_style : 0;
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

    EEPROM.put(0, s);
    EEPROM.commit();
}

static void settings_mark_dirty()
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

/* 开屏 Logo 动画 — 流星雨/烟花 + 黑幕滑入揭示标题
 * anim_ease speed 须 >= 35, 否则 diff/(speed/10) 震荡瞬移
 * 时间线: 两种风格均动态 — 动画散尽后开始揭示段
 *   (流星: 发射预算用完+全部出屏 / 烟花: 打满发数+粒子散尽)
 *   揭示段 = LOGO_REVEAL_FRAMES 帧 (黑幕+标题+版本+定格) */
#define LOGO_STARS   6
#define LOGO_FRAMES  100
#define LOGO_DELAY   33   /* ms/帧 */
#define METEOR_SLOPE 0.55f   /* dx/sv, 所有流星同向 (斜向左下, ~29°) */
#define METEOR_SPAWN_TARGET 36   /* 流星总发射数, 打完后逐一消散 */

#define LOGO_REVEAL_FRAMES  57   /* 揭示段总帧数 */
#define LOGO_REVEAL_TITLE   10   /* 揭示后标题启动帧 */
#define LOGO_REVEAL_VER     20   /* 揭示后版本号启动帧 */

#define FW_ROCKETS        3    /* 轮发火箭数 */
#define FW_PARTICLES      40   /* 烟花粒子池 (容纳 3 次爆炸并存) */
#define FW_BURSTS_TARGET  6    /* 烟花爆炸发数, 打完粒子散尽后揭示 */

static void oled_logo_animation()
{
    float sx[LOGO_STARS], sy[LOGO_STARS], sv[LOGO_STARS];   /* 流星位置+速度 */

    /* 烟花: 3 枚火箭错峰轮发 + 粒子池 */
    float   fw_rx[FW_ROCKETS], fw_ry[FW_ROCKETS], fw_rvy[FW_ROCKETS];
    int     fw_burst_y[FW_ROCKETS], fw_wait[FW_ROCKETS];
    int     fw_bursts = 0;   /* 已爆炸发数 */
    int     fw_flash_x = 0, fw_flash_y = 0, fw_flash_t = 0;
    float   fw_px[FW_PARTICLES], fw_py[FW_PARTICLES];
    float   fw_pvx[FW_PARTICLES], fw_pvy[FW_PARTICLES];
    uint8_t fw_pl[FW_PARTICLES];   /* 剩余寿命, 0=空闲 */

    float curtain = -34.0f;   /* 黑幕顶部 y: -34 → 0 */
    float title_y = -16.0f;   /* 标题 baseline y: -16 → 21 (居中) */
    float ver_x   = 130.0f;   /* 版本号 x: 130 → 100 (屏外缓动滑入) */

    /* 揭示段启动帧: 两种风格均动态 (动画散尽后触发) */
    int reveal_at    = -1;
    int total_frames = LOGO_FRAMES + 80;   /* 兜底上限 */
    int meteor_spawns = LOGO_STARS;        /* 发射计数 (初始 6 颗) */

    randomSeed(millis());

    /* 随机风格: 每次播放抽一种 (重播也会重新抽) */
    int style = (g_logo_style == 2) ? random(0, 2) : g_logo_style;

    for (int i = 0; i < LOGO_STARS; i++) {
        sx[i] = (float)random(8, 124);
        sy[i] = -4.0f - random(0, 12);   /* 散布在上方, 快速进场 */
        sv[i] = 4.5f + (float)random(0, 100) * 0.02f;  /* 4.5~6.5 px/帧 */
    }
    for (int i = 0; i < FW_PARTICLES; i++) fw_pl[i] = 0;
    for (int r = 0; r < FW_ROCKETS; r++) {
        fw_ry[r]   = 40.0f;       /* 全部待发 */
        fw_wait[r] = r * 13;      /* 错峰发射: 0/13/26 帧 */
    }

    for (int f = 0; f < total_frames; f++) {
        u8g2.clearBuffer();
        u8g2.setDrawColor(1);

        if (style == 0) {
        /* --- 流星雨 (同向左下斜落, 出屏后重生) ---
         *  头部: 3px 十字
         *  尾迹: 沿运动反方向的 1px 点, 间距逐段加大模拟消散
         *       ·
         *          ·      ← 尾迹点 (右上, 对齐运动方向)
         *     ·
         *   ·
         *  ***               ← 头部
         */
        for (int i = 0; i < LOGO_STARS; i++) {
            int x = (int)sx[i], y = (int)sy[i];
            /* 尾迹: 点数随速度增长 (快流星拖长尾)
             * 水平偏移 = METEOR_SLOPE × 垂直距离 (所有流星同角度) */
            int dots = 3 + (int)(sv[i] * 1.5f);
            if (dots > 8) dots = 8;

            /* 消散: 头部接近底部/左缘时, 尾迹从尾端逐段回收 */
            int fade = 0;
            if (y > 26) fade = (y - 26) / 3;
            if (sx[i] < 12.0f) {
                int xf = (int)((12.0f - sx[i]) / 3.0f);
                if (xf > fade) fade = xf;
            }
            dots -= fade;
            if (dots < 0) dots = 0;

            int t = 2;
            for (int k = 0; k < dots; k++) {
                int ty = y - t;
                int tx = x + (int)(METEOR_SLOPE * (float)t + 0.5f);
                if (tx >= 0 && tx < 128 && ty >= 0 && ty < 32) {
                    u8g2.drawPixel(tx, ty);
                }
                t += 2 + k;   /* 间距 2,3,4,... 递增 */
            }
            /* 头部: 3px 十字 */
            if (x >= 0 && x < 128 && y >= 0 && y < 32) {
                u8g2.drawHLine(x - 1, y, 3);
                u8g2.drawVLine(x, y - 1, 3);
            }
            /* 同向斜落; 出屏后按预算重生, 预算用完永久退场 */
            sx[i] -= sv[i] * METEOR_SLOPE;
            sy[i] += sv[i];
            if (sy[i] > 44.0f || sx[i] < -4.0f) {
                if (meteor_spawns < METEOR_SPAWN_TARGET) {
                    sx[i] = (float)random(8, 124);
                    sy[i] = -6.0f - random(0, 10);
                    sv[i] = 4.5f + (float)random(0, 100) * 0.02f;
                    meteor_spawns++;
                } else {
                    sy[i] = 100.0f;   /* 预算用完: 永久退场 */
                }
            }
        }

        /* 全部流星出屏 → 触发揭示段 */
        if (reveal_at < 0) {
            bool done = true;
            for (int i = 0; i < LOGO_STARS && done; i++)
                if (sy[i] <= 44.0f) done = false;
            if (done) {
                reveal_at = f;
                total_frames = f + LOGO_REVEAL_FRAMES;
            }
        }
        } else {
        /* --- 烟花 (3枚火箭轮发: 上升→爆炸→粒子受引力坠落) --- */
        for (int r = 0; r < FW_ROCKETS; r++) {
            if (fw_ry[r] > 33.0f) {
                /* 待发: 倒计时结束后发射 (打满发数后停发) */
                if (fw_wait[r] > 0) {
                    fw_wait[r]--;
                } else if (fw_bursts < FW_BURSTS_TARGET) {
                    fw_rx[r]      = (float)random(20, 108);
                    fw_ry[r]      = 33.0f;
                    fw_rvy[r]     = -(3.0f + (float)random(0, 100) / 60.0f);
                    fw_burst_y[r] = random(5, 20);   /* 爆炸高度 */
                }
                continue;
            }
            /* 上升 + 减速 */
            fw_ry[r] += fw_rvy[r];
            fw_rvy[r] += 0.05f;
            if (fw_ry[r] > (float)fw_burst_y[r]) continue;

            /* 爆炸: 计数 + 12 粒子环 + 中心闪光 */
            fw_bursts++;
            for (int k = 0; k < 12; k++) {
                for (int j = 0; j < FW_PARTICLES; j++) {
                    if (fw_pl[j] != 0) continue;
                    float ang = (float)k * TWO_PI / 12.0f + 0.1f;
                    float spd = 0.9f + (float)random(0, 70) / 100.0f;
                    fw_px[j]  = fw_rx[r];
                    fw_py[j]  = fw_ry[r];
                    fw_pvx[j] = cos(ang) * spd;
                    fw_pvy[j] = sin(ang) * spd - 0.3f;
                    fw_pl[j]  = 16 + random(0, 10);
                    break;
                }
            }
            fw_flash_x = (int)fw_rx[r];
            fw_flash_y = (int)fw_ry[r];
            fw_flash_t = 4;
            fw_ry[r]   = 40.0f;   /* 火箭退场 */
            fw_wait[r] = random(10, 22);   /* 短间隔, 高频连发 */
        }

        /* 粒子: 运动 + 引力 + 出屏消亡 */
        for (int j = 0; j < FW_PARTICLES; j++) {
            if (fw_pl[j] == 0) continue;
            fw_px[j] += fw_pvx[j];
            fw_py[j] += fw_pvy[j];
            fw_pvy[j] += 0.07f;   /* 重力 */
            fw_pl[j]--;
            if (fw_px[j] < 0 || fw_px[j] > 127
                || fw_py[j] < 0 || fw_py[j] > 31) {
                fw_pl[j] = 0;
                continue;
            }
            /* 末期闪烁淡出: 寿命<6 时隔帧绘制 */
            if (fw_pl[j] < 6 && (fw_pl[j] & 1)) continue;
            u8g2.drawPixel((int)fw_px[j], (int)fw_py[j]);
        }

        /* 爆炸中心闪光: 短暂十字 */
        if (fw_flash_t > 0) {
            if (fw_flash_x >= 1 && fw_flash_x < 127
                && fw_flash_y >= 1 && fw_flash_y < 31) {
                u8g2.drawHLine(fw_flash_x - 1, fw_flash_y, 3);
                u8g2.drawVLine(fw_flash_x, fw_flash_y - 1, 3);
            }
            fw_flash_t--;
        }

        /* 火箭本体: 亮点 + 向下尾迹 (所有活跃火箭) */
        for (int r = 0; r < FW_ROCKETS; r++) {
            if (fw_ry[r] > 33.0f) continue;
            int rx = (int)fw_rx[r], ry = (int)fw_ry[r];
            if (rx >= 0 && rx < 128 && ry >= 0 && ry < 32) {
                u8g2.drawPixel(rx, ry);
                if (ry + 1 < 32) u8g2.drawPixel(rx, ry + 1);
            }
            for (int t = 3; t <= 9; t += 3) {
                int ty = ry + t;
                if (rx >= 0 && rx < 128 && ty >= 0 && ty < 32) {
                    u8g2.drawPixel(rx, ty);
                }
            }
        }

        /* 打满发数且粒子/火箭/闪光全部消散 → 触发揭示段 */
        if (reveal_at < 0 && fw_bursts >= FW_BURSTS_TARGET) {
            bool done = (fw_flash_t <= 0);
            for (int j = 0; j < FW_PARTICLES && done; j++)
                if (fw_pl[j] != 0) done = false;
            for (int r = 0; r < FW_ROCKETS && done; r++)
                if (fw_ry[r] <= 33.0f) done = false;
            if (done) {
                reveal_at = f;
                total_frames = f + LOGO_REVEAL_FRAMES;
            }
        }
        }

        /* --- 揭示段 (相对 reveal_at: 黑幕→标题→版本→定格)
         * 烟花模式下 reveal_at=-1 表示尚未触发, 不绘制 */
        int rf = f - reveal_at;
        if (reveal_at >= 0 && rf >= 0) {
            /* 黑幕 (揭示段帧0起, speed=65, 约0.5s 盖满全屏) */
            anim_ease(&curtain, 0.0f, 65.0f);
            int cover = (int)curtain + 34;
            if (cover < 0) cover = 0;
            if (cover > 32) cover = 32;
            u8g2.setDrawColor(0);
            if (cover > 0) u8g2.drawBox(0, 0, 128, cover);
            u8g2.setDrawColor(1);
            if (cover > 0 && cover < 32) u8g2.drawHLine(0, cover, 128);

            /* 标题 (揭示段帧10起, speed=42, 快速滑入后定格) */
            u8g2.setFont(FONT_LARGE);
            if (rf >= LOGO_REVEAL_TITLE) anim_ease(&title_y, 21.0f, 42.0f);
            const char* t = "DWZ Deck";
            int tw = u8g2.getStrWidth(t);
            u8g2.setCursor((128 - tw) / 2, (int)title_y);
            u8g2.print(t);

            /* 版本号 (揭示段帧20起, speed=28 缓动滑入) */
            u8g2.setFont(FONT_SMALL);
            if (rf >= LOGO_REVEAL_VER) anim_ease(&ver_x, 100.0f, 28.0f);
            if ((int)ver_x < 128) {   /* 完全屏外时不绘制 */
                u8g2.setCursor((int)ver_x, 30);
                u8g2.print("v3.0");
            }
        }

        u8g2.sendBuffer();
        delay(LOGO_DELAY);
    }
}

void setup1()
{
    /* OLED */
    Wire.setSDA(SDA_PIN);
    Wire.setSCL(SCL_PIN);
    Wire.begin();
    u8g2.begin();
    oled_logo_animation();

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

/* 键盘模式 — 4行分工: 状态 / Speed条 / Bright条 / 灯效名 */
void oled_show_keyboard(bool show = true)
{
    u8g2.clearBuffer();
    u8g2.setFont(FONT_SMALL);
    u8g2.setDrawColor(1);

    /* 行 0: 产品名(左) + 层标签·USB(右) */
    u8g2.setCursor(0, 7);
    u8g2.print("DWZ DECK");

    const char* layer_tag = (g_layer == LAYER_NUMPAD) ? "NUM" : " FN";
    int tag_w = (int)u8g2.getStrWidth(layer_tag) + 10;
    u8g2.setCursor(128 - tag_w, 7);
    u8g2.print(layer_tag);

    u8g2.setDrawColor(g_usb_mounted ? 1 : 0);
    u8g2.drawDisc(128 - tag_w - 6, 3, 2);
    u8g2.setDrawColor(1);
    u8g2.drawCircle(128 - tag_w - 6, 3, 2);

    /* 行 1: Speed 标签+进度条+数值  (bar 起始 x=52 w=48 值 x=106) */
    u8g2.setCursor(0, 15);
    u8g2.print("Speed");

    int spd_bar_w = map(g_led_speed, 0, 100, 0, 48);
    u8g2.drawRFrame(52, 11, 48, 4, 2);
    if (spd_bar_w > 2) u8g2.drawRBox(53, 12, spd_bar_w - 2, 2, 1);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", g_led_speed);
    u8g2.setCursor(106, 15);
    u8g2.print(buf);

    /* 行 2: Bright 标签+进度条+数值 */
    u8g2.setCursor(0, 23);
    u8g2.print("Bright");
    int br_bar_w = map(g_brightness, 0, 100, 0, 48);
    u8g2.drawRFrame(52, 19, 48, 4, 2);
    if (br_bar_w > 2) u8g2.drawRBox(53, 20, br_bar_w - 2, 2, 1);

    snprintf(buf, sizeof(buf), "%d%%", g_brightness);
    u8g2.setCursor(106, 23);
    u8g2.print(buf);

    /* 行 3: 当前灯效模式 */
    u8g2.setCursor(0, 31);
    const char* mn[] = {"Rainbow","Marquee","Breath","Meteor","Flash"};
    u8g2.print("LED:");
    u8g2.print(mn[g_led_mode]);

    if (show) u8g2.sendBuffer();
}

/* 计算器模式界面 */
void oled_show_calculator(bool show = true)
{
    u8g2.clearBuffer();
    u8g2.setFont(FONT_SMALL);
    u8g2.setDrawColor(1);

    /* 行 0: 标题(左) + 亮度(右) */
    u8g2.setCursor(0, 7);
    u8g2.print("CALC");

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", g_brightness);
    u8g2.setCursor(128 - (int)u8g2.getStrWidth(buf), 7);
    u8g2.print(buf);

    /* 行 1-2: 结果居中 */
    if (calc_display.length() <= 10) {
        u8g2.setFont(FONT_LARGE);
        int cx = (128 - (int)u8g2.getStrWidth(calc_display.c_str())) / 2;
        if (cx < 0) cx = 0;
        u8g2.setCursor(cx, 22);
        u8g2.print(calc_display);
    } else if (calc_display.length() <= 21) {
        u8g2.setFont(FONT_SMALL);
        u8g2.setCursor(2, 22);
        u8g2.print(calc_display);
    } else {
        u8g2.setFont(FONT_SMALL);
        u8g2.setCursor(2, 22);
        u8g2.print("...");
    }

    /* 行 3: 提示 */
    u8g2.setFont(FONT_SMALL);
    u8g2.setCursor(0, 31);
    u8g2.print("Press:AC  Hold:Menu");

    if (show) u8g2.sendBuffer();
}

/* 当前页"记忆选项"下标 (行右侧 ">" 标识), 无记忆选项的页返回 -1 */
static int menu_marked_index()
{
    switch (menu.active_page) {
        case PAGE_LED_MODE:   return (int)g_led_mode;
        case PAGE_KEY_LAYER:  return (int)g_layer;
        case PAGE_LOGO_STYLE: return g_logo_style;
        default:              return -1;
    }
}

/* 菜单模式界面 */
void oled_show_menu()
{
    /* 弹窗动画更新 (入场/退场) */
    popup_anim_update();

    if (menu.slider_active || menu.popup_enter || menu.popup_exit) {

        if (menu.popup_exit) {
            /* 退场: 画弹窗来源页 (不推屏) → 叠弹窗 (不推屏) → 一次推屏 */
            int sp = menu.popup_ret_page;
            int sc = menu.popup_ret_cursor;
            const MenuItem* items = menuPages[sp].items;
            oled_draw_menu(items, menuPages[sp].count, sc, 0, false);

            if (menu.active_page == PAGE_LED_SPEED)
                oled_draw_popup("Speed", g_led_speed, false, false);
            else if (menu.active_page == PAGE_CUBE_SPEED)
                oled_draw_popup("Speed", g_cube_speed, false, false);
            else
                oled_draw_popup("Brightness", g_brightness, false, false);

            u8g2.sendBuffer();
        } else {
            if (menu.active_page == PAGE_LED_SPEED)
                oled_draw_popup("Speed", g_led_speed);
            else if (menu.active_page == PAGE_CUBE_SPEED)
                oled_draw_popup("Speed", g_cube_speed);
            else
                oled_draw_popup("Brightness", g_brightness);
        }

    } else if (menu.active_page == PAGE_SHAPE_PREVIEW) {
        oled_draw_shape_preview();
    } else {
        const MenuItem* items = menuPages[menu.active_page].items;
        int count = menuPages[menu.active_page].count;
        oled_draw_menu(items, count, menu.cursor, menu.scroll_offset,
                       true, true, menu_marked_index());
    }
}

void loop1()
{
    /* 重放开屏动画 (调试用, 从菜单触发) */
    if (g_replay_logo) {
        g_replay_logo = false;
        oled_logo_animation();
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

    /* 屏保检测: 非菜单模式下空闲 15 秒触发 */
    if (!menu.is_open && !menu.animating) {
        if (!g_screensaver
            && (millis() - g_activity_ms > 15000)
            && g_usb_mounted) {
            g_screensaver = true;
        }
    }

    /* 显示 */
    if (g_screensaver) {
        oled_draw_cubesaver();
    } else if (menu.animating) {
        float target = menu.entering ? 0.0f : 32.0f;
        anim_ease(&menu.slide_offset, target, 38.0f);

        if (menu.entering) {
            /* 入场: 菜单从下方滑入 (纯菜单) */
            oled_show_menu();
            if (menu.slide_offset < 0.5f) {
                menu.slide_offset = 0.0f;
                menu.animating    = false;
            }
        } else {
            /* 退场: 先画主屏, 叠菜单下滑, 一次推屏 */
            if (g_usb_mounted)
                oled_show_keyboard(false);
            else
                oled_show_calculator(false);

            const MenuItem* items = menuPages[menu.active_page].items;
            int cnt = menuPages[menu.active_page].count;
            oled_draw_menu(items, cnt, menu.cursor,
                           menu.scroll_offset, false, false);

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