// Real ARM core integration probe. Writes only to the supplied isolated save directory.
#include "libretro.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>
#include <chrono>
static const char *systemPath,*savePath,*corePath;
static retro_keyboard_callback keyboard{};
static std::map<std::string,std::string> options;
static unsigned frame,shutdowns,width,height,format;
static std::vector<unsigned char> pixels;
static unsigned long long pcm,audible;
static bool fastMode;
static bool interactive;
static int heldPad=-1,heldKey=-1,mouseDX,mouseDY,heldMouse,axes[2][2];
static unsigned heldMask;
static void log(retro_log_level,const char *fmt,...) { va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a); }
static bool env(unsigned cmd,void *p) {
 switch(cmd) {
 case RETRO_ENVIRONMENT_GET_FASTFORWARDING:*(bool*)p=fastMode;return true;
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((retro_log_callback*)p)->log=log;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char**)p=systemPath;return true;
 case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)p=savePath;return true;
 case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:*(const char**)p=corePath;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE:{auto v=(retro_variable*)p;auto i=options.find(v->key);if(i==options.end())return false;v->value=i->second.c_str();return true;}
 case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:keyboard=*(retro_keyboard_callback*)p;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:format=*(unsigned*)p;return true;
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)p=2;return true;
 case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:*(unsigned*)p=3;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)p=false;return true;
 case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:*(float*)p=60;return true;
 case RETRO_ENVIRONMENT_SHUTDOWN:++shutdowns;return true;
 case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
 case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
 case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
 case RETRO_ENVIRONMENT_SET_GEOMETRY:
 case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:return true;
 default:return false;
 }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch) {
 if(!p)return;width=w;height=h;unsigned bpp=format==RETRO_PIXEL_FORMAT_XRGB8888?4:2;
 pixels.resize(w*h*bpp);for(unsigned y=0;y<h;y++)memcpy(pixels.data()+y*w*bpp,(const char*)p+y*pitch,w*bpp);
}
static size_t audio(const int16_t *p,size_t n){pcm+=n;for(size_t i=0;i<n*2;i++)audible+=p[i]!=0;return n;}
static void poll(){}
static int16_t input(unsigned,unsigned dev,unsigned idx,unsigned id) {
 if(interactive){
  if(dev==RETRO_DEVICE_JOYPAD)return int(id)==heldPad||(id<16&&(heldMask&(1u<<id)));
  if(dev==RETRO_DEVICE_KEYBOARD)return int(id)==heldKey;
  if(dev==RETRO_DEVICE_ANALOG&&idx<2&&id<2)return axes[idx][id];
  if(dev==RETRO_DEVICE_MOUSE){if(id==RETRO_DEVICE_ID_MOUSE_X)return mouseDX;if(id==RETRO_DEVICE_ID_MOUSE_Y)return mouseDY;if(id==RETRO_DEVICE_ID_MOUSE_LEFT)return heldMouse;}
  return 0;
 }
 if(dev==RETRO_DEVICE_MOUSE) {
  if(frame==1600&&id==RETRO_DEVICE_ID_MOUSE_X)return 24;
  if(frame==1600&&id==RETRO_DEVICE_ID_MOUSE_Y)return 64;
  if(frame>=1650&&frame<1655&&id==RETRO_DEVICE_ID_MOUSE_LEFT)return 1;
 }
 if(dev==RETRO_DEVICE_JOYPAD) {
  if(id==RETRO_DEVICE_ID_JOYPAD_L3)return frame>=2400&&frame<2405;
  if(id==RETRO_DEVICE_ID_JOYPAD_R3)return frame>=3000&&frame<3005;
  if(id==RETRO_DEVICE_ID_JOYPAD_L)return frame>=2700&&frame<2705;
 }
 if(dev==RETRO_DEVICE_ANALOG&&idx==0&&id==0&&frame>=2500&&frame<2600)return 24000;
 return 0;
}
static void shot(unsigned n) {
 char path[1024];snprintf(path,sizeof path,"%s/probe-%u.ppm",savePath,n);FILE *f=fopen(path,"wb");if(!f)return;
 fprintf(f,"P6\n%u %u\n255\n",width,height);
 for(size_t i=0;i<size_t(width)*height;i++) {
  unsigned c=0,r,g,b;if(format==RETRO_PIXEL_FORMAT_XRGB8888){memcpy(&c,pixels.data()+i*4,4);r=c>>16;g=c>>8;b=c;}
  else{uint16_t s;memcpy(&s,pixels.data()+i*2,2);r=((s>>11)&31)*255/31;g=((s>>5)&63)*255/63;b=(s&31)*255/31;}
  unsigned char rgb[]={uint8_t(r),uint8_t(g),uint8_t(b)};fwrite(rgb,1,3,f);
 }fclose(f);
}
#define API(n) auto p_##n=(decltype(&n))dlsym(lib,#n);if(!p_##n)return 3
int main(int argc,char **argv) {
 if(argc<6)return 2;fastMode=argc>6;interactive=argc>6&&!strcmp(argv[6],"--interactive");corePath=argv[1];systemPath=argv[2];savePath=argv[4];
 FILE *f=fopen(argv[5],"r");char line[1024],key[256],value[512];while(f&&fgets(line,sizeof line,f))if(sscanf(line,"%255s = \"%511[^\"]\"",key,value)==2)options[key]=value;if(f)fclose(f);
 options["scummvm_pointer_device"]="mouse";
 // Android's app process already owns these platform libraries. Loading them
 // in the core's initial dlopen group can pin that group in a shell process.
 dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);dlopen("libOpenSLES.so",RTLD_NOW|RTLD_LOCAL);
 bool lifecycle=argc>6&&!strcmp(argv[6],"--lifecycle");
 for(int cycle=0;cycle<(lifecycle?3:1);cycle++){
 frame=shutdowns=0;keyboard={};pixels.clear();
 void *lib=dlopen(corePath,RTLD_NOW);if(!lib){puts(dlerror());return 3;}
 if(getenv("UWTV_TEST")){auto boot=(void(*)())dlsym(lib,"uw1_test_start_new_game");if(boot)boot();}
 API(retro_set_environment);API(retro_set_video_refresh);API(retro_set_audio_sample_batch);API(retro_set_input_poll);API(retro_set_input_state);API(retro_init);API(retro_deinit);API(retro_load_game);API(retro_unload_game);API(retro_run);API(retro_reset);
 p_retro_set_environment(env);p_retro_set_video_refresh(video);p_retro_set_audio_sample_batch(audio);p_retro_set_input_poll(poll);p_retro_set_input_state(input);p_retro_init();
 retro_game_info info{argv[3],nullptr,0,nullptr};if(!p_retro_load_game(&info))return 4;
 if(lifecycle){
  int count=getenv("TV_LIFECYCLE_FRAMES")?atoi(getenv("TV_LIFECYCLE_FRAMES")):600;
  for(frame=0;frame<unsigned(count)&&!shutdowns;frame++)p_retro_run();
  if(shutdowns||(count&&!pixels.size()))return 6;
  p_retro_unload_game();
  if(p_retro_load_game(&info)){puts("ERROR: unsafe same-module reload accepted");return 7;}
  p_retro_deinit();
  if(dlclose(lib)){puts(dlerror());return 8;}
  Dl_info after{};printf("MODULE_MAPPED_AFTER_CLOSE=%d\n",dladdr((void*)p_retro_init,&after));
  printf("LIFECYCLE %d CLOSED frames=%u\n",cycle+1,frame);fflush(stdout);continue;
 }
 if(interactive){
  auto run=[&](int n){while(n-->0&&!shutdowns){p_retro_run();++frame;}};
  char cmd[128];puts("READY");fflush(stdout);
  while(fgets(cmd,sizeof cmd,stdin)){
   auto command_start=std::chrono::steady_clock::now();
   unsigned frame_start=frame;unsigned long long audible_start=audible;
   int a=0,b=0,c=0,d=0;
   if(sscanf(cmd,"run %d",&a)==1)run(a);
   else if(sscanf(cmd,"reset %d",&a)==1){p_retro_reset();run(a);}
   else if(sscanf(cmd,"key %d",&a)==1){heldKey=a;if(keyboard.callback)keyboard.callback(true,a,a,0);run(10);heldKey=-1;if(keyboard.callback)keyboard.callback(false,a,a,0);run(30);}
   else if(sscanf(cmd,"axis %d %d %d %d",&a,&b,&c,&d)==4&&a>=0&&a<2&&b>=0&&b<2){axes[a][b]=c;run(d);axes[a][b]=0;run(10);}
   else if(sscanf(cmd,"pads %d %d",&a,&b)==2){heldMask=(unsigned)a;run(b);heldMask=0;run(30);}
   else if(sscanf(cmd,"pad %d",&a)==1){heldPad=a;run(10);heldPad=-1;run(30);}
   else if(sscanf(cmd,"move %d %d",&a,&b)==2){mouseDX=a;mouseDY=b;run(1);mouseDX=mouseDY=0;run(20);}
   else if(!strncmp(cmd,"click",5)){heldMouse=1;run(5);heldMouse=0;run(30);}
   else if(sscanf(cmd,"ff %d",&a)==1)fastMode=a;
   else if(!strncmp(cmd,"quit",4))break;
   double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-command_start).count();
   shot(9999);printf("FRAME %u shutdown=%u seconds=%.3f speed=%.2fx PCM=%llu audible_delta=%llu\n",frame,shutdowns,elapsed,(frame-frame_start)/60.0/elapsed,pcm,audible-audible_start);fflush(stdout);
  }
  p_retro_unload_game();p_retro_deinit();dlclose(lib);puts("CLOSED");return 0;
 }
 auto start=std::chrono::steady_clock::now();
 for(frame=0;frame<3300&&!shutdowns;frame++) {
  unsigned k=0;bool down=false;
  if(frame==1000||frame==1010||frame==1100||frame==1110||frame==1300||frame==1310||frame==1400||frame==1410){k=RETROK_1;down=frame%100==0;}
  if(frame==1200||frame==1210||frame==1500||frame==1510){k=RETROK_RETURN;down=frame%100==0;}
  if(frame>=1700&&frame<=2300&&frame%100<2){k=RETROK_ESCAPE;down=frame%100==0;}
  if(k&&keyboard.callback)keyboard.callback(down,k,k,0);
  p_retro_run();
  if(frame==300||frame==500||frame==700||frame==900||frame==1800||frame==2900||frame==3200)shot(frame);
 }
 double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 p_retro_unload_game();p_retro_deinit();
 printf("frames=%u seconds=%.3f speed=%.2fx PCM=%llu audible=%llu shutdowns=%u\n",frame,secs,frame/60.0/secs,pcm,audible,shutdowns);
 return shutdowns?5:0;
 }
 return 0;
}
