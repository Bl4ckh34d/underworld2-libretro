#include "libretro.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned seen,audios,polls,shutdowns;
static bool env(unsigned cmd,void*p){if(cmd==RETRO_ENVIRONMENT_SHUTDOWN)++shutdowns;return false;}
static void video(const void*p,unsigned w,unsigned h,size_t pitch){seen=*(const unsigned*)p;}
static size_t audio(const int16_t*p,size_t n){++audios;return n;}
static void poll(void){++polls;}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned b){return 17;}
#define API(n) __typeof__(&n) p_##n=dlsym(lib,#n);if(!p_##n)return 3
int main(int argc,char**argv){
 void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 2;}
 API(retro_set_environment);API(retro_set_video_refresh);API(retro_set_audio_sample_batch);API(retro_set_input_poll);API(retro_set_input_state);API(retro_init);API(retro_load_game);API(retro_unload_game);API(retro_run);API(retro_reset);API(retro_deinit);API(retro_set_controller_port_device);
 p_retro_set_environment(env);p_retro_set_video_refresh(video);p_retro_set_audio_sample_batch(audio);p_retro_set_input_poll(poll);p_retro_set_input_state(input);p_retro_init();
 struct retro_game_info g={"fixture.test",0,0,0};if(!p_retro_load_game(&g))return 4;p_retro_set_controller_port_device(0,773);
 for(unsigned cycle=0;cycle<10;cycle++){
  for(unsigned i=0;i<7;i++)p_retro_run();
  if(seen!=7||shutdowns){printf("FAIL cycle %u expected 7 got %u shutdown=%u\n",cycle,seen,shutdowns);return 5;}
  p_retro_reset();
 }
 p_retro_unload_game();p_retro_deinit();dlclose(lib);
 if(polls!=70||audios!=70)return 6;
 puts("PASS 10 resets, callbacks and controller device preserved");return 0;
}
