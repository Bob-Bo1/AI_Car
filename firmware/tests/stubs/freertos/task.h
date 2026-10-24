#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t delay);
void vTaskDelayUntil(TickType_t *wake,TickType_t period);
uint32_t uxTaskGetStackHighWaterMark(void *task);
int xTaskCreate(void (*fn)(void *),const char *name,uint32_t stack,void *arg,int priority,void *handle);
