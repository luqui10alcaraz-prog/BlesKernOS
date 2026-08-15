#include "win32.h"
#include "../include/types.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../include/vfs.h"
#include "../string.h"

#define MMSYSERR_NOERROR 0U
#define MMSYSERR_ERROR 1U
#define ACMERR_NOTPOSSIBLE 512U
#define ACMSTREAMHEADER_STATUSF_PREPARED 0x00020000U
#define ACMSTREAMHEADER_STATUSF_DONE 0x00010000U
#define AVIERR_UNSUPPORTED 0x80044065U
#define WN_SUCCESS 0U
#define WN_MORE_DATA 234U
#define WN_NO_NETWORK 1222U
#define WN_NOT_CONNECTED 2250U

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
typedef struct { uint16_t tag,channels;uint32_t rate,average;uint16_t align,bits,size; } legacy_wave_t;
typedef struct {uint32_t size,status,user;uint8_t*source;uint32_t source_length,source_used,source_user;uint8_t*destination;uint32_t destination_length,destination_used,destination_user,reserved[10];} acm_header_t;
typedef struct {uint32_t magic,owner;legacy_wave_t source,destination;} acm_stream_t;
#define ACM_STREAM_MAGIC 0x41434D53U
static acm_stream_t*acm_streams[16];

/* Windows 95 shipped ACM 4.3.  Reporting it lets old multimedia programs
 * probe the subsystem, while unsupported codec operations fail cleanly. */
