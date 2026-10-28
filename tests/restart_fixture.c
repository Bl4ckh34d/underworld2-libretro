#include "libretro.h"
#include <string.h>
static retro_environment_t env;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static retro_input_state_t input;
static unsigned counter, device;
void retro_set_environment(retro_environment_t c){env=c;}
void retro_set_video_refresh(retro_video_refresh_t c){video=c;}
void retro_set_audio_sample(retro_audio_sample_t c){(void)c;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t c){audio=c;}
void retro_set_input_poll(retro_input_poll_t c){poll=c;}
void retro_set_input_state(retro_input_state_t c){input=c;}
void retro_init(void){}
void retro_deinit(void){}
unsigned retro_api_version(void){return 1;}
void retro_get_system_info(struct retro_system_info*i){*i=(struct retro_system_info){"Restart fixture","1","test",true,false};}
void retro_get_system_av_info(struct retro_system_av_info*i){*i=(struct retro_system_av_info){{1,1,1,1,1},{60,44100}};}
bool retro_load_game(const struct retro_game_info*g){return g&&g->path&&!strcmp(g->path,"fixture.test");}
void retro_unload_game(void){}
void retro_reset(void){} /* The regression: engine globals never reset. */
void retro_run(void){int16_t samples[2]={1,1};poll();++counter;video(&counter,1,1,4);audio(samples,1);if(input(0,1,0,0)!=17||device!=773)env(RETRO_ENVIRONMENT_SHUTDOWN,0);}
void retro_set_controller_port_device(unsigned p,unsigned d){if(!p)device=d;}
bool retro_load_game_special(unsigned t,const struct retro_game_info*g,size_t n){return false;}
size_t retro_serialize_size(void){return 0;}
bool retro_serialize(void*p,size_t n){return false;}
bool retro_unserialize(const void*p,size_t n){return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned i,bool e,const char*c){}
void *retro_get_memory_data(unsigned i){return &counter;}
size_t retro_get_memory_size(unsigned i){return sizeof counter;}
unsigned retro_get_region(void){return 0;}
