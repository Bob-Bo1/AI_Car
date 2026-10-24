#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t web_console_start(void);
bool web_console_owned(void);
