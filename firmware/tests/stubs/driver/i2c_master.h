#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_dev_handle_t;
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t panel,const uint8_t *data,size_t len,int timeout);
