/* Cooperative platform for Underworld Exhumed. The PC engine runs as native C. */
#include "libretro.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/stat.h>
#define MINICORO_IMPL
#include "minicoro.h"
static retro_environment_t env;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static retro_input_state_t input;
static mco_coro *main_co,*timer_co;
static PlatHooks hooks;
static uint64_t now_ns,main_wake,timer_wake;
static jmp_buf main_exit,timer_exit;
static int stopping,exit_status,shutdown_sent,loaded,session_started,mouse_lock;
static int (*timer_fn)(void*);static void *timer_arg;
static void (*audio_fill)(int16_t*,int);static int audio_rate=44100;
static char data_dir[1024],save_dir[1024];
static uint8_t indexed[640*480],palette[768],oldkeys[256];
static uint32_t rgb[640*480];
static int mouse_x=160,mouse_y=100,mouse_buttons;
static int quick_action,old_quick[2],old_look_button;
static int keyboard_open,keyboard_cell,keyboard_repeat,old_select,old_pad[4];
extern const unsigned char settings_font[256][16];
int uw2_tv_take_quick_action(void){int result=quick_action;quick_action=0;return result;}
extern int uw2_port_main(int,char**);
extern void tv_resources_begin(void),tv_resources_end(void);
/* The frontend owns process-wide signal handlers. */
void port_crash_handlers(void){}
void port_backtrace(void){fprintf(stderr,"Underworld: engine error\n");}

