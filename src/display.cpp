/* display.cpp — OLED 绘制层: 主屏/菜单/弹窗/屏保/开屏动画
 */

#include "display.h"
#include <cmath>

void anim_ease(float *a, float target, float speed)
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
    static int   saver_mode;
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
        saver_mode ;
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
    static int  demo_mode;
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
        demo_mode;
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
                    bool show, bool clear_first,
                    int marked)
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
                     bool clear_first, bool show)
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

void oled_logo_animation()
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

void oled_show_keyboard(bool show)
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
void oled_show_calculator(bool show)
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
