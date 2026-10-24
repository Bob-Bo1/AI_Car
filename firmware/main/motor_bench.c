#include "motor_bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_timer.h"

// Sole PWM writer. Network silence stops outputs within 450ms + one task tick.
static const int pins[]={4,5,21,20};
static SemaphoreHandle_t mutex;
static motor_status_t state;
static TickType_t deadline, lease, reverse_ready[2], kick_until[2];
static int applied[2];
static bool network_motion, direct_output,maintenance;
static int64_t last_motion_us;
static TickType_t ticks(unsigned ms){return pdMS_TO_TICKS(ms);}
static bool expired(TickType_t now,TickType_t limit){return (int32_t)(now-limit)>=0;}
static void stop_locked(const char *reason,bool invalidate){
    if(state.active||strcmp(state.reason,reason)!=0)state.reason_at_ms=(uint32_t)(esp_timer_get_time()/1000);
    state.target_a=state.target_b=0;state.active=false;network_motion=false;deadline=0;
    state.throttle=state.steering=0;
    snprintf(state.mode,sizeof(state.mode),"idle");snprintf(state.reason,sizeof(state.reason),"%s",reason);
    if(invalidate)state.generation++;
}
static esp_err_t set_output(int motor,int value){
    int f=motor*2,r=f+1;
    esp_err_t err=ledc_stop(LEDC_LOW_SPEED_MODE,(ledc_channel_t)(value>=0?r:f),0);
    if(err!=ESP_OK)return err;
    if(value==0)return ledc_stop(LEDC_LOW_SPEED_MODE,(ledc_channel_t)f,0);
    ledc_channel_t ch=(ledc_channel_t)(value>0?f:r);
    err=ledc_set_duty(LEDC_LOW_SPEED_MODE,ch,(unsigned)(value>0?value:-value));
    return err==ESP_OK?ledc_update_duty(LEDC_LOW_SPEED_MODE,ch):err;
}
static void worker(void *arg){
    while(1){
        xSemaphoreTake(mutex,portMAX_DELAY);TickType_t now=xTaskGetTickCount();
        if(state.active&&network_motion&&expired(now,lease)){
            stop_locked("heartbeat_timeout",true);printf("BENCH MOTOR_STOP reason=heartbeat_timeout outputs=low\n");
        }else if(state.active&&deadline&&expired(now,deadline)){
            stop_locked("test_complete",false);printf("BENCH MOTOR_STOP reason=timeout outputs=low\n");
        }
        int desired[]={state.target_a,state.target_b};bool invert[]={state.invert_a,state.invert_b};
        for(int i=0;i<2;i++){
            int goal=invert[i]?-desired[i]:desired[i];if(state.locked||state.fault)goal=0;
            int next=applied[i];
            if(goal==0||(next&&((next>0)!=(goal>0)))){
                if(next)reverse_ready[i]=now+ticks(100);
                next=0;kick_until[i]=0;
            }else if(expired(now,reverse_ready[i])){
                if(next==0&&goal)kick_until[i]=now+ticks(100);
                int target=direct_output?goal:(!expired(now,kick_until[i])?(goal>0?255:-255):goal);
                if(direct_output||!expired(now,kick_until[i]))next=target;
                else if(next<target)next=(next+8>target)?target:next+8;
                else if(next>target)next=(next-8<target)?target:next-8;
            }else next=0;
            esp_err_t err=set_output(i,next);
            if(err!=ESP_OK){
                state.fault=state.locked=true;stop_locked("pwm_error",true);
                set_output(0,0);set_output(1,0);applied[0]=applied[1]=0;
                printf("BENCH MOTOR_ERROR result=%s outputs=low locked=true\n",esp_err_to_name(err));break;
            }
            applied[i]=next;
        }
        state.output_a=applied[0];state.output_b=applied[1];
        if(!state.active&&!applied[0]&&!applied[1]&&last_motion_us&&state.stop_elapsed_ms<0)
            state.stop_elapsed_ms=(int)((esp_timer_get_time()-last_motion_us)/1000);
        xSemaphoreGive(mutex);vTaskDelay(ticks(10));
    }
}
esp_err_t motor_bench_init(void){
    mutex=xSemaphoreCreateMutex();if(!mutex)return ESP_ERR_NO_MEM;
    state.generation=1;snprintf(state.mode,sizeof(state.mode),"idle");snprintf(state.reason,sizeof(state.reason),"boot");
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_8_BIT,.timer_num=LEDC_TIMER_0,.freq_hz=20000,.clk_cfg=LEDC_AUTO_CLK};
    esp_err_t err=ledc_timer_config(&timer);if(err!=ESP_OK)return err;
    for(int i=0;i<4;i++){
        ledc_channel_config_t ch={.gpio_num=pins[i],.speed_mode=LEDC_LOW_SPEED_MODE,.channel=(ledc_channel_t)i,.timer_sel=LEDC_TIMER_0,.duty=0,.hpoint=0};
        err=ledc_channel_config(&ch);if(err!=ESP_OK)return err;
    }
    set_output(0,0);set_output(1,0);
    if(xTaskCreate(worker,"motor_safe",4096,NULL,10,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    printf("BENCH MOTOR_READY in1=GPIO4 in2=GPIO5 in3=GPIO21 in4=GPIO20 watchdog_ms=450 reverse_ms=100 idle=low\n");return ESP_OK;
}
void motor_get_status(motor_status_t *out){xSemaphoreTake(mutex,portMAX_DELAY);*out=state;xSemaphoreGive(mutex);}
void motor_note_interaction(void){xSemaphoreTake(mutex,portMAX_DELAY);state.interaction_ms=(uint32_t)(esp_timer_get_time()/1000);xSemaphoreGive(mutex);}
void motor_stop_reason(const char *reason,bool invalidate){if(!mutex)return;xSemaphoreTake(mutex,portMAX_DELAY);if(strcmp(reason,"command")==0||strcmp(reason,"released_control")==0||strcmp(reason,"released")==0)state.interaction_ms=(uint32_t)(esp_timer_get_time()/1000);stop_locked(reason,invalidate);xSemaphoreGive(mutex);}
void motor_bench_stop(void){motor_stop_reason("command",false);}
bool motor_bench_active(void){motor_status_t s;motor_get_status(&s);return s.active||s.output_a||s.output_b;}
void motor_emergency_stop(void){xSemaphoreTake(mutex,portMAX_DELAY);state.locked=true;stop_locked("emergency_stop",true);xSemaphoreGive(mutex);}
bool motor_unlock(void){xSemaphoreTake(mutex,portMAX_DELAY);bool ok=!maintenance&&!state.fault&&!state.active&&!state.output_a&&!state.output_b;if(ok){state.locked=false;state.interaction_ms=(uint32_t)(esp_timer_get_time()/1000);snprintf(state.reason,sizeof(state.reason),"unlocked");}xSemaphoreGive(mutex);return ok;}
static bool test(int motor,int direction,unsigned duty,unsigned ms,bool network,bool direct){
    if((motor!=0&&motor!=1)||(direction!=1&&direction!=-1)||duty<1||duty>255||ms<100||ms>8000)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool ok=!state.locked&&!state.fault&&!state.active&&!state.output_a&&!state.output_b;
    if(ok){
        state.throttle=state.steering=0;state.interaction_ms=(uint32_t)(esp_timer_get_time()/1000);
        state.target_a=motor==0?direction*(int)duty:0;state.target_b=motor==1?direction*(int)duty:0;
        state.active=true;network_motion=network;direct_output=direct;last_motion_us=esp_timer_get_time();state.stop_elapsed_ms=-1;
        TickType_t now=xTaskGetTickCount();deadline=now+ticks(ms);lease=now+ticks(450);state.command_id++;
        snprintf(state.mode,sizeof(state.mode),"test");snprintf(state.reason,sizeof(state.reason),"running");
        printf("BENCH MOTOR_START motor=%c direction=%s duty=%u duration_ms=%u network=%s\n",'A'+motor,direction>0?"FWD":"REV",duty,ms,network?"true":"false");
    }
    xSemaphoreGive(mutex);return ok;
}
bool motor_network_test(int motor,int direction,unsigned ms){return (ms==300||ms==2000)&&test(motor,direction,255,ms,true,true);}
bool motor_network_keep(uint32_t id){xSemaphoreTake(mutex,portMAX_DELAY);bool ok=state.active&&network_motion&&strcmp(state.mode,"test")==0&&state.command_id==id;if(ok){lease=xTaskGetTickCount()+ticks(450);last_motion_us=esp_timer_get_time();}xSemaphoreGive(mutex);return ok;}
bool motor_drive_axes(int throttle,int steering,unsigned speed){
    if(throttle<-100||throttle>100||steering<-100||steering>100||speed<50||speed>100)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool ok=!state.locked&&!state.fault&&(!state.active||strcmp(state.mode,"drive")==0);
    if(ok){
        state.throttle=throttle;state.steering=steering;state.interaction_ms=(uint32_t)(esp_timer_get_time()/1000);
        // Reverse only longitudinal driving; turn sense and single-motor tests stay independent.
        if(state.reverse_drive)throttle=-throttle;
        int left=throttle+steering,right=throttle-steering;
        int scale=abs(left)>abs(right)?abs(left):abs(right);if(scale<100)scale=100;
        left=left*(int)speed*255/(scale*100);right=right*(int)speed*255/(scale*100);
        state.target_a=state.swap_ab?right:left;state.target_b=state.swap_ab?left:right;
        if(!left&&!right)stop_locked("released_control",false);
        else {
            state.active=true;network_motion=true;direct_output=false;deadline=0;lease=xTaskGetTickCount()+ticks(450);last_motion_us=esp_timer_get_time();state.stop_elapsed_ms=-1;
            snprintf(state.mode,sizeof(state.mode),"drive");snprintf(state.reason,sizeof(state.reason),"running");
        }
    }
    xSemaphoreGive(mutex);return ok;
}
bool motor_drive(const char *action,unsigned speed){
    if(strcmp(action,"forward")==0)return motor_drive_axes(100,0,speed);
    if(strcmp(action,"backward")==0)return motor_drive_axes(-100,0,speed);
    if(strcmp(action,"left")==0)return motor_drive_axes(0,-100,speed);
    if(strcmp(action,"right")==0)return motor_drive_axes(0,100,speed);
    return false;
}
bool motor_bench_run(int direction,unsigned duty,unsigned ms){return duty<=128&&ms<=1000&&test(0,direction,duty,ms,false,false);}
bool motor_bench_measure(int direction){return test(0,direction,128,8000,false,false);}
bool motor_bench_start_test(int direction){return motor_bench_start_test_motor(0,direction,300);}
bool motor_bench_start_test_motor(int motor,int direction,unsigned ms){return (ms==300||ms==2000)&&test(motor,direction,255,ms,false,true);}
esp_err_t motor_config_load(void){
    nvs_handle_t h;esp_err_t err=nvs_open("webcar",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    uint8_t flags=0;err=nvs_get_u8(h,"directions",&flags);nvs_close(h);
    if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)return err;
    xSemaphoreTake(mutex,portMAX_DELAY);state.invert_a=flags&1;state.invert_b=flags&2;state.swap_ab=flags&4;state.reverse_drive=flags&8;xSemaphoreGive(mutex);return ESP_OK;
}
bool motor_config_save(bool a,bool b,bool swap,bool reverse){
    xSemaphoreTake(mutex,portMAX_DELAY);if(state.active||state.output_a||state.output_b){xSemaphoreGive(mutex);return false;}
    nvs_handle_t h;esp_err_t err=nvs_open("webcar",NVS_READWRITE,&h);
    uint8_t flags=(a?1:0)|(b?2:0)|(swap?4:0)|(reverse?8:0);
    if(err==ESP_OK){err=nvs_set_u8(h,"directions",flags);if(err==ESP_OK)err=nvs_commit(h);uint8_t readback=255;if(err==ESP_OK)err=nvs_get_u8(h,"directions",&readback);if(err==ESP_OK&&readback!=flags)err=ESP_FAIL;nvs_close(h);}
    if(err==ESP_OK){state.invert_a=a;state.invert_b=b;state.swap_ab=swap;state.reverse_drive=reverse;}xSemaphoreGive(mutex);return err==ESP_OK;
}

bool motor_maintenance_begin(void){
    xSemaphoreTake(mutex,portMAX_DELAY);bool ok=!state.active&&!state.locked&&!state.fault;
    if(ok){stop_locked("firmware_update",true);state.locked=true;maintenance=true;}
    xSemaphoreGive(mutex);return ok;
}
void motor_maintenance_end(void){xSemaphoreTake(mutex,portMAX_DELAY);maintenance=false;if(!state.fault)state.locked=false;xSemaphoreGive(mutex);}
