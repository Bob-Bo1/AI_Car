#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FACE_BOOT, FACE_CONNECTING, FACE_IDLE, FACE_FORWARD, FACE_BACKWARD,
    FACE_LEFT, FACE_RIGHT, FACE_TEST, FACE_SLEEPY, FACE_ASLEEP,
    FACE_WIFI_LOST, FACE_CONTROL_LOST, FACE_ESTOP, FACE_FAULT
} face_kind_t;

typedef struct {
    bool connected, active, testing, locked, fault, control_lost;
    int throttle, steering;
    uint32_t interaction_ms, reason_at_ms;
    char ip[24];
} face_input_t;

typedef struct {
    face_kind_t kind;
    int eye_width, eye_height, gaze_x, gaze_y, left_offset, right_offset;
    uint8_t contrast;
} face_pose_t;

typedef struct {
    uint32_t boot_ms, last_ms, next_blink_ms, random;
    bool ever_connected;
    int width_q8, height_q8, gaze_x_q8, gaze_y_q8;
    face_pose_t pose;
} face_engine_t;

void face_engine_init(face_engine_t *engine, uint32_t now_ms);
void face_engine_step(face_engine_t *engine, const face_input_t *input, uint32_t now_ms);
const char *face_kind_name(face_kind_t kind);
void face_render(uint8_t frame[1024], const face_pose_t *pose, const face_input_t *input);
