#include <dlfcn.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc>2)for(int i=2;i<argc;i++)printf("preload %s %p\n",argv[i],dlopen(argv[i],RTLD_NOW|RTLD_LOCAL));
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
 void *symbol=dlsym(lib,"retro_init");
 dlclose(lib);Dl_info info;
 printf("module mapped after dlclose without calling core: %d\n",dladdr(symbol,&info));
 return 0;
}
