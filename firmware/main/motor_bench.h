#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    int target_a, target_b, output_a, output_b;
    int throttle, steering;
    bool active, locked, fault, invert_a, invert_b, swap_ab, reverse_drive;
    uint32_t generation, command_id;
    uint32_t interaction_ms, reason_at_ms;
    int stop_elapsed_ms;
    char mode[12], reason[32];
} motor_status_t;
esp_err_t motor_bench_init(void);
esp_err_t motor_config_load(void);
bool motor_config_save(bool invert_a, bool invert_b, bool swap_ab, bool reverse_drive);
void motor_get_status(motor_status_t *out);
void motor_note_interaction(void);
bool motor_drive(const char *action, unsigned speed);
bool motor_drive_axes(int throttle, int steering, unsigned speed);
bool motor_network_test(int motor, int direction, unsigned duration_ms);
bool motor_network_keep(uint32_t command_id);
void motor_stop_reason(const char *reason, bool invalidate);
void motor_emergency_stop(void);
bool motor_unlock(void);
bool motor_bench_run(int direction, unsigned duty, unsigned duration_ms);
bool motor_bench_measure(int direction);
bool motor_bench_start_test(int direction);
bool motor_bench_start_test_motor(int motor, int direction, unsigned duration_ms);
void motor_bench_stop(void);
bool motor_bench_active(void);

bool motor_maintenance_begin(void);
void motor_maintenance_end(void);
