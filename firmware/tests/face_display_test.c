// Compile the actual ESP display adapter with only its I/O/task boundaries stubbed.
#include "face_display.h"
#include "motor_bench.h"
#include "wifi_manager.h"
#include "freertos/task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Run face tests with assertions enabled (-UNDEBUG)."
#endif

static int64_t now_us=1000000;
static int locks,steps,transfers,backoffs,reads;
static bool fail;
static void (*task_fn)(void *);
static jmp_buf done;
int64_t esp_timer_get_time(void){return now_us;}
const char *esp_err_to_name(esp_err_t err){return err==ESP_OK?"ESP_OK":"ESP_ERR_TIMEOUT";}
void mock_enter(void *lock){(void)lock;locks++;}
void mock_exit(void *lock){(void)lock;assert(locks>0);locks--;}
TickType_t xTaskGetTickCount(void){return (TickType_t)(now_us/10000);}
static void end_step(void){if(++steps==3)longjmp(done,1);}
void vTaskDelay(TickType_t delay){assert(locks==0);assert(delay==200);backoffs++;now_us+=(int64_t)delay*10000;end_step();}
void vTaskDelayUntil(TickType_t *wake,TickType_t period){assert(locks==0);*wake+=period;now_us=(int64_t)*wake*10000;end_step();}
uint32_t uxTaskGetStackHighWaterMark(void *task){(void)task;return 2000;}
int xTaskCreate(void (*fn)(void *),const char *name,uint32_t stack,void *arg,int priority,void *handle){
    (void)name;(void)arg;(void)handle;assert(stack==4096&&priority==2);task_fn=fn;return pdPASS;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t panel,const uint8_t *data,size_t len,int timeout){
    assert(panel&&data&&len<=129&&timeout==150);assert(locks==0);transfers++;
    now_us+=fail?150000:1000;return fail?ESP_ERR_TIMEOUT:ESP_OK;
}
void motor_get_status(motor_status_t *out){assert(locks==0);memset(out,0,sizeof(*out));out->interaction_ms=(uint32_t)(now_us/1000);reads++;}
void car_wifi_status(car_wifi_status_t *out){assert(locks==0);memset(out,0,sizeof(*out));out->connected=true;strcpy(out->ip,"255.255.255.255");}
int main(int argc,char **argv){
    assert(face_display_start((void*)1)==ESP_OK);assert(task_fn);int init_transfers=transfers;
    fail=argc>1&&strcmp(argv[1],"error")==0;
    if(setjmp(done)==0)task_fn(NULL);
    face_display_status_t h;face_display_status(&h);assert(locks==0&&h.running&&reads==3);
    if(fail){
        assert(!h.healthy&&h.frames==0&&h.total_errors==3&&h.consecutive_errors==3&&h.last_error==ESP_ERR_TIMEOUT);
        assert(backoffs==1&&transfers-init_transfers==3);assert(h.frame_ms==150);
        puts("PASS: actual display adapter exits each failed frame on first I2C error; backs off after three timeouts; no display lock held across I2C or state reads.");
    }else{
        assert(h.healthy&&h.frames==3&&h.total_errors==0&&h.last_success_ms&&h.stack_free==2000);
        assert(backoffs==0&&transfers-init_transfers==28);
        puts("PASS: actual display adapter publishes successful frames, timing, stack and health; uses one low-priority worker; no display lock held across I2C.");
    }
    return 0;
}
