/* menu.h — 菜单系统: 页面数据 + 导航逻辑
 * 共享类型 (LEDMode/KeyLayer) 也放这里, 供 menu 逻辑读写全局值 */
#ifndef MENU_H
#define MENU_H

#include <Arduino.h>

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

/* 菜单 ID 枚举 */
enum MenuPageID {
    PAGE_MAIN,
    PAGE_LED,
    PAGE_LED_MODE,
    PAGE_LED_BRIGHTNESS,
    PAGE_LED_SPEED,
    PAGE_LED_OLED,
    PAGE_KEY_LAYER,
    PAGE_SYSINFO,
    PAGE_ANIM,
    PAGE_CUBE_MENU,
    PAGE_CUBE_SPEED,
    PAGE_SHAPE_PREVIEW,
    PAGE_LOGO_MENU,
    PAGE_LOGO_STYLE,
    PAGE_SAVER_TIMEOUT
};

/* 菜单项: 标签 + 类型 */
struct MenuItem {
    const char* label;
    MenuItemType type;
};

/* 菜单页定义 */
struct MenuPageDef {
    const MenuItem* items;
    int             count;
};

/* 导航栈最大深度 */
#define MENU_STACK_MAX 6

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

#define MENU_TIMEOUT_MS  10000

/* 菜单页表 (定义在 menu.cpp) */
extern const MenuPageDef menuPages[];

/* 菜单运行时状态 (定义在 menu.cpp) */
extern MenuState menu;

/* Flash 设置标记 (定义在 main.cpp) */
void settings_mark_dirty();

/* 菜单导航逻辑 (定义在 menu.cpp) */
void menu_open();
void menu_close();
void menu_navigate(int delta);
void menu_select();
void menu_back();
void menu_check_timeout();

/* 弹窗动画 (popup_anim_update 由显示层每帧调用) */
bool popup_anim_update();

/* 双核共享全局 (定义在 main.cpp) */
extern volatile LEDMode g_led_mode;
extern volatile int     g_led_speed;
extern volatile int     g_brightness;
extern volatile KeyLayer g_layer;
extern volatile int     g_cube_speed;
extern volatile int     g_cube_mode;
extern volatile int     g_logo_style;
extern volatile int     g_oled_bright;
extern volatile int     g_saver_timeout;
extern volatile bool    g_contrast_dirty;
extern volatile bool    g_replay_logo;

#endif
