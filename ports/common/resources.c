/* Resources owned by one loaded legacy engine. These engines originally exited
 * their process; libretro must reclaim outstanding resources when content closes.
 * Link with --wrap for the functions below. Never owns frontend allocations. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
typedef struct Node { void *key; size_t size; int kind; struct Node *next; } Node;
static Node *buckets[4096];
static int active;
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t,size_t);
extern void *__real_realloc(void*,size_t);
extern void __real_free(void*);
extern FILE *__real_fopen(const char*,const char*);
extern FILE *__real_fdopen(int,const char*);
#if __ANDROID_API__ >= 23
extern FILE *__real_fmemopen(void*,size_t,const char*);
#endif
extern int __real_fclose(FILE*);
extern int __real_open(const char*,int,...);
extern int __real_close(int);
extern void *__real_mmap(void*,size_t,int,int,int,off_t);
extern int __real_munmap(void*,size_t);
static unsigned bucket(void *p,int kind){return (((uintptr_t)p>>3) ^ (unsigned)kind*277u)&4095;}
static Node *take(void *p,int kind){
 Node **n=&buckets[bucket(p,kind)];while(*n){if((*n)->key==p&&(*n)->kind==kind){Node*r=*n;*n=r->next;return r;}n=&(*n)->next;}return NULL;
}
static void forget(void*p,int kind){__real_free(take(p,kind));}
static void remember(void*p,size_t size,int kind){
 if(!active||!p)return;Node*n=__real_malloc(sizeof *n);if(!n)return;
 unsigned b=bucket(p,kind);*n=(Node){p,size,kind,buckets[b]};buckets[b]=n;
}
void tv_resources_begin(void){active=1;}
void *__wrap_malloc(size_t n){void*p=__real_malloc(n);remember(p,n,0);return p;}
void *__wrap_calloc(size_t n,size_t s){void*p=__real_calloc(n,s);remember(p,n*s,0);return p;}
void *__wrap_realloc(void*p,size_t n){Node*r=take(p,0);void*q=__real_realloc(p,n);if(q){remember(q,n,0);__real_free(r);}else if(r&&n){unsigned b=bucket(p,0);r->next=buckets[b];buckets[b]=r;}else __real_free(r);return q;}
void __wrap_free(void*p){forget(p,0);__real_free(p);}
FILE *__wrap_fopen(const char*p,const char*m){FILE*f=__real_fopen(p,m);remember(f,0,1);return f;}
FILE *__wrap_fdopen(int fd,const char*m){FILE*f=__real_fdopen(fd,m);remember(f,0,1);return f;}
#if __ANDROID_API__ >= 23
FILE *__wrap_fmemopen(void*p,size_t n,const char*m){FILE*f=__real_fmemopen(p,n,m);remember(f,0,1);return f;}
#endif
int __wrap_fclose(FILE*f){if(f){forget((void*)(intptr_t)(fileno(f)+1),2);forget(f,1);}return __real_fclose(f);}
int __wrap_open(const char*p,int flags,...){mode_t m=0;if(flags&O_CREAT){va_list a;va_start(a,flags);m=va_arg(a,int);va_end(a);}int fd=__real_open(p,flags,m);if(fd>=0)remember((void*)(intptr_t)(fd+1),0,2);return fd;}
int __wrap_close(int fd){forget((void*)(intptr_t)(fd+1),2);return __real_close(fd);}
void *__wrap_mmap(void*a,size_t n,int prot,int flags,int fd,off_t off){void*p=__real_mmap(a,n,prot,flags,fd,off);if(p!=MAP_FAILED&&!(flags&MAP_FIXED))remember(p,n,3);return p;}
int __wrap_munmap(void*p,size_t n){int r=__real_munmap(p,n);if(!r)forget(p,3);return r;}
void tv_resources_end(void){
 active=0;size_t bytes=0,count=0;
 /* Files may still refer to buffers in the allocation set. Close them first. */
 for(int kind=1;kind<=3;kind++)for(unsigned b=0;b<4096;b++){
  for(;;){Node*r=buckets[b];while(r&&r->kind!=kind)r=r->next;if(!r)break;r=take(r->key,kind);
   if(kind==1)__wrap_fclose(r->key);else if(kind==2)__real_close((int)(intptr_t)r->key-1);else __real_munmap(r->key,r->size);__real_free(r);
  }
 }
 for(unsigned b=0;b<4096;b++){Node*n=buckets[b];buckets[b]=NULL;while(n){Node*next=n->next;bytes+=n->size;count++;__real_free(n->key);__real_free(n);n=next;}}
 fprintf(stderr,"libretro: reclaimed %zu outstanding allocations (%zu bytes)\n",count,bytes);
}
