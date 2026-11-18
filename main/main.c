#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

LV_FONT_DECLARE(rush_font_16);
LV_IMAGE_DECLARE(rush_cover);

#define INPUT_QUEUE_DEPTH 8
#define LANE_COUNT 5
#define MAX_OBJECTS 9
#define PLAYER_Y 252
#define SPAWN_Y 50
#define HIT_Y_MIN 230
#define HIT_Y_MAX 270
#define GAME_TICK_MS 35
#define SPAWN_INTERVAL_MS 620
#define OBJECT_SPEED_PX_PER_SEC 94
#define BACKLIGHT_ACTIVE_PERCENT 100

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

typedef enum {
    GAME_MENU = 0,
    GAME_PLAY,
    GAME_PAUSED,
    GAME_END,
} game_mode_t;

typedef enum {
    OBJ_NONE = 0,
    OBJ_NUMBER,
    OBJ_BAD_NUMBER,
    OBJ_BARRIER,
} object_type_t;

typedef struct {
    object_type_t type;
    int lane;
    int32_t value;
    int y;
    bool active;
    bool hit;
} runner_object_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *cover;
    lv_obj_t *score;
    lv_obj_t *battery;
    lv_obj_t *status;
    lv_obj_t *hint;
    lv_obj_t *player;
    lv_obj_t *objects[MAX_OBJECTS];
    lv_timer_t *timer;
} game_ui_t;

static const char *TAG = "number_rush";
static const int LANE_X[LANE_COUNT] = { 24, 72, 120, 168, 216 };

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;
static bool s_battery_ready;
static game_ui_t s_ui;
static runner_object_t s_objects[MAX_OBJECTS];
static game_mode_t s_mode = GAME_MENU;
static int s_player_lane = 2;
static int32_t s_score = 2;
static uint32_t s_number_index;
static uint32_t s_barrier_index;
static bool s_barrier_pending;
static int s_group_bad_slot = -1;
static uint32_t s_spawn_elapsed_ms;
static void reset_game(void);

static void on_start_clicked(lv_event_t *event)
{
    (void)event;
    reset_game();
}

static int32_t barrier_value(uint32_t index)
{
    if (index < 100) {
        return (int32_t)((index + 1u) * 10u);
    }
    if (index < 118) {
        return (int32_t)(1500u + (index - 100u) * 500u);
    }
    if (index < 158) {
        return (int32_t)(11000u + (index - 118u) * 1000u);
    }
    if (index < 168) {
        return (int32_t)(55000u + (index - 158u) * 5000u);
    }
    return 100000;
}

static bool fixed_number(uint32_t index, int32_t *value)
{
    switch (index) {
    case 0: *value = 2; return true;
    case 1: *value = 5; return true;
    case 2: *value = 10; return true;
    case 3: *value = 1; return true;
    case 4: *value = 20; return true;
    case 38: *value = 1; return true;
    case 120: *value = 60; return true;
    case 300: *value = 1000; return true;
    case 430: *value = 1500 + (int32_t)(esp_random() % 201u); return true;
    case 610: *value = 2222; return true;
    case 780: *value = 6666; return true;
    default: return false;
    }
}

static int32_t number_value(int32_t barrier, uint32_t slot)
{
    if (barrier >= 10000) {
        return 15001 + (int32_t)(esp_random() % 5000u);
    }
    if (barrier >= 1000) {
        return 1001 + (int32_t)(esp_random() % 1000u);
    }
    const uint32_t base = 11u + (uint32_t)(barrier / 90) + slot * 2u;
    const uint32_t swing = 7u + (uint32_t)(barrier / 180);
    return (int32_t)(base + (esp_random() % (swing > 0 ? swing : 1u)));
}

