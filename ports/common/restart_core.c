/* Reload a legacy engine inside one libretro core. No frontend restart required.
 * The payload has private symbols and owns its native cleanup. Only synchronous
 * software-rendered engines belong here; GL/audio-thread callbacks need a
 * different lifecycle contract. Disk saves are never touched by this adapter. */
#define _GNU_SOURCE
#include "libretro.h"
#include <dlfcn.h>
#include <android/dlext.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

extern const unsigned char engine_blob_start[],engine_blob_end[];
#define FUNCTIONS(X) \
 X(retro_set_environment) X(retro_set_video_refresh) X(retro_set_audio_sample) \
 X(retro_set_audio_sample_batch) X(retro_set_input_poll) X(retro_set_input_state) \
 X(retro_init) X(retro_deinit) X(retro_get_system_info) X(retro_get_system_av_info) \
 X(retro_load_game) X(retro_unload_game) X(retro_run) X(retro_set_controller_port_device) \
 X(retro_get_region) X(retro_serialize_size) X(retro_serialize) X(retro_unserialize) \
 X(retro_cheat_reset) X(retro_cheat_set) X(retro_get_memory_data) X(retro_get_memory_size)
#define DECLARE(n) static __typeof__(&n) inner_##n;
FUNCTIONS(DECLARE)
static void *module;
static int module_fd=-1;
static bool initialized,loaded,unload_failed;
static char *content_path,*content_meta;
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_t audio_sample;
static retro_audio_sample_batch_t audio_batch;
static retro_input_poll_t poll;
static retro_input_state_t input;
static retro_keyboard_event_t keyboard;
static unsigned devices[16];
static bool device_set[16];
static char name[128],version[128],extensions[128];

