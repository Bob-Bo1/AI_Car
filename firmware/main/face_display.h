#pragma once
#include "driver/i2c_master.h"
#include "face_engine.h"

typedef struct {
    bool running, healthy;
    face_kind_t kind;
    uint32_t frames, total_errors, consecutive_errors, last_success_ms;
    uint32_t frame_ms, stack_free;
    esp_err_t last_error;
} face_display_status_t;
esp_err_t face_display_start(i2c_master_dev_handle_t oled);
void face_display_status(face_display_status_t *out);
