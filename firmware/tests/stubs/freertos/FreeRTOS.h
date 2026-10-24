#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((ms)/10)
void mock_enter(void *lock);
void mock_exit(void *lock);
#define portENTER_CRITICAL(p) mock_enter(p)
#define portEXIT_CRITICAL(p) mock_exit(p)
