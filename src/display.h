/* display.h — OLED 绘制层: 主屏/菜单/弹窗/屏保/开屏动画
 * 全局 u8g2 对象与共享状态在 main.cpp 定义, 此处 extern */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "menu.h"

/* U8g2 字体: 5x8 适合 4 行布局, 9x15 用于大号数字 */
#define FONT_SMALL  u8g2_font_5x8_tf
#define FONT_LARGE  u8g2_font_9x15_tf

/* 缓动函数: 先快后慢, 接近目标时减速 (speed 须 >= 35) */
void anim_ease(float *a, float target, float speed);

/* 菜单绘制 (page=页ID 导航图标, marked=当前选中项下标, -1=无) */
void oled_draw_menu(const MenuItem* items, int count,
                    int cursor, int scroll,
                    bool show = true, bool clear_first = true,
                    int marked = -1, int page = -1);

/* 弹窗式数值调节器 */
void oled_draw_popup(const char* title, int value,
                     bool clear_first = true, bool show = true);

/* 屏保: 旋转多面体 (g_cube_mode==4 时随机换形) */
void oled_draw_cubesaver();

/* 形状预览页 */
void oled_draw_shape_preview();

/* 开屏 Logo 动画 (按 g_logo_style) */
void oled_logo_animation();

/* 各模式主屏 + 菜单屏 */
void oled_show_keyboard(bool show = true);
void oled_show_calculator(bool show = true);
void oled_show_menu();

/* 共享对象/状态 (定义在 main.cpp) */
extern U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2;
extern String calc_display;
extern volatile AppMode g_mode;
extern volatile bool    g_usb_mounted;

#endif