uint64_t plat_counter(void){
 static unsigned reads;
 /* VGA retrace polling spins on the hardware clock without port_idle().
  * Let the cooperative frontend advance that clock and the PIT coroutine. */
 if(main_co && mco_running()==main_co && (++reads&127)==0)plat_sleep_ns(500000);
 return now_ns;
}
uint64_t plat_counter_hz(void){return 1000000000ULL;}
void plat_sleep_ns(uint64_t ns){
 mco_coro *co=mco_running();
 if(co==timer_co){if(stopping)longjmp(timer_exit,1);timer_wake=now_ns+ns;mco_yield(co);if(stopping)longjmp(timer_exit,1);}
 else if(co==main_co){if(stopping)longjmp(main_exit,1);main_wake=now_ns+ns;mco_yield(co);if(stopping)longjmp(main_exit,1);}
}
void plat_game_exit(int status){exit_status=status;longjmp(main_exit,1);}
void plat_game_park(void){for(;;)plat_sleep_ns(10000000);}
static void timer_entry(mco_coro *co){(void)co;if(!setjmp(timer_exit))timer_fn(timer_arg);}
int plat_thread_start(const char *name,int(*fn)(void*),void *arg){
 (void)name;if(timer_co)return -1;timer_fn=fn;timer_arg=arg;
 mco_desc d=mco_desc_init(timer_entry,2*1024*1024);return mco_create(&timer_co,&d)==MCO_SUCCESS?0:-1;
}
int plat_run(const PlatConfig *cfg,const PlatHooks *h,int(*game)(void*),void *arg){(void)cfg;hooks=*h;return game(arg);}
int plat_audio_open(int rate,void(*fill)(int16_t*,int)){audio_rate=rate;audio_fill=fill;return 0;}
void plat_pointer_capture(int on){mouse_lock=on;}
void plat_set_mouse_lock(int on){mouse_lock=on;}
void plat_set_display(int fullscreen,int scale,int aspect,int integer_scale){(void)fullscreen;(void)scale;(void)aspect;(void)integer_scale;}
int plat_max_scale(int aspect){(void)aspect;return 1;}
void plat_key_byte(uint8_t sc){if(hooks.key)hooks.key(sc);}
void plat_pointer_event(const PlatPointer *p){if(hooks.pointer)hooks.pointer(p);}
void plat_window_click(float x,float y,int release){PlatPointer p={.type=PLAT_POINTER_DOWN,.x=x,.y=y,.button=1,.buttons=1,.absolute=1};plat_pointer_event(&p);if(release){p.type=PLAT_POINTER_UP;p.buttons=0;plat_pointer_event(&p);}}
void plat_message(int error,const char *title,const char *text){fprintf(stderr,"%s: %s (%d)\n",title,text,error);}
int plat_choose_folder(const char *title,const char *text,char *out,size_t len){(void)title;(void)text;(void)out;(void)len;return -1;}
const char *plat_base_dir(void){return data_dir;}
static void main_entry(mco_coro *co){
 (void)co;if(setjmp(main_exit))return;
 char *args[]={"uw2port","--data",data_dir,"--home",save_dir,"--sound","3,1","--no-recording","--enhance","skip-intro","--enhance","modern-keys","--enhance","mouse-look","--enhance","restore-at-rest",NULL};
 const char *replay=getenv("UW2_TEST_REPLAY");
 if(replay){const char *sound=getenv("UW2_TEST_SOUND");char *testargs[]={"uw2port","--data",data_dir,"--home",save_dir,"--sound",(char*)(sound?sound:"0,0"),"--replay",(char*)replay,NULL};exit_status=uw2_port_main(9,testargs);}
 else exit_status=uw2_port_main(16,args);
}
static int16_t key(unsigned device,unsigned index,unsigned id){return input?input(0,device,index,id):0;}
static const char keyboard_chars[]="1234567890qwertyuiopasdfghjkl zxcvbnm,./";
static unsigned scan_for(unsigned c){
 const char *letters="qwertyuiopasdfghjklzxcvbnm";
 const unsigned char scans[]={16,17,18,19,20,21,22,23,24,25,30,31,32,33,34,35,36,37,38,44,45,46,47,48,49,50};
 const char *p=strchr(letters,c);if(p&&c)return scans[p-letters];
 if(c>='1'&&c<='9')return c-'1'+2;if(c=='0')return 11;
 if(c==' ')return 57;if(c==',')return 51;if(c=='.')return 52;if(c=='/')return 53;
 return 0;
}
static void text_overlay(int w,int h,int x,int y,const char *s,uint32_t color){
 for(;*s;s++,x+=8)for(int j=0;j<16;j++)for(int i=0;i<8;i++)if((settings_font[(unsigned char)*s][j]>>(7-i)&1)&&x+i>=0&&x+i<w&&y+j>=0&&y+j<h)rgb[(y+j)*w+x+i]=color;
}
static void keyboard_overlay(int w,int h){
 if(!keyboard_open)return;int y=h-128;if(y<0)y=0;
 for(int j=y;j<h;j++)for(int i=0;i<w;i++)rgb[j*w+i]=0x101820;
 text_overlay(w,h,8,y+2,"A:Type X:Enter B:Back Y:Space",0xffffff);
 for(int n=0;n<(int)strlen(keyboard_chars);n++){int x=8+n%10*30,ky=y+25+n/10*22;char c[2]={keyboard_chars[n],0};if(n==keyboard_cell)for(int j=ky-2;j<ky+18&&j<h;j++)for(int i=x-3;i<x+18&&i<w;i++)rgb[j*w+i]=0x496797;text_overlay(w,h,x,ky,c,0xffffff);}
}
static void inputs(void){
 /* A deterministic recording owns every input stream. Test harness/controller
  * events must not leak into the engine between recorded key checkpoints. */
 if(getenv("UW2_TEST_REPLAY"))return;
 if(poll)poll();if(!hooks.key)return;
 int select=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_SELECT);
 if(select&&!old_select){keyboard_open=!keyboard_open;keyboard_repeat=0;}old_select=select;
 if(keyboard_open){
  for(int i=1;i<128;i++)if(oldkeys[i]){plat_key_byte(i|128);oldkeys[i]=0;}
  PlatPointer p={.type=PLAT_POINTER_UP,.button=1,.buttons=0};plat_pointer_event(&p);mouse_buttons=0;
  int dx=(key(RETRO_DEVICE_JOYPAD,0,7)||key(RETRO_DEVICE_ANALOG,0,0)>12000)-(key(RETRO_DEVICE_JOYPAD,0,6)||key(RETRO_DEVICE_ANALOG,0,0)<-12000);
  int dy=(key(RETRO_DEVICE_JOYPAD,0,5)||key(RETRO_DEVICE_ANALOG,0,1)>12000)-(key(RETRO_DEVICE_JOYPAD,0,4)||key(RETRO_DEVICE_ANALOG,0,1)<-12000);
  if(!dx&&!dy)keyboard_repeat=0;else if(keyboard_repeat++%12==0){int count=strlen(keyboard_chars);keyboard_cell=(keyboard_cell+dx+dy*10+count)%count;}
  unsigned ids[]={RETRO_DEVICE_ID_JOYPAD_B,RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_Y,RETRO_DEVICE_ID_JOYPAD_X};
  for(int i=0;i<4;i++){int down=key(RETRO_DEVICE_JOYPAD,0,ids[i]);if(down&&!old_pad[i]){unsigned sc=i==0?scan_for(keyboard_chars[keyboard_cell]):i==1?14:i==2?28:57;if(sc){plat_key_byte(sc);oldkeys[sc]=1;}if(i==2)keyboard_open=0;}old_pad[i]=down;}
  return;
 }
 memset(old_pad,0,sizeof old_pad);
 for(int i=0;i<2;i++){int down=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L3+i);if(down&&!old_quick[i])quick_action=i+1;old_quick[i]=down;}
 uint8_t keys[256]={0};
 struct K{unsigned key,scan;} map[]={{RETROK_ESCAPE,1},{RETROK_RETURN,28},{RETROK_SPACE,57},{RETROK_w,17},{RETROK_a,30},{RETROK_s,31},{RETROK_d,32},{RETROK_e,18},{RETROK_q,16},{RETROK_j,36},{RETROK_F2,60},{RETROK_F3,61},{RETROK_F5,63},{RETROK_F9,67},{RETROK_BACKSPACE,14},{RETROK_1,2},{RETROK_2,3},{RETROK_3,4},{RETROK_4,5}};
 for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++)keys[map[i].scan]=!!key(RETRO_DEVICE_KEYBOARD,0,map[i].key);
 for(unsigned c='a';c<='z';c++)keys[scan_for(c)]=!!key(RETRO_DEVICE_KEYBOARD,0,c);
 for(unsigned c='0';c<='9';c++)keys[scan_for(c)]=!!key(RETRO_DEVICE_KEYBOARD,0,c);
 keys[17]|=key(RETRO_DEVICE_ANALOG,0,1)<-8000;keys[31]|=key(RETRO_DEVICE_ANALOG,0,1)>8000;
 keys[30]|=key(RETRO_DEVICE_ANALOG,0,0)<-8000;keys[32]|=key(RETRO_DEVICE_ANALOG,0,0)>8000;
 keys[28]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B);keys[1]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START);
 keys[18]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y);keys[57]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2);
 /* Backtick toggles mouse-look. A held controller button must not reach the
  * engine's keyboard repeat path and toggle it back off repeatedly. */
 { int look_button=!!key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X);
   keys[41]|=look_button&&!old_look_button;old_look_button=look_button; }
 keys[42]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A);
 keys[44]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP);
 keys[16]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN);
 keys[19]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT);
 keys[33]|=key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT);
 for(unsigned i=1;i<128;i++)if(keys[i]!=oldkeys[i]){plat_key_byte(i|(keys[i]?0:128));oldkeys[i]=keys[i];}
 int dx=key(RETRO_DEVICE_ANALOG,1,0)/5000+key(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_X),dy=key(RETRO_DEVICE_ANALOG,1,1)/5000+key(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_Y);
 mouse_x+=dx;mouse_y+=dy;if(mouse_x<0)mouse_x=0;if(mouse_x>319)mouse_x=319;if(mouse_y<0)mouse_y=0;if(mouse_y>199)mouse_y=199;
 int pointer=key(RETRO_DEVICE_POINTER,0,RETRO_DEVICE_ID_POINTER_PRESSED);
 if(pointer&&!mouse_lock){mouse_x=((int)key(RETRO_DEVICE_POINTER,0,RETRO_DEVICE_ID_POINTER_X)+32767)*319/65534;mouse_y=((int)key(RETRO_DEVICE_POINTER,0,RETRO_DEVICE_ID_POINTER_Y)+32767)*199/65534;}
 int buttons=((key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L)||key(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_LEFT))?1:0)|((key(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)||key(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_RIGHT))?2:0);
 if(pointer&&!mouse_lock)buttons|=1;
 PlatPointer p={.type=PLAT_POINTER_MOVE,.x=mouse_x,.y=mouse_y,.dx=dx,.dy=dy,.buttons=buttons,.absolute=!mouse_lock};plat_pointer_event(&p);
 for(int b=1;b<=2;b*=2)if((buttons^mouse_buttons)&b){p.button=b;p.type=(buttons&b)?PLAT_POINTER_DOWN:PLAT_POINTER_UP;plat_pointer_event(&p);}mouse_buttons=buttons;
}
RETRO_API void retro_set_environment(retro_environment_t cb){env=cb;}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb){video=cb;}
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb){(void)cb;}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){audio=cb;}
RETRO_API void retro_set_input_poll(retro_input_poll_t cb){poll=cb;}
RETRO_API void retro_set_input_state(retro_input_state_t cb){input=cb;}
RETRO_API unsigned retro_api_version(void){return 1;}
RETRO_API void retro_get_system_info(struct retro_system_info *i){*i=(struct retro_system_info){"Underworld II Native","prototype","exe",true,false};}
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *i){*i=(struct retro_system_av_info){{320,200,640,480,4.0f/3.0f},{60,44100}};}
RETRO_API void retro_init(void){tv_resources_begin();}
RETRO_API bool retro_load_game(const struct retro_game_info *g){
 if(loaded||session_started||!g||!g->path)return false;snprintf(data_dir,sizeof data_dir,"%s",g->path);char *slash=strrchr(data_dir,'/');if(!slash)return false;*slash=0;
 const char *save=NULL;env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&save);if(!save)return false;snprintf(save_dir,sizeof save_dir,"%s",save);mkdir(save_dir,0700);
 enum retro_pixel_format fmt=RETRO_PIXEL_FORMAT_XRGB8888;if(!env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&fmt))return false;
 char cfg[1200],tmp[1210],line[1024];snprintf(cfg,sizeof cfg,"%s/uw2port.cfg",save_dir);snprintf(tmp,sizeof tmp,"%s.tmp",cfg);
 FILE *f=fopen(tmp,"w");if(!f)return false;
 FILE *previous=fopen(cfg,"r");if(previous){while(fgets(line,sizeof line,previous))if(strncmp(line,"settings-at-start=",18))fputs(line,f);fclose(previous);}
 fputs("settings-at-start=0\n",f);fclose(f);if(rename(tmp,cfg))return false;
 now_ns=main_wake=timer_wake=0;stopping=exit_status=shutdown_sent=0;quick_action=old_look_button=0;old_quick[0]=old_quick[1]=0;keyboard_open=keyboard_cell=keyboard_repeat=old_select=0;memset(old_pad,0,sizeof old_pad);mouse_x=160;mouse_y=100;mouse_buttons=mouse_lock=0;audio_fill=NULL;memset(&hooks,0,sizeof hooks);memset(oldkeys,0,sizeof oldkeys);
 mco_desc d=mco_desc_init(main_entry,8*1024*1024);if(mco_create(&main_co,&d)!=MCO_SUCCESS)return false;session_started=loaded=1;return true;
}
RETRO_API void retro_run(void){
 if(!loaded)return;inputs();
 uint64_t end=now_ns+16666667;
 while(now_ns<end){now_ns+=(end-now_ns<500000?end-now_ns:500000);if(timer_co&&mco_status(timer_co)==MCO_SUSPENDED&&now_ns>=timer_wake)mco_resume(timer_co);if(mco_status(main_co)==MCO_SUSPENDED&&now_ns>=main_wake)mco_resume(main_co);}
 if(hooks.tick)hooks.tick();int w=320,h=200;if(hooks.scanout)hooks.scanout(indexed,&w,&h,palette);
 if(w>0&&h>0&&w<=640&&h<=480){
  /* Convert the 256 colours once, rather than doing three DAC conversions
   * for every pixel of the software-rendered frame. */
  uint32_t colours[256];
  for(unsigned i=0;i<256;i++){unsigned c=i*3;colours[i]=(palette[c]*255/63)<<16|(palette[c+1]*255/63)<<8|(palette[c+2]*255/63);}
  for(int i=0;i<w*h;i++)rgb[i]=colours[indexed[i]];
  keyboard_overlay(w,h);if(video)video(rgb,w,h,w*4);
 }
 /* Exhume renders its music and speech ring at AUDIO_RATE=44100. */
 int16_t samples[1470]={0};if(audio_fill&&audio_rate==44100)audio_fill(samples,735);if(audio)audio(samples,735);
 if(mco_status(main_co)==MCO_DEAD&&!shutdown_sent){shutdown_sent=1;fprintf(stderr,"UW2 ended status=%d\n",exit_status);env(RETRO_ENVIRONMENT_SHUTDOWN,NULL);}
}
RETRO_API void retro_unload_game(void){if(!loaded)return;stopping=1;if(mco_status(main_co)==MCO_SUSPENDED)mco_resume(main_co);if(timer_co){if(mco_status(timer_co)==MCO_SUSPENDED)mco_resume(timer_co);mco_destroy(timer_co);timer_co=NULL;}mco_destroy(main_co);main_co=NULL;loaded=0;}
RETRO_API void retro_deinit(void){retro_unload_game();tv_resources_end();}
RETRO_API void retro_reset(void){}
RETRO_API void retro_set_controller_port_device(unsigned p,unsigned d){(void)p;(void)d;}
RETRO_API unsigned retro_get_region(void){return RETRO_REGION_NTSC;}
RETRO_API size_t retro_serialize_size(void){return 0;}
RETRO_API bool retro_serialize(void*p,size_t n){(void)p;(void)n;return false;}
RETRO_API bool retro_unserialize(const void*p,size_t n){(void)p;(void)n;return false;}
RETRO_API void retro_cheat_reset(void){}
RETRO_API void retro_cheat_set(unsigned i,bool e,const char*c){(void)i;(void)e;(void)c;}
RETRO_API bool retro_load_game_special(unsigned t,const struct retro_game_info*g,size_t n){(void)t;(void)g;(void)n;return false;}
RETRO_API void *retro_get_memory_data(unsigned i){(void)i;return NULL;}
RETRO_API size_t retro_get_memory_size(unsigned i){(void)i;return 0;}