static void keyboard_event(bool down,unsigned key,uint32_t character,uint16_t modifiers)
{ if(keyboard)keyboard(down,key,character,modifiers); }
static bool engine_environment(unsigned command,void *data)
{
 if(command==RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK){
  keyboard=((struct retro_keyboard_callback*)data)->callback;
  struct retro_keyboard_callback stable={keyboard_event};
  return environment&&environment(command,&stable);
 }
 return environment&&environment(command,data);
}
static void report_failure(const char *operation)
{
 fprintf(stderr,"Native restart: %s failed\n",operation);
 if(environment){struct retro_message msg={"Native core restart failed; close and reopen content.",300};environment(RETRO_ENVIRONMENT_SET_MESSAGE,&msg);environment(RETRO_ENVIRONMENT_SHUTDOWN,NULL);}
}
static void close_engine(void)
{
 if(!module)return;
 if(loaded)inner_retro_unload_game();
 loaded=false;
 if(initialized)inner_retro_deinit();
 initialized=false;keyboard=NULL;
 void *address=(void*)inner_retro_init;
 int result=dlclose(module);module=NULL;
 close(module_fd);module_fd=-1;
 Dl_info info;
 if(result||dladdr(address,&info)){unload_failed=true;fprintf(stderr,"Native restart: payload remained mapped after dlclose\n");}
#define CLEAR(n) inner_##n=NULL;
 FUNCTIONS(CLEAR)
}
static bool open_engine(void)
{
 if(module)return true;
 if(unload_failed)return false;
 module_fd=(int)syscall(__NR_memfd_create,"retro-native-engine",1U);
 if(module_fd<0){fprintf(stderr,"Native restart memfd: %s\n",strerror(errno));return false;}
 size_t size=engine_blob_end-engine_blob_start,done=0;
 while(done<size){ssize_t n=write(module_fd,engine_blob_start+done,size-done);if(n<0&&errno==EINTR)continue;if(n<=0){close(module_fd);module_fd=-1;return false;}done+=(size_t)n;}
 android_dlextinfo ext={0};ext.flags=ANDROID_DLEXT_USE_LIBRARY_FD;ext.library_fd=module_fd;
 module=android_dlopen_ext("libretro_embedded_engine.so",RTLD_NOW|RTLD_LOCAL,&ext);
 if(!module){fprintf(stderr,"Native restart dlopen: %s\n",dlerror());close(module_fd);module_fd=-1;return false;}
#define RESOLVE(n) inner_##n=(__typeof__(inner_##n))dlsym(module,#n);if(!inner_##n){fprintf(stderr,"Native restart missing %s\n",#n);dlclose(module);module=NULL;close(module_fd);module_fd=-1;return false;}
 FUNCTIONS(RESOLVE)
 inner_retro_set_environment(engine_environment);
 inner_retro_set_video_refresh(video);inner_retro_set_audio_sample(audio_sample);
 inner_retro_set_audio_sample_batch(audio_batch);inner_retro_set_input_poll(poll);inner_retro_set_input_state(input);
 return true;
}
static bool start_engine(void)
{
 if(!open_engine())return false;
 if(!initialized){inner_retro_init();initialized=true;}
 struct retro_game_info game={content_path,NULL,0,content_meta};
 loaded=inner_retro_load_game(&game);
 if(loaded)for(unsigned p=0;p<16;p++)if(device_set[p])inner_retro_set_controller_port_device(p,devices[p]);
 return loaded;
}
void retro_set_environment(retro_environment_t c){environment=c;if(module)inner_retro_set_environment(engine_environment);}
void retro_set_video_refresh(retro_video_refresh_t c){video=c;if(module)inner_retro_set_video_refresh(c);}
void retro_set_audio_sample(retro_audio_sample_t c){audio_sample=c;if(module)inner_retro_set_audio_sample(c);}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t c){audio_batch=c;if(module)inner_retro_set_audio_sample_batch(c);}
void retro_set_input_poll(retro_input_poll_t c){poll=c;if(module)inner_retro_set_input_poll(c);}
void retro_set_input_state(retro_input_state_t c){input=c;if(module)inner_retro_set_input_state(c);}
void retro_init(void){if(open_engine()){inner_retro_init();initialized=true;}else report_failure("initialization");}
void retro_deinit(void){close_engine();free(content_path);free(content_meta);content_path=content_meta=NULL;}
unsigned retro_api_version(void){return RETRO_API_VERSION;}
void retro_get_system_info(struct retro_system_info *info)
{
 if(!open_engine()){*info=(struct retro_system_info){"Native restart error","1","",true,false};return;}
 inner_retro_get_system_info(info);
 snprintf(name,sizeof name,"%s",info->library_name);snprintf(version,sizeof version,"%s-reset",info->library_version);snprintf(extensions,sizeof extensions,"%s",info->valid_extensions);
 info->library_name=name;info->library_version=version;info->valid_extensions=extensions;
}
void retro_get_system_av_info(struct retro_system_av_info *info){if(open_engine())inner_retro_get_system_av_info(info);else memset(info,0,sizeof *info);}
bool retro_load_game(const struct retro_game_info *game)
{
 if(!game||!game->path||game->data||loaded)return false;
 char *path=strdup(game->path),*meta=game->meta?strdup(game->meta):NULL;
 if(!path||(game->meta&&!meta)){free(path);free(meta);return false;}
 free(content_path);free(content_meta);content_path=path;content_meta=meta;
 if(start_engine())return true;
 close_engine();return false;
}
void retro_unload_game(void){close_engine();}
void retro_reset(void){if(!loaded)return;close_engine();if(!start_engine())report_failure("reset");else fprintf(stderr,"Native restart: fresh engine loaded\n");}
void retro_run(void){if(loaded)inner_retro_run();}
void retro_set_controller_port_device(unsigned p,unsigned d){if(p<16){device_set[p]=true;devices[p]=d;}if(module)inner_retro_set_controller_port_device(p,d);}
unsigned retro_get_region(void){return module?inner_retro_get_region():RETRO_REGION_NTSC;}
size_t retro_serialize_size(void){return loaded?inner_retro_serialize_size():0;}
bool retro_serialize(void*p,size_t n){return loaded&&inner_retro_serialize(p,n);}
bool retro_unserialize(const void*p,size_t n){return loaded&&inner_retro_unserialize(p,n);}
void retro_cheat_reset(void){if(module)inner_retro_cheat_reset();}
void retro_cheat_set(unsigned i,bool e,const char*c){if(module)inner_retro_cheat_set(i,e,c);}
void *retro_get_memory_data(unsigned i){return loaded?inner_retro_get_memory_data(i):NULL;}
size_t retro_get_memory_size(unsigned i){return loaded?inner_retro_get_memory_size(i):0;}
bool retro_load_game_special(unsigned t,const struct retro_game_info*g,size_t n){(void)t;(void)g;(void)n;return false;}