static int32_t bad_number_value(int32_t barrier)
{
    if (barrier >= 10000) {
        return 1 + (int32_t)(esp_random() % 99u);
    }
    if (barrier >= 1000) {
        return 1 + (int32_t)(esp_random() % 29u);
    }
    uint32_t value = 6u + (uint32_t)(barrier / 120) + (esp_random() % 12u);
    if (value > 9999u) {
        value = 9999u;
    }
    return (int32_t)value;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static void object_label_style(lv_obj_t *label)
{
    lv_obj_set_width(label, 48);
    lv_obj_set_height(label, 28);
    lv_obj_set_style_radius(label, 6, 0);
    lv_obj_set_style_pad_top(label, 3, 0);
    lv_obj_set_style_pad_bottom(label, 3, 0);
    lv_obj_set_style_pad_left(label, 2, 0);
    lv_obj_set_style_pad_right(label, 2, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create(void)
{
    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(0x071421), 0);
    lv_obj_set_style_pad_all(s_ui.screen, 0, 0);

    lv_obj_t *title = make_label(s_ui.screen, &lv_font_montserrat_20, lv_color_hex(0xFFE16A));
    lv_label_set_text(title, "NUMBER RUSH");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_ui.score = make_label(s_ui.screen, &lv_font_montserrat_14, lv_color_hex(0x66F2FF));
    lv_obj_set_width(s_ui.score, 128);
    lv_obj_align(s_ui.score, LV_ALIGN_TOP_LEFT, 6, 34);

    s_ui.battery = make_label(s_ui.screen, &lv_font_montserrat_14, lv_color_hex(0xE7EEF7));
    lv_obj_set_width(s_ui.battery, 58);
    lv_obj_align(s_ui.battery, LV_ALIGN_TOP_RIGHT, -6, 36);

    lv_obj_t *road = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(road);
    lv_obj_set_size(road, 226, 214);
    lv_obj_align(road, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(road, lv_color_hex(0x162B4D), 0);
    lv_obj_set_style_bg_opa(road, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(road, 12, 0);
    lv_obj_set_style_border_width(road, 2, 0);
    lv_obj_set_style_border_color(road, lv_color_hex(0x4B69A5), 0);

    for (int i = 1; i < LANE_COUNT; i++) {
        lv_obj_t *line = lv_obj_create(s_ui.screen);
        lv_obj_remove_style_all(line);
        lv_obj_set_size(line, 1, 208);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x345175), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
        lv_obj_set_pos(line, LANE_X[i] - 24, 61);
    }

    for (int i = 0; i < MAX_OBJECTS; i++) {
        s_ui.objects[i] = make_label(s_ui.screen, &lv_font_montserrat_14, lv_color_hex(0xFFFFFF));
        object_label_style(s_ui.objects[i]);
    }

    s_ui.player = make_label(s_ui.screen, &lv_font_montserrat_20, lv_color_hex(0x071421));
    lv_obj_set_width(s_ui.player, 44);
    lv_obj_set_height(s_ui.player, 34);
    lv_obj_set_style_radius(s_ui.player, 10, 0);
    lv_obj_set_style_bg_color(s_ui.player, lv_color_hex(0x66F2FF), 0);
    lv_obj_set_style_bg_opa(s_ui.player, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(s_ui.player, 3, 0);
    lv_label_set_text(s_ui.player, "2");

    lv_obj_t *footer = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, 240, 44);
    lv_obj_set_pos(footer, 0, 276);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x071421), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);

    s_ui.status = make_label(footer, &rush_font_16, lv_color_hex(0xFFFFFF));
    lv_obj_set_size(s_ui.status, 224, 20);
    lv_label_set_long_mode(s_ui.status, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_ui.status, LV_ALIGN_TOP_MID, 0, 0);

    s_ui.hint = make_label(footer, &rush_font_16, lv_color_hex(0xFFFFFF));
    lv_obj_set_size(s_ui.hint, 230, 20);
    lv_label_set_long_mode(s_ui.hint, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_ui.hint, LV_ALIGN_TOP_MID, 0, 22);

    s_ui.cover = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.cover);
    lv_obj_set_size(s_ui.cover, 240, 320);
    lv_obj_remove_flag(s_ui.cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_ui.cover, lv_color_hex(0x071421), 0);
    lv_obj_set_style_bg_opa(s_ui.cover, LV_OPA_COVER, 0);
    lv_obj_t *art = lv_image_create(s_ui.cover);
    lv_image_set_src(art, &rush_cover);
    lv_obj_set_pos(art, 0, 0);
    lv_obj_t *start = lv_button_create(s_ui.cover);
    lv_obj_set_size(start, 200, 40);
    lv_obj_set_pos(start, 20, 268);
    lv_obj_set_style_radius(start, 6, 0);
    lv_obj_set_style_bg_color(start, lv_color_hex(0xFFD447), 0);
    lv_obj_add_event_cb(start, on_start_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *caption = make_label(start, &rush_font_16, lv_color_hex(0x071421));
    lv_label_set_text(caption, "OK  开始游戏");
    lv_obj_center(caption);
    lv_screen_load(s_ui.screen);
}

static void ui_update_header(void)
{
    lv_label_set_text_fmt(s_ui.score, "NUM %ld", (long)s_score);
    if (s_battery_ready) {
        int soc = bsp_battery_soc();
        lv_label_set_text_fmt(s_ui.battery, soc >= 0 ? "%d%%" : "--%%", soc);
    } else {
        lv_label_set_text(s_ui.battery, "--%");
    }
}

static void ui_update_player(void)
{
    lv_obj_set_pos(s_ui.player, LANE_X[s_player_lane] - 22, PLAYER_Y - 17);
    lv_label_set_text_fmt(s_ui.player, "%ld", (long)s_score);
}

static void ui_update_object(int index)
{
    runner_object_t *obj = &s_objects[index];
    lv_obj_t *label = s_ui.objects[index];
    if (!obj->active) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    if (obj->type == OBJ_NUMBER) {
        lv_obj_set_width(label, 48);
        lv_obj_set_style_bg_color(label, lv_color_hex(0x38D9FF), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x07324D), 0);
        lv_label_set_text_fmt(label, "+%ld", (long)obj->value);
        lv_obj_set_pos(label, LANE_X[obj->lane] - 24, obj->y);
    } else if (obj->type == OBJ_BAD_NUMBER) {
        lv_obj_set_width(label, 48);
        lv_obj_set_style_bg_color(label, lv_color_hex(0xFF4768), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text_fmt(label, "-%ld", (long)obj->value);
        lv_obj_set_pos(label, LANE_X[obj->lane] - 24, obj->y);
    } else if (obj->type == OBJ_BARRIER) {
        lv_obj_set_width(label, 184);
        lv_obj_set_style_bg_color(label, lv_color_hex(0xFFD447), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x2E2200), 0);
        lv_label_set_text_fmt(label, "%ld", (long)obj->value);
        lv_obj_set_pos(label, 28, obj->y);
    }
}