static uint32_t WIN32_API legacy_acmGetVersion(void) { return 0x04030000U; }
static uint32_t WIN32_API legacy_acmMetrics(void *object UNUSED, uint32_t metric,
                                            uint32_t *value) {
    if (!value) return MMSYSERR_ERROR;
    /* Count/size queries are the probes most installers and players perform. */
    *value = (metric == 2U || metric == 3U) ? 64U : 0U;
    return MMSYSERR_NOERROR;
}
static uint32_t WIN32_API legacy_acmDriverEnum(void *callback UNUSED,
                                               uint32_t instance UNUSED,
                                               uint32_t flags UNUSED) {
    return MMSYSERR_NOERROR;
}
static bool pcm_format(const legacy_wave_t*f){return f&&f->tag==1U&&(f->bits==8U||f->bits==16U)&&(f->channels==1U||f->channels==2U)&&f->rate>=4000U&&f->rate<=48000U;}
static void finish_wave(legacy_wave_t*f){f->align=(uint16_t)(f->channels*(f->bits/8U));f->average=f->rate*f->align;f->size=0;}
static uint32_t WIN32_API legacy_acmFormatSuggest(void *driver UNUSED,
    const legacy_wave_t *source, legacy_wave_t *destination,
    uint32_t destination_size, uint32_t flags UNUSED) {
    if(!pcm_format(source)||!destination||destination_size<sizeof(*destination))return ACMERR_NOTPOSSIBLE;
    if(destination->tag!=1U)destination->tag=1U;
    if(!destination->channels)destination->channels=source->channels;
    if(!destination->rate)destination->rate=source->rate;
    if(!destination->bits)destination->bits=source->bits;
    finish_wave(destination);
    return pcm_format(destination)?MMSYSERR_NOERROR:ACMERR_NOTPOSSIBLE;
}
static uint32_t WIN32_API legacy_acmStreamOpen(void **stream, void *driver UNUSED,
    const legacy_wave_t *source, const legacy_wave_t *destination,
    const void *filter UNUSED, uint32_t callback UNUSED,
    uint32_t instance UNUSED, uint32_t flags UNUSED) {
    acm_stream_t*s;if(!stream||!pcm_format(source)||!pcm_format(destination))return ACMERR_NOTPOSSIBLE;s=(acm_stream_t*)kmalloc(sizeof(*s));if(!s)return MMSYSERR_ERROR;s->magic=ACM_STREAM_MAGIC;s->owner=task_current_process_id();s->source=*source;s->destination=*destination;for(uint32_t i=0;i<16U;i++)if(!acm_streams[i]){acm_streams[i]=s;*stream=s;return MMSYSERR_NOERROR;}kfree(s);return MMSYSERR_ERROR;
}
static uint32_t WIN32_API legacy_acmStreamClose(acm_stream_t *stream,uint32_t flags UNUSED){if(!stream||stream->magic!=ACM_STREAM_MAGIC)return MMSYSERR_ERROR;for(uint32_t i=0;i<16U;i++)if(acm_streams[i]==stream)acm_streams[i]=NULL;stream->magic=0;kfree(stream);return MMSYSERR_NOERROR;}
static uint32_t WIN32_API legacy_acmStreamSize(acm_stream_t *stream,
    uint32_t input, uint32_t *output, uint32_t flags) {
    uint32_t src_frame,dst_frame,src_frames,dst_frames;if(!stream||stream->magic!=ACM_STREAM_MAGIC||!output)return MMSYSERR_ERROR;src_frame=stream->source.align;dst_frame=stream->destination.align;if(flags==0U){src_frames=input/src_frame;dst_frames=(src_frames*stream->destination.rate+stream->source.rate-1U)/stream->source.rate;*output=dst_frames*dst_frame;}else{dst_frames=input/dst_frame;src_frames=(dst_frames*stream->source.rate+stream->destination.rate-1U)/stream->destination.rate;*output=src_frames*src_frame;}return MMSYSERR_NOERROR;
}
static int pcm_read(const legacy_wave_t*f,const uint8_t*p,uint32_t frame,uint32_t channel){if(f->bits==8U)return((int)p[frame*f->align+(channel<f->channels?channel:0)]-128)<<8;return((const int16_t*)(p+frame*f->align))[channel<f->channels?channel:0];}
static void pcm_write(const legacy_wave_t*f,uint8_t*p,uint32_t frame,uint32_t channel,int value){if(value<-32768)value=-32768;if(value>32767)value=32767;if(f->bits==8U)p[frame*f->align+channel]=(uint8_t)((value>>8)+128);else((int16_t*)(p+frame*f->align))[channel]=(int16_t)value;}
static uint32_t WIN32_API legacy_acmPrepare(acm_stream_t*stream,acm_header_t*h,uint32_t flags UNUSED){if(!stream||stream->magic!=ACM_STREAM_MAGIC||!h)return MMSYSERR_ERROR;h->status|=ACMSTREAMHEADER_STATUSF_PREPARED;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API legacy_acmUnprepare(acm_stream_t*stream,acm_header_t*h,uint32_t flags UNUSED){if(!stream||stream->magic!=ACM_STREAM_MAGIC||!h)return MMSYSERR_ERROR;h->status&=~ACMSTREAMHEADER_STATUSF_PREPARED;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API legacy_acmConvert(acm_stream_t*stream,acm_header_t*h,uint32_t flags UNUSED){uint32_t source_frames,dest_capacity,dest_frames;if(!stream||stream->magic!=ACM_STREAM_MAGIC||!h||!h->source||!h->destination)return MMSYSERR_ERROR;source_frames=h->source_length/stream->source.align;dest_capacity=h->destination_length/stream->destination.align;dest_frames=(source_frames*stream->destination.rate)/stream->source.rate;if(dest_frames>dest_capacity)dest_frames=dest_capacity;for(uint32_t d=0;d<dest_frames;d++){uint32_t s=(d*stream->source.rate)/stream->destination.rate;for(uint32_t c=0;c<stream->destination.channels;c++){int value;if(stream->source.channels==2U&&stream->destination.channels==1U)value=(pcm_read(&stream->source,h->source,s,0)+pcm_read(&stream->source,h->source,s,1))/2;else value=pcm_read(&stream->source,h->source,s,c);pcm_write(&stream->destination,h->destination,d,c,value);}}h->source_used=source_frames*stream->source.align;h->destination_used=dest_frames*stream->destination.align;h->status|=ACMSTREAMHEADER_STATUSF_DONE;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API legacy_acmStreamReset(void *stream UNUSED,
                                                uint32_t flags UNUSED) { return MMSYSERR_NOERROR; }

uint32_t win32_msacm32_resolve(const char *name) {
    if (equal(name, "acmGetVersion")) return (uint32_t)(uintptr_t)legacy_acmGetVersion;
    if (equal(name, "acmMetrics")) return (uint32_t)(uintptr_t)legacy_acmMetrics;
    if (equal(name, "acmDriverEnum")) return (uint32_t)(uintptr_t)legacy_acmDriverEnum;
    if (equal(name, "acmFormatSuggest")) return (uint32_t)(uintptr_t)legacy_acmFormatSuggest;
    if (equal(name, "acmStreamOpen")) return (uint32_t)(uintptr_t)legacy_acmStreamOpen;
    if (equal(name, "acmStreamClose")) return (uint32_t)(uintptr_t)legacy_acmStreamClose;
    if (equal(name, "acmStreamSize")) return (uint32_t)(uintptr_t)legacy_acmStreamSize;
    if (equal(name, "acmStreamPrepareHeader")) return (uint32_t)(uintptr_t)legacy_acmPrepare;
    if (equal(name, "acmStreamUnprepareHeader")) return (uint32_t)(uintptr_t)legacy_acmUnprepare;
    if (equal(name, "acmStreamConvert")) return (uint32_t)(uintptr_t)legacy_acmConvert;
    if (equal(name, "acmStreamReset")) return (uint32_t)(uintptr_t)legacy_acmStreamReset;
    return 0;
}

#define AVI_FILE_MAGIC 0x41564946U
#define AVI_STREAM_MAGIC 0x41564953U
#define FCC(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
#define STREAM_MAX 4U
typedef struct {uint32_t type,handler,scale,rate,length,sample_size,format_offset,format_size;} avi_stream_info_t;
typedef struct {uint32_t magic,refs,owner;uint8_t*data;uint32_t size,movi_start,movi_end,stream_count;avi_stream_info_t streams[STREAM_MAX];} avi_file_t;
typedef struct {uint32_t magic,refs,owner,index;avi_file_t*file;} avi_stream_t;
static avi_file_t*avi_files[16];
static avi_stream_t*avi_streams[16];
static uint32_t read32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static bool avi_path(const char*src,char*out){uint32_t s=0,d=0;if(!src||!out)return false;if(src[1]==':')s=2;while(src[s]&&d+1U<VFS_MAX_PATH){out[d++]=src[s]=='\\'?'/':src[s];s++;}out[d]=0;return d&&src[s]==0;}
static void avi_parse_strl(avi_file_t*f,uint32_t start,uint32_t end){avi_stream_info_t info;bool header=false;kmemset(&info,0,sizeof(info));while(start+8U<=end){uint32_t id=read32(f->data+start),size=read32(f->data+start+4U),data=start+8U,next=data+((size+1U)&~1U);if(next>end||next<data)break;if(id==FCC('s','t','r','h')&&size>=48U){info.type=read32(f->data+data);info.handler=read32(f->data+data+4U);info.scale=read32(f->data+data+20U);info.rate=read32(f->data+data+24U);info.length=read32(f->data+data+32U);info.sample_size=read32(f->data+data+44U);header=true;}else if(id==FCC('s','t','r','f')&&header){info.format_offset=data;info.format_size=size;}start=next;}if(header&&f->stream_count<STREAM_MAX)f->streams[f->stream_count++]=info;}
static void avi_parse_lists(avi_file_t*f,uint32_t start,uint32_t end){while(start+8U<=end){uint32_t id=read32(f->data+start),size=read32(f->data+start+4U),data=start+8U,next=data+((size+1U)&~1U);if(next>end||next<data)break;if((id==FCC('L','I','S','T')||id==FCC('R','I','F','F'))&&size>=4U){uint32_t type=read32(f->data+data);if(type==FCC('s','t','r','l'))avi_parse_strl(f,data+4U,data+size);else if(type==FCC('m','o','v','i')){f->movi_start=data+4U;f->movi_end=data+size;}else avi_parse_lists(f,data+4U,data+size);}start=next;}}
static avi_file_t*avi_open_native(const char*name){char path[VFS_MAX_PATH];void*raw=NULL;uint32_t size=0;avi_file_t*f;if(!avi_path(name,path)||!vfs_read_all(path,&raw,&size)||size<12U)return NULL;if(read32(raw)!=FCC('R','I','F','F')||read32((uint8_t*)raw+8U)!=FCC('A','V','I',' ')){kfree(raw);return NULL;}f=(avi_file_t*)kmalloc(sizeof(*f));if(!f){kfree(raw);return NULL;}kmemset(f,0,sizeof(*f));f->magic=AVI_FILE_MAGIC;f->refs=1;f->owner=task_current_process_id();f->data=(uint8_t*)raw;f->size=size;avi_parse_lists(f,12U,size);if(!f->movi_start||!f->stream_count){kfree(raw);kfree(f);return NULL;}for(uint32_t i=0;i<16U;i++)if(!avi_files[i]){avi_files[i]=f;return f;}kfree(raw);kfree(f);return NULL;}
static uint32_t avi_file_release(avi_file_t*f){uint32_t refs;if(!f||f->magic!=AVI_FILE_MAGIC)return 0;refs=--f->refs;if(!refs){for(uint32_t i=0;i<16U;i++)if(avi_files[i]==f)avi_files[i]=NULL;f->magic=0;kfree(f->data);kfree(f);}return refs;}
static bool chunk_matches(uint32_t id,uint32_t index,uint32_t type){uint8_t*a=(uint8_t*)&id;if(a[0]!=(uint8_t)('0'+(index/10U))||a[1]!=(uint8_t)('0'+(index%10U)))return false;return type==FCC('a','u','d','s')?(a[2]=='w'&&a[3]=='b'):((a[2]=='d'&&a[3]=='b')||(a[2]=='d'&&a[3]=='c')) ;}
static void avi_read_chunks(avi_stream_t*s,uint32_t start,uint32_t end,uint32_t*skip,uint32_t*wanted,uint8_t*out,uint32_t capacity,uint32_t*written,uint32_t*units){avi_file_t*f=s->file;avi_stream_info_t*i=&f->streams[s->index];while(start+8U<=end&&*wanted){uint32_t id=read32(f->data+start),size=read32(f->data+start+4U),data=start+8U,next=data+((size+1U)&~1U);if(next>end||next<data)break;if(id==FCC('L','I','S','T')&&size>=4U)avi_read_chunks(s,data+4U,data+size,skip,wanted,out,capacity,written,units);else if(chunk_matches(id,s->index,i->type)){uint32_t chunk_units=i->sample_size?size/i->sample_size:1U;if(*skip>=chunk_units)*skip-=chunk_units;else{uint32_t first_bytes=(*skip)*(i->sample_size?i->sample_size:size),available=size-first_bytes,take_units=*wanted<chunk_units-*skip?*wanted:chunk_units-*skip,take_bytes=i->sample_size?take_units*i->sample_size:available;if(take_bytes>capacity-*written)take_bytes=capacity-*written;if(take_bytes){kmemcpy(out+*written,f->data+data+first_bytes,take_bytes);*written+=take_bytes;*units+=i->sample_size?take_bytes/i->sample_size:1U;}*wanted-=take_units;*skip=0;if(*written>=capacity)return;}}start=next;}}
static void WIN32_API legacy_AVIFileInit(void) {}
static void WIN32_API legacy_AVIFileExit(void) {}
static uint32_t WIN32_API legacy_AVIFileOpenA(void **file, const char *name,
    uint32_t mode UNUSED, const void *handler UNUSED) {
    avi_file_t*f;if(!file)return AVIERR_UNSUPPORTED;f=avi_open_native(name);*file=f;return f?0U:AVIERR_UNSUPPORTED;
}
static uint32_t WIN32_API legacy_AVIFileOpenW(void **file, const uint16_t *name,
    uint32_t mode UNUSED, const void *handler UNUSED) {
    char path[VFS_MAX_PATH];uint32_t i=0;if(!name)return AVIERR_UNSUPPORTED;while(name[i]&&i+1U<sizeof(path)){path[i]=(char)(name[i]&0x7FU);i++;}path[i]=0;return legacy_AVIFileOpenA(file,path,mode,handler);
}
static uint32_t WIN32_API legacy_AVIRelease(void *object) {uint32_t magic;if(!object)return 0;magic=*(uint32_t*)object;if(magic==AVI_FILE_MAGIC)return avi_file_release((avi_file_t*)object);if(magic==AVI_STREAM_MAGIC){avi_stream_t*s=(avi_stream_t*)object;uint32_t refs=--s->refs;if(!refs){for(uint32_t i=0;i<16U;i++)if(avi_streams[i]==s)avi_streams[i]=NULL;avi_file_release(s->file);s->magic=0;kfree(s);}return refs;}return 0;}
static uint32_t WIN32_API legacy_AVIAddRef(void *object) {if(!object)return 0;if(*(uint32_t*)object==AVI_FILE_MAGIC)return++((avi_file_t*)object)->refs;if(*(uint32_t*)object==AVI_STREAM_MAGIC)return++((avi_stream_t*)object)->refs;return 0;}
static uint32_t WIN32_API legacy_AVIFileGetStream(avi_file_t *file, void **stream,
    uint32_t type, int32_t position) {
    avi_stream_t*s;if(!file||file->magic!=AVI_FILE_MAGIC||!stream)return AVIERR_UNSUPPORTED;for(uint32_t i=0;i<file->stream_count;i++)if((!type||file->streams[i].type==type)&&position--==0){s=(avi_stream_t*)kmalloc(sizeof(*s));if(!s)return AVIERR_UNSUPPORTED;s->magic=AVI_STREAM_MAGIC;s->refs=1;s->owner=task_current_process_id();s->index=i;s->file=file;for(uint32_t n=0;n<16U;n++)if(!avi_streams[n]){avi_streams[n]=s;file->refs++;*stream=s;return 0;}kfree(s);break;}*stream=NULL;return AVIERR_UNSUPPORTED;
}
static int32_t WIN32_API legacy_AVIStreamLength(avi_stream_t *stream) {return stream&&stream->magic==AVI_STREAM_MAGIC?(int32_t)stream->file->streams[stream->index].length:0;}
static int32_t WIN32_API legacy_AVIStreamStart(void *stream UNUSED) { return 0; }
static uint32_t WIN32_API legacy_AVIInfo(void *object, void *raw,int32_t size){avi_stream_info_t*i;uint32_t*out=(uint32_t*)raw;if(!object||!raw||size<52||*(uint32_t*)object!=AVI_STREAM_MAGIC)return AVIERR_UNSUPPORTED;i=&((avi_stream_t*)object)->file->streams[((avi_stream_t*)object)->index];kmemset(raw,0,(uint32_t)size);out[0]=i->type;out[1]=i->handler;out[5]=i->scale;out[6]=i->rate;out[8]=i->length;out[10]=i->sample_size?i->sample_size:65536U;out[12]=i->sample_size;return 0;}
static uint32_t WIN32_API legacy_AVIStreamOpenFromFileA(void **stream,
    const char *name, uint32_t type, int32_t handler,
    uint32_t mode UNUSED, const void *clsid UNUSED) {
    avi_file_t*f=avi_open_native(name);uint32_t result;if(!f){if(stream)*stream=NULL;return AVIERR_UNSUPPORTED;}result=legacy_AVIFileGetStream(f,stream,type,handler);avi_file_release(f);return result;
}
static uint32_t WIN32_API legacy_AVIStreamOpenFromFileW(void **stream,
    const uint16_t *file, uint32_t type, int32_t handler,
    uint32_t mode UNUSED, const void *clsid UNUSED) {
    char path[VFS_MAX_PATH];uint32_t i=0;if(!file)return AVIERR_UNSUPPORTED;while(file[i]&&i+1U<sizeof(path)){path[i]=(char)(file[i]&0x7FU);i++;}path[i]=0;return legacy_AVIStreamOpenFromFileA(stream,path,type,handler,mode,clsid);
}
static uint32_t WIN32_API legacy_AVIStreamReadFormat(avi_stream_t*s,int32_t pos UNUSED,void*format,int32_t*size){avi_stream_info_t*i;if(!s||s->magic!=AVI_STREAM_MAGIC||!size)return AVIERR_UNSUPPORTED;i=&s->file->streams[s->index];if(!format||*size<(int32_t)i->format_size){*size=(int32_t)i->format_size;return format?AVIERR_UNSUPPORTED:0U;}kmemcpy(format,s->file->data+i->format_offset,i->format_size);*size=(int32_t)i->format_size;return 0;}
static uint32_t WIN32_API legacy_AVIStreamRead(avi_stream_t*s,int32_t start,int32_t samples,void*buffer,int32_t capacity,int32_t*bytesread,int32_t*samplesread){uint32_t skip=start<0?0U:(uint32_t)start,wanted=samples<0?0x7FFFFFFFU:(uint32_t)samples,written=0,units=0;if(bytesread)*bytesread=0;if(samplesread)*samplesread=0;if(!s||s->magic!=AVI_STREAM_MAGIC||!buffer||capacity<0)return AVIERR_UNSUPPORTED;avi_read_chunks(s,s->file->movi_start,s->file->movi_end,&skip,&wanted,buffer,(uint32_t)capacity,&written,&units);if(bytesread)*bytesread=(int32_t)written;if(samplesread)*samplesread=(int32_t)units;return 0;}
uint32_t win32_avifil32_resolve(const char *name) {
    if (equal(name, "AVIFileInit")) return (uint32_t)(uintptr_t)legacy_AVIFileInit;
    if (equal(name, "AVIFileExit")) return (uint32_t)(uintptr_t)legacy_AVIFileExit;
    if (equal(name, "AVIFileOpen") || equal(name, "AVIFileOpenA")) return (uint32_t)(uintptr_t)legacy_AVIFileOpenA;
    if (equal(name, "AVIFileOpenW")) return (uint32_t)(uintptr_t)legacy_AVIFileOpenW;
    if (equal(name, "AVIFileRelease") || equal(name, "AVIStreamRelease")) return (uint32_t)(uintptr_t)legacy_AVIRelease;
    if (equal(name, "AVIFileAddRef") || equal(name, "AVIStreamAddRef")) return (uint32_t)(uintptr_t)legacy_AVIAddRef;
    if (equal(name, "AVIFileGetStream")) return (uint32_t)(uintptr_t)legacy_AVIFileGetStream;
    if (equal(name, "AVIStreamLength")) return (uint32_t)(uintptr_t)legacy_AVIStreamLength;
    if (equal(name, "AVIStreamStart")) return (uint32_t)(uintptr_t)legacy_AVIStreamStart;
    if (equal(name, "AVIStreamReadFormat")) return (uint32_t)(uintptr_t)legacy_AVIStreamReadFormat;
    if (equal(name, "AVIStreamRead")) return (uint32_t)(uintptr_t)legacy_AVIStreamRead;
    if (equal(name, "AVIFileInfo") || equal(name, "AVIFileInfoA") || equal(name, "AVIFileInfoW") ||
        equal(name, "AVIStreamInfo") || equal(name, "AVIStreamInfoA") || equal(name, "AVIStreamInfoW"))
        return (uint32_t)(uintptr_t)legacy_AVIInfo;
    if (equal(name, "AVIStreamOpenFromFile") || equal(name, "AVIStreamOpenFromFileA"))
        return (uint32_t)(uintptr_t)legacy_AVIStreamOpenFromFileA;
    if (equal(name, "AVIStreamOpenFromFileW")) return (uint32_t)(uintptr_t)legacy_AVIStreamOpenFromFileW;
    return 0;
}

static uint32_t WIN32_API legacy_WNetGetUserA(const char *resource UNUSED,
                                               char *user, uint32_t *size) {
    static const char value[] = "User";
    if (!size) return WN_MORE_DATA;
    if (!user || *size < sizeof(value)) { *size = sizeof(value); return WN_MORE_DATA; }
    for (uint32_t i = 0; i < sizeof(value); i++) user[i] = value[i];
    *size = sizeof(value) - 1U;
    return WN_SUCCESS;
}
static uint32_t WIN32_API legacy_WNetGetUserW(const uint16_t *resource UNUSED,
                                               uint16_t *user, uint32_t *size) {
    static const uint16_t value[] = {'U','s','e','r',0};
    if (!size) return WN_MORE_DATA;
    if (!user || *size < 5U) { *size = 5U; return WN_MORE_DATA; }
    for (uint32_t i = 0; i < 5U; i++) user[i] = value[i];
    *size = 4U;
    return WN_SUCCESS;
}
static uint32_t WIN32_API legacy_WNetGetConnectionA(const char *local UNUSED,
                                                     char *remote UNUSED,
                                                     uint32_t *size UNUSED) { return WN_NOT_CONNECTED; }
static uint32_t WIN32_API legacy_WNetGetConnectionW(const uint16_t *local UNUSED,
                                                     uint16_t *remote UNUSED,
                                                     uint32_t *size UNUSED) { return WN_NOT_CONNECTED; }
static uint32_t WIN32_API legacy_WNetUnavailable3(const void *a UNUSED,
    uint32_t b UNUSED, uint32_t c UNUSED) { return WN_NO_NETWORK; }
static uint32_t WIN32_API legacy_WNetUnavailable4(const void *a UNUSED,
    const void *b UNUSED, const void *c UNUSED, uint32_t d UNUSED) { return WN_NO_NETWORK; }
static uint32_t WIN32_API legacy_WNetOpenEnum(uint32_t scope UNUSED,
    uint32_t type UNUSED, uint32_t usage UNUSED, const void *resource UNUSED,
    void **enumeration) { if (enumeration) *enumeration = NULL; return WN_NO_NETWORK; }
static uint32_t WIN32_API legacy_WNetCloseEnum(void *enumeration UNUSED) { return WN_SUCCESS; }
static uint32_t WIN32_API legacy_WNetEnumResource(void *enumeration UNUSED,
    uint32_t *count UNUSED, void *buffer UNUSED, uint32_t *size UNUSED) { return WN_NO_NETWORK; }
uint32_t win32_mpr_resolve(const char *name) {
    if (equal(name, "WNetGetUserA")) return (uint32_t)(uintptr_t)legacy_WNetGetUserA;
    if (equal(name, "WNetGetUserW")) return (uint32_t)(uintptr_t)legacy_WNetGetUserW;
    if (equal(name, "WNetGetConnectionA")) return (uint32_t)(uintptr_t)legacy_WNetGetConnectionA;
    if (equal(name, "WNetGetConnectionW")) return (uint32_t)(uintptr_t)legacy_WNetGetConnectionW;
    if (equal(name, "WNetCancelConnection2A") || equal(name, "WNetCancelConnection2W"))
        return (uint32_t)(uintptr_t)legacy_WNetUnavailable3;
    if (equal(name, "WNetAddConnection2A") || equal(name, "WNetAddConnection2W"))
        return (uint32_t)(uintptr_t)legacy_WNetUnavailable4;
    if (equal(name, "WNetOpenEnumA") || equal(name, "WNetOpenEnumW")) return (uint32_t)(uintptr_t)legacy_WNetOpenEnum;
    if (equal(name, "WNetCloseEnum")) return (uint32_t)(uintptr_t)legacy_WNetCloseEnum;
    if (equal(name, "WNetEnumResourceA") || equal(name, "WNetEnumResourceW")) return (uint32_t)(uintptr_t)legacy_WNetEnumResource;
    return 0;
}

void win32_legacy_cleanup_process(uint32_t owner){for(uint32_t i=0;i<16U;i++)if(acm_streams[i]&&acm_streams[i]->owner==owner){acm_streams[i]->magic=0;kfree(acm_streams[i]);acm_streams[i]=NULL;}for(uint32_t i=0;i<16U;i++)if(avi_streams[i]&&avi_streams[i]->owner==owner){avi_stream_t*s=avi_streams[i];avi_streams[i]=NULL;if(s->file&&s->file->refs)s->file->refs--;s->magic=0;kfree(s);}for(uint32_t i=0;i<16U;i++)if(avi_files[i]&&avi_files[i]->owner==owner){avi_file_t*f=avi_files[i];avi_files[i]=NULL;f->magic=0;if(f->data)kfree(f->data);kfree(f);}}
