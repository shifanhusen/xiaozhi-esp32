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
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <lvgl.h>
#include <string>
#include <algorithm>

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

#define TAG "RobotEyesDisplay"

class RobotEyesDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    static constexpr int NUM_MOUTH_BARS = 5;
    lv_obj_t* mouth_bars_[NUM_MOUTH_BARS] = {nullptr};
    lv_obj_t* status_label_ = nullptr;
    lv_timer_t* anim_timer_ = nullptr;

    std::string emotion_ = "neutral";
    uint32_t color_ = 0x00E5FF; // Neon Cyan
    
    int frame_count_ = 0;
    int blink_state_ = 0; // 0=open, 1=closing, 2=shut, 3=opening
    int glance_x_ = 0;
    int mouth_phase_ = 0;

    int base_w_ = 54;
    int base_h_ = 82;
    int base_y_ = 74;
    int base_lx_ = 48;
    int base_rx_ = 138;

public:
    RobotEyesDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                     int width, int height, int offset_x, int offset_y,
                     bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {}

    ~RobotEyesDisplay() {
        if (anim_timer_) {
            lv_timer_delete(anim_timer_);
            anim_timer_ = nullptr;
        }
    }

    virtual void SetupUI() override {
        DisplayLockGuard lock(this);
        auto screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        // Left Eye (Full-screen capsule)
        left_eye_ = lv_obj_create(screen);
        lv_obj_set_size(left_eye_, base_w_, base_h_);
        lv_obj_set_pos(left_eye_, base_lx_, base_y_);
        lv_obj_set_style_radius(left_eye_, base_w_ / 2, 0);
        lv_obj_set_style_bg_color(left_eye_, lv_color_hex(color_), 0);
        lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(left_eye_, 0, 0);
        lv_obj_set_scrollbar_mode(left_eye_, LV_SCROLLBAR_MODE_OFF);

        // Right Eye (Full-screen capsule)
        right_eye_ = lv_obj_create(screen);
        lv_obj_set_size(right_eye_, base_w_, base_h_);
        lv_obj_set_pos(right_eye_, base_rx_, base_y_);
        lv_obj_set_style_radius(right_eye_, base_w_ / 2, 0);
        lv_obj_set_style_bg_color(right_eye_, lv_color_hex(color_), 0);
        lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(right_eye_, 0, 0);
        lv_obj_set_scrollbar_mode(right_eye_, LV_SCROLLBAR_MODE_OFF);

        // 5-Bar Dynamic Voice Waveform / Mouth
        static const int bar_x_offsets[NUM_MOUTH_BARS] = {-30, -15, 0, 15, 30};
        for (int i = 0; i < NUM_MOUTH_BARS; i++) {
            mouth_bars_[i] = lv_obj_create(screen);
            lv_obj_set_size(mouth_bars_[i], 8, 8);
            lv_obj_set_pos(mouth_bars_[i], 116 + bar_x_offsets[i], 175);
            lv_obj_set_style_radius(mouth_bars_[i], 4, 0);
            lv_obj_set_style_bg_color(mouth_bars_[i], lv_color_hex(color_), 0);
            lv_obj_set_style_bg_opa(mouth_bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(mouth_bars_[i], 0, 0);
            lv_obj_set_scrollbar_mode(mouth_bars_[i], LV_SCROLLBAR_MODE_OFF);
        }

        // Subtitle / Status Text at bottom curve of the GC9A01 circle
        status_label_ = lv_label_create(screen);
        lv_obj_set_width(status_label_, 190);
        lv_obj_set_pos(status_label_, 25, 202);
        lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0x888888), 0);
        lv_label_set_text(status_label_, "");

        // 35ms animation timer (~28 FPS)
        anim_timer_ = lv_timer_create([](lv_timer_t* t) {
            auto self = static_cast<RobotEyesDisplay*>(lv_timer_get_user_data(t));
            if (self) self->UpdateAnimation();
        }, 35, this);

        setup_ui_called_ = true;
    }

    void UpdateAnimation() {
        frame_count_++;

        // Natural smooth blink cycle every ~120 frames (approx 4 seconds)
        if (blink_state_ == 0 && (frame_count_ % 120 == 0)) {
            blink_state_ = 1;
        }

        int cur_h = base_h_;
        if (blink_state_ == 1) { // closing
            cur_h = base_h_ / 3;
            blink_state_ = 2;
        } else if (blink_state_ == 2) { // shut
            cur_h = 4;
            blink_state_ = 3;
        } else if (blink_state_ == 3) { // opening
            cur_h = base_h_ * 2 / 3;
            blink_state_ = 4;
        } else if (blink_state_ == 4) { // fully open
            cur_h = base_h_;
            blink_state_ = 0;
        }

        // Periodic glance saccades
        int cycle = frame_count_ % 240;
        if (cycle >= 40 && cycle < 90) {
            glance_x_ = -12; // Glance left
        } else if (cycle >= 140 && cycle < 190) {
            glance_x_ = 12;  // Glance right
        } else {
            glance_x_ = 0;   // Forward
        }

        // Bob eyes slightly when speaking
        int eye_bob = 0;
        int bar_heights[NUM_MOUTH_BARS] = {6, 8, 10, 8, 6};

        if (emotion_ == "speaking") {
            mouth_phase_ = (mouth_phase_ + 1) % 16;
            static const int voice_pattern[16][5] = {
                { 6, 14, 28, 14,  6},
                { 8, 20, 24, 18,  8},
                {12, 26, 16, 22, 10},
                {16, 18, 10, 26, 14},
                {10, 12,  6, 18, 10},
                { 6, 16, 24, 14,  6},
                {14, 24, 30, 20, 10},
                {18, 28, 22, 16,  8},
                {12, 18, 14, 22, 12},
                { 8, 12, 26, 18,  8},
                {10, 22, 28, 14,  6},
                {14, 26, 20, 10,  6},
                {10, 18, 14, 18, 10},
                { 6, 12, 22, 16,  8},
                { 8, 16, 26, 20, 10},
                { 6, 10, 16, 12,  6}
            };
            for (int i = 0; i < NUM_MOUTH_BARS; i++) {
                bar_heights[i] = voice_pattern[mouth_phase_][i];
            }
            eye_bob = (bar_heights[2] > 18) ? -3 : 0;
        } else if (emotion_ == "thinking") {
            // Scanner sweep wave across mouth bars
            int step = (frame_count_ / 2) % 8;
            int active = (step > 4) ? (8 - step) : step;
            for (int i = 0; i < NUM_MOUTH_BARS; i++) {
                bar_heights[i] = (i == active) ? 22 : (abs(i - active) == 1 ? 12 : 5);
            }
        } else if (emotion_ == "listening") {
            // Attentive breathing waveform
            int wave = (frame_count_ / 3) % 6;
            int h = (wave > 3) ? (6 - wave) : wave;
            bar_heights[0] = 6 + h;
            bar_heights[1] = 10 + h * 2;
            bar_heights[2] = 16 + h * 3;
            bar_heights[3] = 10 + h * 2;
            bar_heights[4] = 6 + h;
        } else if (emotion_ == "happy") {
            // Cheerful smile curve
            bar_heights[0] = 6;
            bar_heights[1] = 12;
            bar_heights[2] = 18;
            bar_heights[3] = 12;
            bar_heights[4] = 6;
        } else if (emotion_ == "angry") {
            // Downward frown
            bar_heights[0] = 14;
            bar_heights[1] = 8;
            bar_heights[2] = 5;
            bar_heights[3] = 8;
            bar_heights[4] = 14;
        }

        // Apply geometry updates to eyes
        if (left_eye_ && right_eye_) {
            int eye_y = base_y_ + (base_h_ - cur_h) / 2 + eye_bob;
            lv_obj_set_size(left_eye_, base_w_, cur_h);
            lv_obj_set_pos(left_eye_, base_lx_ + glance_x_, eye_y);

            lv_obj_set_size(right_eye_, base_w_, cur_h);
            lv_obj_set_pos(right_eye_, base_rx_ + glance_x_, eye_y);
        }

        // Apply geometry updates to mouth waveform bars
        static const int bar_x_offsets[NUM_MOUTH_BARS] = {-30, -15, 0, 15, 30};
        for (int i = 0; i < NUM_MOUTH_BARS; i++) {
            if (mouth_bars_[i]) {
                int bh = bar_heights[i];
                int by = 175 - bh / 2;
                lv_obj_set_size(mouth_bars_[i], 8, bh);
                lv_obj_set_pos(mouth_bars_[i], 116 + bar_x_offsets[i], by);
            }
        }
    }

    virtual bool SetEmotion(const char* emotion) override {
        if (!emotion) return false;
        DisplayLockGuard lock(this);
        emotion_ = emotion;

        if (emotion_ == "listening") {
            color_ = 0x00FF66; // Bright Electric Green
            base_w_ = 58; base_h_ = 86; base_y_ = 72;
        } else if (emotion_ == "thinking") {
            color_ = 0xD500F9; // Cyber Magenta
            base_w_ = 50; base_h_ = 68; base_y_ = 80;
        } else if (emotion_ == "speaking") {
            color_ = 0xFFD600; // Warm Vivid Gold
            base_w_ = 54; base_h_ = 82; base_y_ = 74;
        } else if (emotion_ == "happy") {
            color_ = 0x76FF03; // Neon Lime
            base_w_ = 56; base_h_ = 46; base_y_ = 88;
        } else if (emotion_ == "sad") {
            color_ = 0x2979FF; // Cyber Blue
            base_w_ = 48; base_h_ = 66; base_y_ = 86;
        } else if (emotion_ == "angry") {
            color_ = 0xFF1744; // Fiery Neon Red
            base_w_ = 58; base_h_ = 50; base_y_ = 86;
        } else if (emotion_ == "sleepy") {
            color_ = 0x4FC3F7; // Soft Sky Blue
            base_w_ = 52; base_h_ = 18; base_y_ = 98;
        } else { // neutral / idle
            color_ = 0x00E5FF; // Neon Cyan
            base_w_ = 54; base_h_ = 82; base_y_ = 74;
        }

        lv_color_t col = lv_color_hex(color_);
        if (left_eye_ && right_eye_) {
            lv_obj_set_style_bg_color(left_eye_, col, 0);
            lv_obj_set_style_bg_color(right_eye_, col, 0);
            lv_obj_set_style_radius(left_eye_, base_w_ / 2, 0);
            lv_obj_set_style_radius(right_eye_, base_w_ / 2, 0);
        }

        for (int i = 0; i < NUM_MOUTH_BARS; i++) {
            if (mouth_bars_[i]) {
                lv_obj_set_style_bg_color(mouth_bars_[i], col, 0);
            }
        }
        return true;
    }

    // Prevents LcdDisplay::SetTheme() from dereferencing uninitialized status/battery/mute labels
    virtual void SetTheme(Theme* theme) override {
        DisplayLockGuard lock(this);
        current_theme_ = theme;
    }

    virtual bool SetTextFont(std::shared_ptr<LvglFont> text_font) override {
        DisplayLockGuard lock(this);
        if (status_label_ && text_font && text_font->font()) {
            lv_obj_set_style_text_font(status_label_, text_font->font(), 0);
        }
        return true;
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