static int free_object_slot(void)
{
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (!s_objects[i].active) {
            return i;
        }
    }
    return -1;
}

static void choose_bad_slot_for_group(void)
{
    int candidates[LANE_COUNT];
    int count = 0;
    for (int slot = 0; slot < LANE_COUNT; slot++) {
        int32_t fixed = 0;
        if (!fixed_number(s_number_index + (uint32_t)slot, &fixed)) {
            candidates[count++] = slot;
        }
    }
    s_group_bad_slot = count > 0 ? candidates[esp_random() % (uint32_t)count] : -1;
}

static void spawn_number(void)
{
    int slot = free_object_slot();
    if (slot < 0 || s_barrier_index >= 168) {
        return;
    }

    uint32_t group_pos = s_number_index % LANE_COUNT;
    if (group_pos == 0) {
        choose_bad_slot_for_group();
    }

    runner_object_t *obj = &s_objects[slot];
    obj->active = true;
    obj->hit = false;
    obj->y = SPAWN_Y;
    obj->lane = (int)(esp_random() % LANE_COUNT);

    int32_t fixed = 0;
    const int32_t barrier = barrier_value(s_barrier_index);
    const bool is_fixed = fixed_number(s_number_index, &fixed);
    if (!is_fixed && (int)group_pos == s_group_bad_slot) {
        obj->type = OBJ_BAD_NUMBER;
        obj->value = bad_number_value(barrier);
    } else {
        obj->type = OBJ_NUMBER;
        obj->value = is_fixed ? fixed : number_value(barrier, group_pos);
        if ((barrier >= 10000 && obj->value <= 15000) ||
            (barrier >= 1000 && obj->value <= 1000)) {
            obj->value = number_value(barrier, group_pos);
        }
    }
    s_number_index++;
    s_barrier_pending = (s_number_index % LANE_COUNT) == 0;
}

static void spawn_barrier(void)
{
    int slot = free_object_slot();
    if (slot < 0 || s_barrier_index >= 168) {
        return;
    }
    runner_object_t *obj = &s_objects[slot];
    obj->type = OBJ_BARRIER;
    obj->lane = 2;
    obj->value = barrier_value(s_barrier_index++);
    s_barrier_pending = false;
    obj->y = SPAWN_Y;
    obj->active = true;
    obj->hit = false;
}

static void reset_game(void)
{
    lv_obj_add_flag(s_ui.cover, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < MAX_OBJECTS; i++) {
        s_objects[i].active = false;
        ui_update_object(i);
    }
    s_mode = GAME_PLAY;
    s_player_lane = 2;
    s_score = 2;
    s_number_index = 0;
    s_barrier_index = 0;
    s_barrier_pending = false;
    s_group_bad_slot = -1;
    s_spawn_elapsed_ms = 0;
    lv_label_set_text(s_ui.status, "游戏中");
    lv_label_set_text(s_ui.hint, "上/下换道  OK暂停");
    ui_update_header();
    ui_update_player();
}

