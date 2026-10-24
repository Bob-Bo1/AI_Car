// Original procedural face. No external bitmap assets or animation dependencies.
#include "face_engine.h"
#include <string.h>

static int clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static uint32_t random_next(face_engine_t *e){uint32_t x=e->random;x^=x<<13;x^=x>>17;x^=x<<5;return e->random=x;}
static int smooth(int current,int target,uint32_t dt){return current+(target*256-current)*(int)dt/((int)dt+100);}
static int round_q8(int value){return (value+(value>=0?128:-128))/256;}
const char *face_kind_name(face_kind_t kind){
    static const char *const names[]={"boot","connecting","idle","forward","backward","left","right","test","sleepy","asleep","wifi_lost","control_lost","estop","fault"};
    return (unsigned)kind<sizeof(names)/sizeof(names[0])?names[kind]:"idle";
}
void face_engine_init(face_engine_t *e,uint32_t now){
    memset(e,0,sizeof(*e));e->boot_ms=e->last_ms=now;e->next_blink_ms=now+3500;
    e->random=now^0x6d2b79f5U;if(!e->random)e->random=1;
    e->width_q8=34*256;e->height_q8=26*256;e->pose.kind=FACE_BOOT;
}
void face_engine_step(face_engine_t *e,const face_input_t *in,uint32_t now){
    uint32_t dt=now-e->last_ms;if(dt>250)dt=250;e->last_ms=now;
    if(in->connected)e->ever_connected=true;
    face_kind_t kind=FACE_IDLE;
    if(in->fault)kind=FACE_FAULT;
    else if(in->locked)kind=FACE_ESTOP;
    else if(!in->connected&&e->ever_connected)kind=FACE_WIFI_LOST;
    else if(now-e->boot_ms<1200)kind=FACE_BOOT;
    else if(!in->connected)kind=FACE_CONNECTING;
    else if(in->control_lost&&now-in->reason_at_ms<1500)kind=FACE_CONTROL_LOST;
    else if(in->active){
        if(in->testing)kind=FACE_TEST;
        else if(in->steering<-5)kind=FACE_LEFT;
        else if(in->steering>5)kind=FACE_RIGHT;
        else kind=in->throttle<0?FACE_BACKWARD:FACE_FORWARD;
    }else if(now-in->interaction_ms>=90000)kind=FACE_ASLEEP;
    else if(now-in->interaction_ms>=60000)kind=FACE_SLEEPY;

    int width=34,height=26,gx=0,gy=0,lo=0,ro=0;
    switch(kind){
        case FACE_BOOT:height=now-e->boot_ms<250?2:28;break;
        case FACE_CONNECTING:gx=((int)((now/750)%3)-1)*3;break;
        case FACE_FORWARD:height=28;width=36;break;
        case FACE_BACKWARD:height=22;width=30;gy=3;break;
        case FACE_LEFT:case FACE_RIGHT:
            gx=clamp(in->steering,-100,100)*7/100;gy=in->throttle<0?3:0;
            height=in->throttle<0?22:26;lo=kind==FACE_LEFT?-1:1;ro=-lo;break;
        case FACE_TEST:height=24;break;
        case FACE_SLEEPY:height=10;break;
        case FACE_ASLEEP:height=2;break;
        case FACE_WIFI_LOST:case FACE_CONTROL_LOST:case FACE_FAULT:
            height=23;lo=-2;ro=2;break;
        case FACE_ESTOP:height=30;width=36;break;
        case FACE_IDLE:
            height=26+(int)((now/1200)%2);
            if((now/5000)%3==1)gx=2;else if((now/5000)%3==2)gx=-2;
            break;
    }
    bool may_blink=kind!=FACE_ESTOP&&kind!=FACE_FAULT&&kind!=FACE_ASLEEP&&kind!=FACE_BOOT;
    if((int32_t)(now-e->next_blink_ms)>=0){
        uint32_t elapsed=now-e->next_blink_ms;
        if(elapsed<125&&may_blink)height=2;
        else if(elapsed<250&&may_blink)height=height/2;
        else if(elapsed>=250)e->next_blink_ms=now+3000+random_next(e)%3001;
    }
    e->width_q8=smooth(e->width_q8,width,dt);e->height_q8=smooth(e->height_q8,height,dt);
    e->gaze_x_q8=smooth(e->gaze_x_q8,gx,dt);e->gaze_y_q8=smooth(e->gaze_y_q8,gy,dt);
    // A blink and a sleeping face must actually close, independent of interpolation.
    int drawn_height=height==2?2:(e->height_q8+128)/256;
    e->pose=(face_pose_t){.kind=kind,.eye_width=clamp((e->width_q8+128)/256,28,36),.eye_height=clamp(drawn_height,2,30),
        .gaze_x=round_q8(e->gaze_x_q8),.gaze_y=round_q8(e->gaze_y_q8),.left_offset=lo,.right_offset=ro,
        .contrast=kind==FACE_ASLEEP?0x25:kind==FACE_SLEEPY?0x55:0x7f};
}

static void pixel(uint8_t *f,int x,int y,bool on){
    if(x<0||x>=128||y<0||y>=64)return;
    if(on)f[x+(y/8)*128]|=(uint8_t)(1U<<(y%8));else f[x+(y/8)*128]&=(uint8_t)~(1U<<(y%8));
}
static void round_rect(uint8_t *f,int x,int y,int w,int h){
    int r=6;if(h/2<r)r=h/2;
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){
        int dx=i<r?r-i:i>=w-r?i-(w-r-1):0;
        int dy=j<r?r-j:j>=h-r?j-(h-r-1):0;
        if(dx*dx+dy*dy<=r*r)pixel(f,x+i,y+j,true);
    }
}
static void line(uint8_t *f,int x0,int y0,int x1,int y1){
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=-(y1>y0?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;
    while(1){pixel(f,x0,y0,true);if(x0==x1&&y0==y1)break;int e=err*2;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
static void eye(uint8_t *f,int x,int y,int w,int h){
    if(h<=4){line(f,x-w/2,y+1,x,y-1);line(f,x,y-1,x+w/2,y+1);return;}
    round_rect(f,x-w/2,y-h/2,w,h);
}
void face_render(uint8_t f[1024],const face_pose_t *p,const face_input_t *in){
    memset(f,0,1024);
    (void)in;
    int width=clamp(p->eye_width,28,36),height=clamp(p->eye_height,2,30);
    eye(f,42+p->gaze_x,31+p->gaze_y+p->left_offset,width,height);
    eye(f,86+p->gaze_x,31+p->gaze_y+p->right_offset,width,height);
}
