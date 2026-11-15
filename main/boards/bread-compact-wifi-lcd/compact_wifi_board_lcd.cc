#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <lvgl.h>
#include <string>
#include <algorithm>
#include <cstdlib>

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0, (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C, 0x04, 0x12, 0x14, 0x1f}, 14, 0},
    {0xf1, (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D, 0x0C, 0x1A, 0x14, 0x1E}, 14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

#define TAG "ExpressiveRobotFace"

class RobotEyesDisplay : public SpiLcdDisplay {
private:
    // ESP-VoCat / EchoEar Animated Pet Ears
    lv_obj_t* ear_l_ = nullptr;
    lv_obj_t* ear_r_ = nullptr;
    lv_obj_t* inner_ear_l_ = nullptr;
    lv_obj_t* inner_ear_r_ = nullptr;

    // Eye body squircles
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;

    // Pixar-grade dual specular highlights (gloss reflection for 3D glassy lens effect)
    lv_obj_t* left_glare1_ = nullptr;
    lv_obj_t* left_glare2_ = nullptr;
    lv_obj_t* right_glare1_ = nullptr;
    lv_obj_t* right_glare2_ = nullptr;

    // Dynamic eyelid masks (black overlays that sculpt angular brows & smile crescents)
    lv_obj_t* left_top_lid_ = nullptr;
    lv_obj_t* right_top_lid_ = nullptr;
    lv_obj_t* left_bottom_lid_ = nullptr;
    lv_obj_t* right_bottom_lid_ = nullptr;

    // Cute Anime/VoCat rosy cheek blush spots (soft glowing pink ovals)
    lv_obj_t* blush_l_ = nullptr;
    lv_obj_t* blush_r_ = nullptr;

    // Sleek pet whiskers
    lv_obj_t* whisker_l1_ = nullptr;
    lv_obj_t* whisker_l2_ = nullptr;
    lv_obj_t* whisker_r1_ = nullptr;
    lv_obj_t* whisker_r2_ = nullptr;

    // Tear drop object for sad/crying expressions
    lv_obj_t* tear_ = nullptr;

    // Morphing mouth objects (center aperture + dynamic smiling/frowning corners)
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* mouth_corner_l_ = nullptr;
    lv_obj_t* mouth_corner_r_ = nullptr;

    // EchoEar-style Acoustic Soundwave Bars for Listening Mode
    lv_obj_t* wave_bar_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    // Subtitle label
    lv_obj_t* status_label_ = nullptr;

    // 30 FPS animation timer
    lv_timer_t* anim_timer_ = nullptr;

    // Mood & styling state
    std::string emotion_ = "neutral";
    uint32_t color_ = 0x00E5FF; // High-Tech Neon Cyan

    // Animation physics state
    int frame_count_ = 0;
    int blink_stage_ = 0; // 0=open, 1..4=blink frames
    int blink_interval_ = 110;
    int double_blink_counter_ = 0;
    int pet_reaction_frames_ = 0;

    // Saccadic gaze simulation (biological rapid eye jumps)
    int saccade_target_x_ = 0;
    int saccade_target_y_ = 0;
    int saccade_cur_x_ = 0;
    int saccade_cur_y_ = 0;
    int next_saccade_frame_ = 60;

    int mouth_phase_ = 0;
    int tear_y_ = 0;

    // Base geometry constants for 240x240 circular GC9A01 LCD
    static constexpr int EYE_WIDTH = 58;
    static constexpr int EYE_HEIGHT = 80;
    static constexpr int EYE_RADIUS = 26;
    static constexpr int LEFT_EYE_X = 46;
    static constexpr int RIGHT_EYE_X = 136;
    static constexpr int EYE_Y = 68;
    static constexpr int MOUTH_Y = 172;

public:
    RobotEyesDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                     int width, int height, int offset_x, int offset_y,
                     bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {}

    virtual ~RobotEyesDisplay() {
        if (anim_timer_) {
            lv_timer_delete(anim_timer_);
            anim_timer_ = nullptr;
        }
    }

    void TriggerPetReaction() {
        DisplayLockGuard lock(this);
        pet_reaction_frames_ = 45; // ~1.5s playful interaction
    }

    virtual void SetupUI() override {
        DisplayLockGuard lock(this);
        auto screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        lv_color_t col = lv_color_hex(color_);

        // --- 1. ESP-VoCat Animated Pet Ears (Top rim of circular display) ---
        ear_l_ = lv_obj_create(screen);
        lv_obj_set_size(ear_l_, 22, 26);
        lv_obj_set_pos(ear_l_, 68, 20);
        lv_obj_set_style_radius(ear_l_, 7, 0);
        lv_obj_set_style_bg_color(ear_l_, col, 0);
        lv_obj_set_style_bg_opa(ear_l_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ear_l_, 0, 0);
        lv_obj_set_style_transform_rotation(ear_l_, -220, 0);
        lv_obj_set_scrollbar_mode(ear_l_, LV_SCROLLBAR_MODE_OFF);

        inner_ear_l_ = lv_obj_create(ear_l_);
        lv_obj_set_size(inner_ear_l_, 10, 14);
        lv_obj_set_pos(inner_ear_l_, 6, 6);
        lv_obj_set_style_radius(inner_ear_l_, 4, 0);
        lv_obj_set_style_bg_color(inner_ear_l_, lv_color_hex(0xFF80AB), 0);
        lv_obj_set_style_bg_opa(inner_ear_l_, LV_OPA_70, 0);
        lv_obj_set_style_border_width(inner_ear_l_, 0, 0);
        lv_obj_set_scrollbar_mode(inner_ear_l_, LV_SCROLLBAR_MODE_OFF);

        ear_r_ = lv_obj_create(screen);
        lv_obj_set_size(ear_r_, 22, 26);
        lv_obj_set_pos(ear_r_, 150, 20);
        lv_obj_set_style_radius(ear_r_, 7, 0);
        lv_obj_set_style_bg_color(ear_r_, col, 0);
        lv_obj_set_style_bg_opa(ear_r_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ear_r_, 0, 0);
        lv_obj_set_style_transform_rotation(ear_r_, 220, 0);
        lv_obj_set_scrollbar_mode(ear_r_, LV_SCROLLBAR_MODE_OFF);

        inner_ear_r_ = lv_obj_create(ear_r_);
        lv_obj_set_size(inner_ear_r_, 10, 14);
        lv_obj_set_pos(inner_ear_r_, 6, 6);
        lv_obj_set_style_radius(inner_ear_r_, 4, 0);
        lv_obj_set_style_bg_color(inner_ear_r_, lv_color_hex(0xFF80AB), 0);
        lv_obj_set_style_bg_opa(inner_ear_r_, LV_OPA_70, 0);
        lv_obj_set_style_border_width(inner_ear_r_, 0, 0);
        lv_obj_set_scrollbar_mode(inner_ear_r_, LV_SCROLLBAR_MODE_OFF);

        // --- 2. Whiskers (Sleek Pet Accents on Cheeks) ---
        whisker_l1_ = lv_obj_create(screen);
        lv_obj_set_size(whisker_l1_, 16, 2);
        lv_obj_set_pos(whisker_l1_, 20, 134);
        lv_obj_set_style_radius(whisker_l1_, 1, 0);
        lv_obj_set_style_bg_color(whisker_l1_, col, 0);
        lv_obj_set_style_bg_opa(whisker_l1_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(whisker_l1_, 0, 0);
        lv_obj_set_style_transform_rotation(whisker_l1_, 100, 0);
        lv_obj_set_scrollbar_mode(whisker_l1_, LV_SCROLLBAR_MODE_OFF);

        whisker_l2_ = lv_obj_create(screen);
        lv_obj_set_size(whisker_l2_, 14, 2);
        lv_obj_set_pos(whisker_l2_, 22, 142);
        lv_obj_set_style_radius(whisker_l2_, 1, 0);
        lv_obj_set_style_bg_color(whisker_l2_, col, 0);
        lv_obj_set_style_bg_opa(whisker_l2_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(whisker_l2_, 0, 0);
        lv_obj_set_style_transform_rotation(whisker_l2_, -80, 0);
        lv_obj_set_scrollbar_mode(whisker_l2_, LV_SCROLLBAR_MODE_OFF);

        whisker_r1_ = lv_obj_create(screen);
        lv_obj_set_size(whisker_r1_, 16, 2);
        lv_obj_set_pos(whisker_r1_, 204, 134);
        lv_obj_set_style_radius(whisker_r1_, 1, 0);
        lv_obj_set_style_bg_color(whisker_r1_, col, 0);
        lv_obj_set_style_bg_opa(whisker_r1_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(whisker_r1_, 0, 0);
        lv_obj_set_style_transform_rotation(whisker_r1_, -100, 0);
        lv_obj_set_scrollbar_mode(whisker_r1_, LV_SCROLLBAR_MODE_OFF);

        whisker_r2_ = lv_obj_create(screen);
        lv_obj_set_size(whisker_r2_, 14, 2);
        lv_obj_set_pos(whisker_r2_, 204, 142);
        lv_obj_set_style_radius(whisker_r2_, 1, 0);
        lv_obj_set_style_bg_color(whisker_r2_, col, 0);
        lv_obj_set_style_bg_opa(whisker_r2_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(whisker_r2_, 0, 0);
        lv_obj_set_style_transform_rotation(whisker_r2_, 80, 0);
        lv_obj_set_scrollbar_mode(whisker_r2_, LV_SCROLLBAR_MODE_OFF);

        // --- 3. Cheek Blush Spots (Cute glowing ovals for happy/shy/loving) ---
        blush_l_ = lv_obj_create(screen);
        lv_obj_set_size(blush_l_, 24, 12);
        lv_obj_set_pos(blush_l_, 34, 142);
        lv_obj_set_style_radius(blush_l_, 6, 0);
        lv_obj_set_style_bg_color(blush_l_, lv_color_hex(0xFF3366), 0);
        lv_obj_set_style_bg_opa(blush_l_, LV_OPA_0, 0);
        lv_obj_set_style_border_width(blush_l_, 0, 0);
        lv_obj_set_scrollbar_mode(blush_l_, LV_SCROLLBAR_MODE_OFF);

        blush_r_ = lv_obj_create(screen);
        lv_obj_set_size(blush_r_, 24, 12);
        lv_obj_set_pos(blush_r_, 182, 142);
        lv_obj_set_style_radius(blush_r_, 6, 0);
        lv_obj_set_style_bg_color(blush_r_, lv_color_hex(0xFF3366), 0);
        lv_obj_set_style_bg_opa(blush_r_, LV_OPA_0, 0);
        lv_obj_set_style_border_width(blush_r_, 0, 0);
        lv_obj_set_scrollbar_mode(blush_r_, LV_SCROLLBAR_MODE_OFF);

        // --- 4. Left Eye Main Body (Neon Squircle with Bloom Glow) ---
        left_eye_ = lv_obj_create(screen);
        lv_obj_set_size(left_eye_, EYE_WIDTH, EYE_HEIGHT);
        lv_obj_set_pos(left_eye_, LEFT_EYE_X, EYE_Y);
        lv_obj_set_style_radius(left_eye_, EYE_RADIUS, 0);
        lv_obj_set_style_bg_color(left_eye_, col, 0);
        lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(left_eye_, 0, 0);
        lv_obj_set_style_shadow_width(left_eye_, 14, 0);
        lv_obj_set_style_shadow_color(left_eye_, col, 0);
        lv_obj_set_style_shadow_opa(left_eye_, LV_OPA_50, 0);
        lv_obj_set_style_shadow_spread(left_eye_, 1, 0);
        lv_obj_set_scrollbar_mode(left_eye_, LV_SCROLLBAR_MODE_OFF);

        // Left Eye Primary Glint (Glossy Pill)
        left_glare1_ = lv_obj_create(left_eye_);
        lv_obj_set_size(left_glare1_, 12, 16);
        lv_obj_set_pos(left_glare1_, 8, 8);
        lv_obj_set_style_radius(left_glare1_, 6, 0);
        lv_obj_set_style_bg_color(left_glare1_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(left_glare1_, LV_OPA_80, 0);
        lv_obj_set_style_border_width(left_glare1_, 0, 0);
        lv_obj_set_scrollbar_mode(left_glare1_, LV_SCROLLBAR_MODE_OFF);

        // Left Eye Secondary Glint (Subtle Glimmer Dot)
        left_glare2_ = lv_obj_create(left_eye_);
        lv_obj_set_size(left_glare2_, 6, 6);
        lv_obj_set_pos(left_glare2_, 38, 54);
        lv_obj_set_style_radius(left_glare2_, 3, 0);
        lv_obj_set_style_bg_color(left_glare2_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(left_glare2_, LV_OPA_40, 0);
        lv_obj_set_style_border_width(left_glare2_, 0, 0);
        lv_obj_set_scrollbar_mode(left_glare2_, LV_SCROLLBAR_MODE_OFF);

        // --- 5. Right Eye Main Body (Neon Squircle with Bloom Glow) ---
        right_eye_ = lv_obj_create(screen);
        lv_obj_set_size(right_eye_, EYE_WIDTH, EYE_HEIGHT);
        lv_obj_set_pos(right_eye_, RIGHT_EYE_X, EYE_Y);
        lv_obj_set_style_radius(right_eye_, EYE_RADIUS, 0);
        lv_obj_set_style_bg_color(right_eye_, col, 0);
        lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(right_eye_, 0, 0);
        lv_obj_set_style_shadow_width(right_eye_, 14, 0);
        lv_obj_set_style_shadow_color(right_eye_, col, 0);
        lv_obj_set_style_shadow_opa(right_eye_, LV_OPA_50, 0);
        lv_obj_set_style_shadow_spread(right_eye_, 1, 0);
        lv_obj_set_scrollbar_mode(right_eye_, LV_SCROLLBAR_MODE_OFF);

        // Right Eye Primary Glint (Glossy Pill)
        right_glare1_ = lv_obj_create(right_eye_);
        lv_obj_set_size(right_glare1_, 12, 16);
        lv_obj_set_pos(right_glare1_, 8, 8);
        lv_obj_set_style_radius(right_glare1_, 6, 0);
        lv_obj_set_style_bg_color(right_glare1_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(right_glare1_, LV_OPA_80, 0);
        lv_obj_set_style_border_width(right_glare1_, 0, 0);
        lv_obj_set_scrollbar_mode(right_glare1_, LV_SCROLLBAR_MODE_OFF);

        // Right Eye Secondary Glint (Subtle Glimmer Dot)
        right_glare2_ = lv_obj_create(right_eye_);
        lv_obj_set_size(right_glare2_, 6, 6);
        lv_obj_set_pos(right_glare2_, 38, 54);
        lv_obj_set_style_radius(right_glare2_, 3, 0);
        lv_obj_set_style_bg_color(right_glare2_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(right_glare2_, LV_OPA_40, 0);
        lv_obj_set_style_border_width(right_glare2_, 0, 0);
        lv_obj_set_scrollbar_mode(right_glare2_, LV_SCROLLBAR_MODE_OFF);

        // --- 6. Dynamic Eyelid Masks (OLED-Black Overlays for Expressions) ---
        // Top Lids (for blinks, sleepy, and angled brows with rotation support)
        left_top_lid_ = lv_obj_create(screen);
        lv_obj_set_size(left_top_lid_, EYE_WIDTH + 14, 0);
        lv_obj_set_pos(left_top_lid_, LEFT_EYE_X - 7, EYE_Y - 6);
        lv_obj_set_style_bg_color(left_top_lid_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(left_top_lid_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(left_top_lid_, 0, 0);
        lv_obj_set_scrollbar_mode(left_top_lid_, LV_SCROLLBAR_MODE_OFF);

        right_top_lid_ = lv_obj_create(screen);
        lv_obj_set_size(right_top_lid_, EYE_WIDTH + 14, 0);
        lv_obj_set_pos(right_top_lid_, RIGHT_EYE_X - 7, EYE_Y - 6);
        lv_obj_set_style_bg_color(right_top_lid_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(right_top_lid_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(right_top_lid_, 0, 0);
        lv_obj_set_scrollbar_mode(right_top_lid_, LV_SCROLLBAR_MODE_OFF);

        // Bottom Lids (for happy smiling crescents)
        left_bottom_lid_ = lv_obj_create(screen);
        lv_obj_set_size(left_bottom_lid_, EYE_WIDTH + 16, 0);
        lv_obj_set_pos(left_bottom_lid_, LEFT_EYE_X - 8, EYE_Y + EYE_HEIGHT);
        lv_obj_set_style_bg_color(left_bottom_lid_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(left_bottom_lid_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(left_bottom_lid_, 24, 0);
        lv_obj_set_style_border_width(left_bottom_lid_, 0, 0);
        lv_obj_set_scrollbar_mode(left_bottom_lid_, LV_SCROLLBAR_MODE_OFF);

        right_bottom_lid_ = lv_obj_create(screen);
        lv_obj_set_size(right_bottom_lid_, EYE_WIDTH + 16, 0);
        lv_obj_set_pos(right_bottom_lid_, RIGHT_EYE_X - 8, EYE_Y + EYE_HEIGHT);
        lv_obj_set_style_bg_color(right_bottom_lid_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(right_bottom_lid_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(right_bottom_lid_, 24, 0);
        lv_obj_set_style_border_width(right_bottom_lid_, 0, 0);
        lv_obj_set_scrollbar_mode(right_bottom_lid_, LV_SCROLLBAR_MODE_OFF);

        // --- 7. Tear Drop (for Sad/Crying Expressions) ---
        tear_ = lv_obj_create(screen);
        lv_obj_set_size(tear_, 6, 12);
        lv_obj_set_pos(tear_, LEFT_EYE_X + 18, EYE_Y + EYE_HEIGHT);
        lv_obj_set_style_radius(tear_, 3, 0);
        lv_obj_set_style_bg_color(tear_, lv_color_hex(0x2979FF), 0);
        lv_obj_set_style_bg_opa(tear_, LV_OPA_0, 0);
        lv_obj_set_style_border_width(tear_, 0, 0);
        lv_obj_set_scrollbar_mode(tear_, LV_SCROLLBAR_MODE_OFF);

        // --- 8. Morphing Animated Mouth ---
        mouth_ = lv_obj_create(screen);
        lv_obj_set_size(mouth_, 44, 6);
        lv_obj_set_pos(mouth_, 98, MOUTH_Y);
        lv_obj_set_style_radius(mouth_, 3, 0);
        lv_obj_set_style_bg_color(mouth_, col, 0);
        lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mouth_, 0, 0);
        lv_obj_set_style_shadow_width(mouth_, 8, 0);
        lv_obj_set_style_shadow_color(mouth_, col, 0);
        lv_obj_set_style_shadow_opa(mouth_, LV_OPA_50, 0);
        lv_obj_set_scrollbar_mode(mouth_, LV_SCROLLBAR_MODE_OFF);

        mouth_corner_l_ = lv_obj_create(screen);
        lv_obj_set_size(mouth_corner_l_, 8, 6);
        lv_obj_set_pos(mouth_corner_l_, 92, MOUTH_Y);
        lv_obj_set_style_radius(mouth_corner_l_, 3, 0);
        lv_obj_set_style_bg_color(mouth_corner_l_, col, 0);
        lv_obj_set_style_bg_opa(mouth_corner_l_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mouth_corner_l_, 0, 0);
        lv_obj_set_scrollbar_mode(mouth_corner_l_, LV_SCROLLBAR_MODE_OFF);

        mouth_corner_r_ = lv_obj_create(screen);
        lv_obj_set_size(mouth_corner_r_, 8, 6);
        lv_obj_set_pos(mouth_corner_r_, 140, MOUTH_Y);
        lv_obj_set_style_radius(mouth_corner_r_, 3, 0);
        lv_obj_set_style_bg_color(mouth_corner_r_, col, 0);
        lv_obj_set_style_bg_opa(mouth_corner_r_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mouth_corner_r_, 0, 0);
        lv_obj_set_scrollbar_mode(mouth_corner_r_, LV_SCROLLBAR_MODE_OFF);

        // --- 9. EchoEar Acoustic Soundwave Bars for Listening Mode ---
        static const int wave_x[5] = {100, 108, 116, 124, 132};
        for (int i = 0; i < 5; ++i) {
            wave_bar_[i] = lv_obj_create(screen);
            lv_obj_set_size(wave_bar_[i], 4, 8);
            lv_obj_set_pos(wave_bar_[i], wave_x[i], MOUTH_Y);
            lv_obj_set_style_radius(wave_bar_[i], 2, 0);
            lv_obj_set_style_bg_color(wave_bar_[i], col, 0);
            lv_obj_set_style_bg_opa(wave_bar_[i], LV_OPA_0, 0);
            lv_obj_set_style_border_width(wave_bar_[i], 0, 0);
            lv_obj_set_scrollbar_mode(wave_bar_[i], LV_SCROLLBAR_MODE_OFF);
        }

        // --- 10. Subtitle Status Label (Centered at bottom curve) ---
        status_label_ = lv_label_create(screen);
        lv_obj_set_width(status_label_, 190);
        lv_obj_set_pos(status_label_, 25, 204);
        lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0x888888), 0);
        lv_label_set_text(status_label_, "");

        // --- 11. 30 FPS Animation Timer (33ms) ---
        anim_timer_ = lv_timer_create([](lv_timer_t* t) {
            auto self = static_cast<RobotEyesDisplay*>(lv_timer_get_user_data(t));
            if (self) self->UpdateAnimation();
        }, 33, this);

        setup_ui_called_ = true;
    }

    void UpdateAnimation() {
        frame_count_++;

        // Pet reaction active timer
        if (pet_reaction_frames_ > 0) {
            pet_reaction_frames_--;
        }

        // --- 1. Saccadic Gaze Engine (Natural Micro-Looks & Glances) ---
        if (frame_count_ >= next_saccade_frame_) {
            next_saccade_frame_ = frame_count_ + 75 + (frame_count_ % 65);
            int rnd = (frame_count_ / 17) % 6;
            if (rnd == 0) {
                saccade_target_x_ = -12; // Look curious left
                saccade_target_y_ = 0;
            } else if (rnd == 1) {
                saccade_target_x_ = 12;  // Look curious right
                saccade_target_y_ = 0;
            } else if (rnd == 2) {
                saccade_target_x_ = -8;  // Look up-left thoughtfully
                saccade_target_y_ = -5;
            } else if (rnd == 3) {
                saccade_target_x_ = 8;   // Look up-right
                saccade_target_y_ = -5;
            } else {
                saccade_target_x_ = 0;   // Forward center
                saccade_target_y_ = 0;
            }
        }

        // Fast biological spring interpolation for saccades
        int dx = saccade_target_x_ - saccade_cur_x_;
        int dy = saccade_target_y_ - saccade_cur_y_;
        if (dx != 0) saccade_cur_x_ += (dx > 0) ? std::max(1, (dx + 1) / 2) : std::min(-1, (dx - 1) / 2);
        if (dy != 0) saccade_cur_y_ += (dy > 0) ? std::max(1, (dy + 1) / 2) : std::min(-1, (dy - 1) / 2);

        // Subtle biological breathing float (+-1 pixel sinusoidal heave)
        int breath_y = (frame_count_ % 70 < 35) ? 0 : 1;

        // --- 2. Dynamic Organic Blinking with Squash & Stretch ---
        if (blink_stage_ == 0) {
            if (frame_count_ % blink_interval_ == 0) {
                blink_stage_ = 1;
                double_blink_counter_++;
                blink_interval_ = 95 + (frame_count_ % 45); // Randomized natural timing
            }
        }

        int top_lid_h = 0;
        int blink_squash_w = 0;

        if (blink_stage_ == 1) { // Closing fast
            top_lid_h = EYE_HEIGHT * 6 / 10;
            blink_squash_w = 2;
            blink_stage_ = 2;
        } else if (blink_stage_ == 2) { // Fully closed
            top_lid_h = EYE_HEIGHT + 4;
            blink_squash_w = 4;
            blink_stage_ = 3;
        } else if (blink_stage_ == 3) { // Opening with elastic overshoot
            top_lid_h = EYE_HEIGHT * 3 / 10;
            blink_squash_w = 1;
            blink_stage_ = 4;
        } else if (blink_stage_ == 4) { // Reopened
            top_lid_h = 0;
            blink_squash_w = 0;
            if ((double_blink_counter_ % 3 == 0) && (double_blink_counter_ % 6 != 0)) {
                blink_stage_ = 1; // Playful double-blink flutter
                double_blink_counter_++;
            } else {
                blink_stage_ = 0;
            }
        }

        // --- 3. ESP-VoCat Pet Ear Kinematics ---
        int ear_rot_l = -220;
        int ear_rot_r = 220;
        int ear_y = 20 + breath_y;

        if (pet_reaction_frames_ > 0) {
            // Pet reaction: happy rapid ear flutter!
            int flick = (frame_count_ % 6 < 3) ? 50 : -30;
            ear_rot_l = -220 + flick;
            ear_rot_r = 220 - flick;
            ear_y = 18;
        } else if (emotion_ == "listening") {
            // Ears perk straight up attentively!
            ear_rot_l = -100;
            ear_rot_r = 100;
            ear_y = 18;
        } else if (emotion_ == "sleepy" || emotion_ == "tired") {
            // Ears droop downward sleepily
            ear_rot_l = -340;
            ear_rot_r = 340;
            ear_y = 24;
        } else if (emotion_ == "happy" || emotion_ == "laughing" || emotion_ == "funny") {
            ear_rot_l = -180;
            ear_rot_r = 180;
            ear_y = (frame_count_ % 10 < 5) ? 18 : 20;
        } else if (blink_stage_ > 0 && double_blink_counter_ % 2 == 0) {
            // Cute ear twitch during double blinks
            ear_rot_l = -160;
        }

        if (ear_l_ && ear_r_) {
            lv_obj_set_pos(ear_l_, 68, ear_y);
            lv_obj_set_pos(ear_r_, 150, ear_y);
            lv_obj_set_style_transform_rotation(ear_l_, ear_rot_l, 0);
            lv_obj_set_style_transform_rotation(ear_r_, ear_rot_r, 0);
        }

        // --- 4. Mood-Specific Morphing, Rotation, & Dynamic Shapes ---
        int eye_w = EYE_WIDTH + blink_squash_w;
        int eye_h = EYE_HEIGHT;
        int left_y_offset = 0;
        int right_y_offset = 0;
        int bottom_lid_h = 0;
        int top_lid_rot_l = 0; // In 0.1 degrees
        int top_lid_rot_r = 0;
        int bounce_y = 0;
        lv_opa_t blush_opa = LV_OPA_0;

        if (pet_reaction_frames_ > 0) {
            // Interactive pet reaction: joyful wink & blush!
            bottom_lid_h = 44;
            top_lid_h = (top_lid_h > 12) ? top_lid_h : 10;
            blush_opa = LV_OPA_90;
            bounce_y = (frame_count_ % 8 < 4) ? -2 : 0;
        } else if (emotion_ == "happy") {
            bottom_lid_h = 44;
            top_lid_h = (top_lid_h > 12) ? top_lid_h : 10;
            blush_opa = LV_OPA_70;
            bounce_y = (frame_count_ % 30 < 15) ? -1 : 0;
        } else if (emotion_ == "laughing" || emotion_ == "funny") {
            bottom_lid_h = 48;
            top_lid_h = (top_lid_h > 20) ? top_lid_h : 18;
            blush_opa = LV_OPA_80;
            bounce_y = (frame_count_ % 6 < 3) ? -2 : 2;
        } else if (emotion_ == "angry") {
            top_lid_rot_l = 140;
            top_lid_rot_r = -140;
            top_lid_h = (top_lid_h > 36) ? top_lid_h : 36;
            eye_w = EYE_WIDTH - 2;
        } else if (emotion_ == "thinking") {
            left_y_offset = -7;
            right_y_offset = 3;
            top_lid_rot_l = -100;
            top_lid_h = (top_lid_h > 16) ? top_lid_h : 16;
            saccade_cur_x_ = -10;
            saccade_cur_y_ = -5;
        } else if (emotion_ == "shocked" || emotion_ == "surprised") {
            eye_w = EYE_WIDTH + 6;
            eye_h = EYE_HEIGHT + 8;
            top_lid_h = 0;
            bottom_lid_h = 0;
            bounce_y = (frame_count_ % 4 < 2) ? -1 : 1;
        } else if (emotion_ == "sad" || emotion_ == "crying") {
            top_lid_rot_l = -120;
            top_lid_rot_r = 120;
            top_lid_h = (top_lid_h > 30) ? top_lid_h : 30;
            left_y_offset = 3;
            right_y_offset = 3;
            if (emotion_ == "crying" && tear_) {
                tear_y_ = (tear_y_ + 2) % 45;
                lv_obj_set_pos(tear_, LEFT_EYE_X + 22, EYE_Y + EYE_HEIGHT + tear_y_ - 6);
                lv_obj_set_style_bg_opa(tear_, (tear_y_ > 38) ? LV_OPA_30 : LV_OPA_80, 0);
            }
        } else if (emotion_ == "sleepy" || emotion_ == "tired") {
            top_lid_h = (top_lid_h > 58) ? top_lid_h : 58;
            breath_y = (frame_count_ % 90 < 45) ? 0 : 2;
        } else if (emotion_ == "loving" || emotion_ == "heart" || emotion_ == "shy") {
            blush_opa = LV_OPA_80;
            bottom_lid_h = 32;
            top_lid_h = (top_lid_h > 10) ? top_lid_h : 8;
        } else if (emotion_ == "listening") {
            eye_h = EYE_HEIGHT + 3;
            top_lid_h = (top_lid_h > 0) ? top_lid_h : 0;
        }

        if (emotion_ != "crying" && tear_) {
            lv_obj_set_style_bg_opa(tear_, LV_OPA_0, 0);
            tear_y_ = 0;
        }

        // Update Blush Spots opacity
        if (blush_l_ && blush_r_) {
            lv_obj_set_style_bg_opa(blush_l_, blush_opa, 0);
            lv_obj_set_style_bg_opa(blush_r_, blush_opa, 0);
        }

        // --- 5. Render Left Eye & Dynamic Sculptors ---
        if (left_eye_ && left_top_lid_ && left_bottom_lid_) {
            int cur_lx = LEFT_EYE_X + saccade_cur_x_ - (eye_w - EYE_WIDTH) / 2;
            int cur_ly = EYE_Y + breath_y + bounce_y + left_y_offset;

            lv_obj_set_pos(left_eye_, cur_lx, cur_ly);
            lv_obj_set_size(left_eye_, eye_w, eye_h);

            if (left_glare1_) {
                int g1_x = 8 + (saccade_cur_x_ * 3) / 10;
                int g1_y = 8 + (saccade_cur_y_ * 3) / 10;
                lv_obj_set_pos(left_glare1_, g1_x, g1_y);
            }
            if (left_glare2_) {
                int g2_x = 38 + (saccade_cur_x_ * 2) / 10;
                int g2_y = 54 + (saccade_cur_y_ * 2) / 10;
                lv_obj_set_pos(left_glare2_, g2_x, g2_y);
            }

            lv_obj_set_pos(left_top_lid_, cur_lx - 7, cur_ly - 6);
            lv_obj_set_size(left_top_lid_, eye_w + 14, top_lid_h);
            lv_obj_set_style_transform_rotation(left_top_lid_, top_lid_rot_l, 0);

            lv_obj_set_pos(left_bottom_lid_, cur_lx - 8, cur_ly + eye_h - bottom_lid_h);
            lv_obj_set_size(left_bottom_lid_, eye_w + 16, bottom_lid_h + 6);
        }

        // --- 6. Render Right Eye & Dynamic Sculptors ---
        if (right_eye_ && right_top_lid_ && right_bottom_lid_) {
            int cur_rx = RIGHT_EYE_X + saccade_cur_x_ - (eye_w - EYE_WIDTH) / 2;
            int cur_ry = EYE_Y + breath_y + bounce_y + right_y_offset;

            lv_obj_set_pos(right_eye_, cur_rx, cur_ry);
            lv_obj_set_size(right_eye_, eye_w, eye_h);

            if (right_glare1_) {
                int g1_x = 8 + (saccade_cur_x_ * 3) / 10;
                int g1_y = 8 + (saccade_cur_y_ * 3) / 10;
                lv_obj_set_pos(right_glare1_, g1_x, g1_y);
            }
            if (right_glare2_) {
                int g2_x = 38 + (saccade_cur_x_ * 2) / 10;
                int g2_y = 54 + (saccade_cur_y_ * 2) / 10;
                lv_obj_set_pos(right_glare2_, g2_x, g2_y);
            }

            lv_obj_set_pos(right_top_lid_, cur_rx - 7, cur_ry - 6);
            lv_obj_set_size(right_top_lid_, eye_w + 14, top_lid_h);
            lv_obj_set_style_transform_rotation(right_top_lid_, top_lid_rot_r, 0);

            lv_obj_set_pos(right_bottom_lid_, cur_rx - 8, cur_ry + eye_h - bottom_lid_h);
            lv_obj_set_size(right_bottom_lid_, eye_w + 16, bottom_lid_h + 6);
        }

        // --- 7. Morphing Mouth & EchoEar Soundwave Visualizer ---
        int mouth_w = 42;
        int mouth_h = 5;
        int corner_y_offset = 0;

        bool is_listening = (emotion_ == "listening");

        // Toggle Acoustic Soundwave vs Normal Mouth
        for (int i = 0; i < 5; ++i) {
            if (wave_bar_[i]) {
                if (is_listening) {
                    lv_obj_set_style_bg_opa(wave_bar_[i], LV_OPA_COVER, 0);
                    static const int base_h[5] = {6, 12, 18, 12, 6};
                    int pulse = (frame_count_ * 4 + i * 2) % 10;
                    int h = base_h[i] + ((pulse > 5) ? (10 - pulse) : pulse);
                    lv_obj_set_size(wave_bar_[i], 4, h);
                    lv_obj_set_pos(wave_bar_[i], 100 + i * 8, MOUTH_Y - h / 2);
                } else {
                    lv_obj_set_style_bg_opa(wave_bar_[i], LV_OPA_0, 0);
                }
            }
        }

        if (is_listening) {
            // Hide standard mouth during soundwave display
            if (mouth_) lv_obj_set_style_bg_opa(mouth_, LV_OPA_0, 0);
            if (mouth_corner_l_) lv_obj_set_style_bg_opa(mouth_corner_l_, LV_OPA_0, 0);
            if (mouth_corner_r_) lv_obj_set_style_bg_opa(mouth_corner_r_, LV_OPA_0, 0);
        } else {
            if (mouth_) lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);

            if (pet_reaction_frames_ > 0) {
                // Cheerful open smile
                mouth_w = 54;
                mouth_h = 10;
                corner_y_offset = -6;
            } else if (emotion_ == "speaking") {
                mouth_phase_ = (mouth_phase_ + 1) % 12;
                static const int speech_h[12] = {4, 10, 18, 24, 16, 10, 5, 14, 22, 16, 8, 4};
                static const int speech_w[12] = {40, 46, 52, 56, 48, 44, 42, 48, 54, 48, 42, 40};
                mouth_h = speech_h[mouth_phase_];
                mouth_w = speech_w[mouth_phase_];
                corner_y_offset = (mouth_h > 12) ? -2 : 0;
                if (mouth_h > 16 && left_eye_ && right_eye_) {
                    lv_obj_set_size(left_eye_, eye_w + 1, eye_h - 1);
                    lv_obj_set_size(right_eye_, eye_w + 1, eye_h - 1);
                }
            } else if (emotion_ == "happy" || emotion_ == "laughing" || emotion_ == "funny") {
                mouth_w = 54;
                mouth_h = (emotion_ == "laughing" || emotion_ == "funny") ? 14 : 6;
                corner_y_offset = -7;
            } else if (emotion_ == "angry") {
                mouth_w = 48;
                mouth_h = 5;
                corner_y_offset = 6;
            } else if (emotion_ == "sad" || emotion_ == "crying") {
                mouth_w = 38;
                mouth_h = 4;
                corner_y_offset = 6;
            } else if (emotion_ == "shocked" || emotion_ == "surprised") {
                mouth_w = 20;
                mouth_h = 22;
            } else if (emotion_ == "thinking") {
                mouth_w = 32;
                mouth_h = 4;
                corner_y_offset = (frame_count_ % 30 < 15) ? -2 : 2;
            } else if (emotion_ == "loving" || emotion_ == "shy") {
                mouth_w = 42;
                mouth_h = 5;
                corner_y_offset = -4;
            }

            if (mouth_ && mouth_corner_l_ && mouth_corner_r_) {
                int mx = 120 - mouth_w / 2;
                int my = MOUTH_Y - mouth_h / 2;

                lv_obj_set_pos(mouth_, mx, my);
                lv_obj_set_size(mouth_, mouth_w, mouth_h);
                lv_obj_set_style_radius(mouth_, mouth_h / 2, 0);

                if (emotion_ == "shocked" || emotion_ == "surprised") {
                    lv_obj_set_style_bg_opa(mouth_corner_l_, LV_OPA_0, 0);
                    lv_obj_set_style_bg_opa(mouth_corner_r_, LV_OPA_0, 0);
                } else {
                    lv_obj_set_style_bg_opa(mouth_corner_l_, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_opa(mouth_corner_r_, LV_OPA_COVER, 0);
                    lv_obj_set_pos(mouth_corner_l_, mx - 6, MOUTH_Y + corner_y_offset - 2);
                    lv_obj_set_pos(mouth_corner_r_, mx + mouth_w - 2, MOUTH_Y + corner_y_offset - 2);
                }
            }
        }
    }

    virtual bool SetEmotion(const char* emotion) override {
        if (!emotion) return false;
        DisplayLockGuard lock(this);
        emotion_ = emotion;

        if (emotion_ == "listening") {
            color_ = 0x00FF66; // Electric Lime Green
        } else if (emotion_ == "thinking") {
            color_ = 0xD500F9; // Cyberpunk Vivid Magenta
        } else if (emotion_ == "speaking") {
            color_ = 0xFFB300; // Warm Solar Amber
        } else if (emotion_ == "happy" || emotion_ == "laughing" || emotion_ == "funny") {
            color_ = 0xFFEA00; // Radiant Lemon Gold
        } else if (emotion_ == "sad" || emotion_ == "crying") {
            color_ = 0x2979FF; // Deep Cerulean Blue
        } else if (emotion_ == "angry") {
            color_ = 0xFF1744; // Fiery Crimson Red
        } else if (emotion_ == "sleepy" || emotion_ == "tired") {
            color_ = 0x80D8FF; // Soft Dreamy Ice Blue
        } else if (emotion_ == "loving" || emotion_ == "heart" || emotion_ == "shy") {
            color_ = 0xFF4081; // Electric Rose Pink
        } else if (emotion_ == "shocked" || emotion_ == "surprised") {
            color_ = 0xFF6D00; // Intense Neon Orange
        } else if (emotion_ == "chill" || emotion_ == "relaxed") {
            color_ = 0x00E676; // Mint Turquoise
        } else { // neutral / idle
            color_ = 0x00E5FF; // High-Tech Neon Cyan
        }

        lv_color_t col = lv_color_hex(color_);

        // Update Ears & Whiskers Color
        if (ear_l_ && ear_r_) {
            lv_obj_set_style_bg_color(ear_l_, col, 0);
            lv_obj_set_style_bg_color(ear_r_, col, 0);
        }
        if (whisker_l1_ && whisker_l2_ && whisker_r1_ && whisker_r2_) {
            lv_obj_set_style_bg_color(whisker_l1_, col, 0);
            lv_obj_set_style_bg_color(whisker_l2_, col, 0);
            lv_obj_set_style_bg_color(whisker_r1_, col, 0);
            lv_obj_set_style_bg_color(whisker_r2_, col, 0);
        }

        // Update Eyes & Glow
        if (left_eye_ && right_eye_) {
            lv_obj_set_style_bg_color(left_eye_, col, 0);
            lv_obj_set_style_bg_color(right_eye_, col, 0);
            lv_obj_set_style_shadow_color(left_eye_, col, 0);
            lv_obj_set_style_shadow_color(right_eye_, col, 0);
        }

        // Update Mouth & Wave Bars
        if (mouth_ && mouth_corner_l_ && mouth_corner_r_) {
            lv_obj_set_style_bg_color(mouth_, col, 0);
            lv_obj_set_style_bg_color(mouth_corner_l_, col, 0);
            lv_obj_set_style_bg_color(mouth_corner_r_, col, 0);
            lv_obj_set_style_shadow_color(mouth_, col, 0);
        }
        for (int i = 0; i < 5; ++i) {
            if (wave_bar_[i]) lv_obj_set_style_bg_color(wave_bar_[i], col, 0);
        }
        return true;
    }

    // Prevents LcdDisplay::SetTheme() from dereferencing uninitialized status/battery/mute labels
    virtual void SetTheme(Theme* theme) override {
        DisplayLockGuard lock(this);
        Display::SetTheme(theme);
    }

    virtual void SetStatus(const char* status) override {
        DisplayLockGuard lock(this);
        if (status_label_ && status) {
            lv_label_set_text(status_label_, status);
        }
    }

    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override {
        DisplayLockGuard lock(this);
        if (status_label_ && notification) {
            lv_label_set_text(status_label_, notification);
        }
    }

    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override {
        ShowNotification(notification.c_str(), duration_ms);
    }

    virtual void UpdateStatusBar(bool update_all = false) override {}

    virtual void SetChatMessage(const char* role, const char* content) override {
        DisplayLockGuard lock(this);
        if (status_label_) {
            if (content && content[0] != '\0') {
                std::string s = content;
                std::replace(s.begin(), s.end(), '\n', ' ');
                if (s.length() > 36) s = s.substr(0, 33) + "...";
                lv_label_set_text(status_label_, s.c_str());
            } else {
                lv_label_set_text(status_label_, "");
            }
        }
    }

    virtual void ClearChatMessages() override {
        DisplayLockGuard lock(this);
        if (status_label_) {
            lv_label_set_text(status_label_, "");
        }
    }

    virtual void RemoveChatMessage(const char* role, const char* content) override {
        ClearChatMessages();
    }
};

class CompactWifiBoardLCD : public WifiBoard {
private:
    Button boot_button_;
    Display* display_;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
        panel_config.vendor_config = &gc9107_vendor_config;
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new RobotEyesDisplay(panel_io, panel,
                                        DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            // Pet interaction on Boot button tap!
            if (display_ && app.GetDeviceState() == kDeviceStateIdle) {
                static_cast<RobotEyesDisplay*>(display_)->TriggerPetReaction();
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    CompactWifiBoardLCD() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    // Keep Wi-Fi radio in continuous high-performance receive mode
    // to prevent packet loss and dropped words during audio playback
    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);