static void end_game(const char *message)
{
    s_mode = GAME_END;
    lv_label_set_text(s_ui.status, message);
    lv_label_set_text(s_ui.hint, "OK 再玩一次");
}

static void handle_collision(runner_object_t *obj)
{
    if (obj->hit || obj->y < HIT_Y_MIN || obj->y > HIT_Y_MAX) {
        return;
    }
    if (obj->type == OBJ_BARRIER) {
        obj->hit = true;
        if (s_score > obj->value) {
            lv_label_set_text_fmt(s_ui.status, "冲破 %ld", (long)obj->value);
        } else {
            end_game("被卡住了");
        }
        obj->active = false;
        return;
    }
    if (obj->lane != s_player_lane) {
        return;
    }
    obj->hit = true;
    obj->active = false;
    if (obj->type == OBJ_NUMBER) {
        s_score += obj->value;
        lv_label_set_text_fmt(s_ui.status, "+%ld", (long)obj->value);
    } else if (obj->type == OBJ_BAD_NUMBER) {
        s_score -= obj->value;
        if (s_score < 1) {
            s_score = 1;
        }
        lv_label_set_text_fmt(s_ui.status, "-%ld", (long)obj->value);
    }
    ui_update_header();
    ui_update_player();
}

static void game_tick(lv_timer_t *timer)
{
    (void)timer;
    ui_update_header();
    if (s_mode != GAME_PLAY) {
        return;
    }

    s_spawn_elapsed_ms += GAME_TICK_MS;
    if (s_spawn_elapsed_ms >= SPAWN_INTERVAL_MS) {
        s_spawn_elapsed_ms = 0;
        if (s_barrier_pending) {
            spawn_barrier();
        } else {
            spawn_number();
        }
    }

    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (!s_objects[i].active) {
            ui_update_object(i);
            continue;
        }
        s_objects[i].y += (OBJECT_SPEED_PX_PER_SEC * GAME_TICK_MS) / 1000;
        handle_collision(&s_objects[i]);
        if (s_objects[i].y > 290) {
            s_objects[i].active = false;
        }
        ui_update_object(i);
        if (s_mode != GAME_PLAY) {
            return;
        }
    }

    if (s_barrier_index >= 168) {
        bool any_active = false;
        for (int i = 0; i < MAX_OBJECTS; i++) {
            any_active = any_active || s_objects[i].active;
        }
        if (!any_active) {
            end_game("极限突破成功");
        }
    }
}

static void game_handle_button(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_PRESS) {
        return;
    }
    if (btn == BSP_BTN_OK) {
        if (s_mode == GAME_PLAY) {
            s_mode = GAME_PAUSED;
            lv_label_set_text(s_ui.status, "已暂停");
            lv_label_set_text(s_ui.hint, "OK 继续");
        } else if (s_mode == GAME_PAUSED) {
            s_mode = GAME_PLAY;
            lv_label_set_text(s_ui.status, "游戏中");
            lv_label_set_text(s_ui.hint, "上/下换道  OK暂停");
        } else {
            reset_game();
        }
        return;
    }
    if (s_mode != GAME_PLAY) {
        return;
    }
    if (btn == BSP_BTN_UP && s_player_lane > 0) {
        s_player_lane--;
    } else if (btn == BSP_BTN_DOWN && s_player_lane < LANE_COUNT - 1) {
        s_player_lane++;
    }
    ui_update_player();
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            if (bsp_lvgl_lock(250)) {
                game_handle_button(input.btn, input.event);
                bsp_lvgl_unlock();
            }
        }
    }
}

static esp_err_t input_dispatch_init(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(input_task, "rush_input", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) {
        return;
    }
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "数字冲冲冲启动");
    bsp_i2c_init();
    s_battery_ready = (bsp_battery_init() == ESP_OK);

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    bsp_display_backlight(BACKLIGHT_ACTIVE_PERCENT);

    esp_err_t input_err = input_dispatch_init();
    esp_err_t button_err = input_err == ESP_OK
                         ? bsp_button_init(on_key, NULL)
                         : ESP_ERR_INVALID_STATE;
    if (input_err != ESP_OK || button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: input=%s button=%s",
                 esp_err_to_name(input_err), esp_err_to_name(button_err));
    }

    if (bsp_lvgl_lock(1000)) {
        ui_create();
        ui_update_header();
        ui_update_player();
        lv_label_set_text(s_ui.status, "数字冲冲冲");
        lv_label_set_text(s_ui.hint, "OK 开始  上/下换道");
        s_ui.timer = lv_timer_create(game_tick, GAME_TICK_MS, NULL);
        bsp_lvgl_unlock();
    }
    s_input_ready = button_err == ESP_OK;
}
