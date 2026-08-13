/* menu.cpp — 菜单系统: 页面数据 + 导航逻辑
 */

#include "menu.h"
#include "display.h"   /* anim_ease (弹窗动画) */

MenuState menu;

/* ================================================================
 *  SECTION 6: 菜单系统数据
 * ================================================================ */

/* 菜单 ID 枚举 */

/* 菜单项: 标签 + 类型 */

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
    {"OLED",         MENU_SUBMENU},
    {"Speed",        MENU_SUBMENU},
    {"Back",         MENU_BACK},
};
#define MENU_LED_COUNT 5

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
    {"Timeout",     MENU_SUBMENU},
    {"Back",        MENU_BACK},
};
#define MENU_CUBE_COUNT 4

const MenuItem menuSaverTimeout[] = {
    {"10 sec",      MENU_ACTION},
    {"30 sec",      MENU_ACTION},
    {"60 sec",      MENU_ACTION},
    {"Off",         MENU_ACTION},
    {"Back",        MENU_BACK},
};
#define MENU_SAVER_TIMEOUT_COUNT 5

const MenuItem menuSysInfo[] = {
    {"DWZ Pad v3.0",     MENU_ACTION},
    {"RP2350 RISC-V",    MENU_ACTION},
    {"Back",             MENU_BACK},
};
#define MENU_INFO_COUNT 3

/* 菜单页表 */

/* 导航栈最大深度: Main→LED→Effect 或 Main→Anim→Cube→预览 */
const MenuPageDef menuPages[] = {
    {menuMain,          MENU_MAIN_COUNT},      /* PAGE_MAIN */
    {menuLED,           MENU_LED_COUNT},       /* PAGE_LED */
    {menuLEDMode,       MENU_LEDMODE_COUNT},   /* PAGE_LED_MODE */
    {nullptr,           0},                    /* PAGE_LED_BRIGHTNESS */
    {nullptr,           0},                    /* PAGE_LED_SPEED */
    {nullptr,           0},                    /* PAGE_LED_OLED */
    {menuKeyLayer,      MENU_LAYER_COUNT},     /* PAGE_KEY_LAYER */
    {menuSysInfo,       MENU_INFO_COUNT},      /* PAGE_SYSINFO */
    {menuAnim,          MENU_ANIM_COUNT},      /* PAGE_ANIM */
    {menuCube,          MENU_CUBE_COUNT},      /* PAGE_CUBE_MENU */
    {nullptr,           0},                    /* PAGE_CUBE_SPEED (弹窗) */
    {nullptr,           0},                    /* PAGE_SHAPE_PREVIEW */
    {menuLogo,          MENU_LOGO_COUNT},      /* PAGE_LOGO_MENU */
    {menuLogoStyle,     MENU_LOGO_STYLE_COUNT},/* PAGE_LOGO_STYLE */
    {menuSaverTimeout,  MENU_SAVER_TIMEOUT_COUNT},/* PAGE_SAVER_TIMEOUT */
};

/* 菜单运行时状态 */

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
bool popup_anim_update()
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


/* ================================================================
 *  SECTION 10: 菜单逻辑
 * ================================================================ */

/* 前向声明 (定义在 Section 11.5) */

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
        g_cube_mode = (g_cube_mode + delta + 6) % 6;
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
        } else if (menu.active_page == PAGE_LED_OLED) {
            int val = g_oled_bright + delta * step;
            if (val < 0)   val = 0;
            if (val > 100) val = 100;
            g_oled_bright = val;
            /* 只置脏标志: I2C 写屏必须由 Core 1 执行,
             * 否则与渲染线程抢总线导致画面崩坏 */
            g_contrast_dirty = true;
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
                && (menu.cursor == 1 || menu.cursor == 2
                    || menu.cursor == 3)) {
                /* LED Bright / OLED / Speed: 弹窗式滑块
                 * 返回位置存 popup_ret, 不动导航栈 */
                menu.popup_ret_page   = PAGE_LED;
                menu.popup_ret_cursor = menu.cursor;
                if (menu.cursor == 1)
                    menu.active_page = PAGE_LED_BRIGHTNESS;
                else if (menu.cursor == 2)
                    menu.active_page = PAGE_LED_OLED;
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
                } else if (menu.active_page == PAGE_CUBE_MENU
                           && menu.cursor == 2) {
                    /* Timeout 子菜单, 光标定位到当前超时档 */
                    target = PAGE_SAVER_TIMEOUT;
                    init_cursor = g_saver_timeout;
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
            else if (menu.active_page == PAGE_SAVER_TIMEOUT
                     && menu.cursor <= 3) {
                /* 选择屏保超时档, 选完弹栈回 Screen Saver 菜单 */
                g_saver_timeout = menu.cursor;
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

