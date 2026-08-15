#include "win32.h"
#include "../include/pit.h"
#include "../include/sound.h"
#include "../include/vfs.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../string.h"

#define MMSYSERR_NOERROR 0U
#define MMSYSERR_ERROR 1U
#define MMSYSERR_BADDEVICEID 2U
#define MMSYSERR_NOTENABLED 3U
#define MMSYSERR_ALLOCATED 4U
#define MMSYSERR_INVALHANDLE 5U
#define MMSYSERR_NODRIVER 6U
#define MMSYSERR_NOMEM 7U
#define MMSYSERR_NOTSUPPORTED 8U
#define MMSYSERR_BADERRNUM 9U
#define MMSYSERR_INVALFLAG 10U
#define MMSYSERR_INVALPARAM 11U
#define WAVERR_BADFORMAT 32U
#define WAVERR_STILLPLAYING 33U
#define WAVERR_UNPREPARED 34U
#define TIMERR_NOERROR 0U
#define TIMERR_NOCANDO 97U
#define SND_ASYNC 0x0001U
#define SND_MEMORY 0x0004U
#define SND_PURGE 0x0040U
#define CALLBACK_TYPEMASK 0x00070000U
#define CALLBACK_WINDOW 0x00010000U
#define CALLBACK_THREAD 0x00020000U
#define CALLBACK_FUNCTION 0x00030000U
#define WAVE_FORMAT_PCM 1U
#define WAVE_MAPPER 0xFFFFFFFFU
#define WHDR_DONE 0x00000001U
#define WHDR_PREPARED 0x00000002U
#define WHDR_INQUEUE 0x00000010U
#define WOM_OPEN 0x03BBU
#define WOM_CLOSE 0x03BCU
#define WOM_DONE 0x03BDU
#define TIME_MS 0x0001U
#define TIME_SAMPLES 0x0002U
#define TIME_BYTES 0x0004U
#define TIME_ONESHOT 0U
#define TIME_PERIODIC 1U
#define MM_WAVE_BASE 0x7B000000U
#define MM_WAVE_SLOTS 4U
#define MM_QUEUE_SLOTS 8U
#define MM_TIMER_SLOTS 8U

typedef struct PACKED {
    uint16_t format_tag, channels;
    uint32_t samples_per_second, average_bytes_per_second;
    uint16_t block_align, bits_per_sample, extra_size;
} wave_format_t;

typedef struct PACKED {
    char *data;
    uint32_t buffer_length, bytes_recorded, user, flags, loops;
    void *next;
    uint32_t reserved;
} wave_header_t;

typedef struct PACKED {
    uint32_t type;
    union { uint32_t ms, sample, bytes; } value;
} mm_time_t;

typedef void (WIN32_API *wave_callback_t)(void *, uint32_t, uint32_t,
                                          uint32_t, uint32_t);
typedef void (WIN32_API *timer_callback_t)(uint32_t, uint32_t, uint32_t,
                                           uint32_t, uint32_t);

typedef struct {
    wave_header_t *header;
    uint8_t *samples;
    uint32_t sample_count;
} wave_queue_entry_t;

typedef struct {
    bool used, paused, playing;
    uint32_t owner, callback, callback_flags, instance;
    wave_format_t format;
    wave_queue_entry_t queue[MM_QUEUE_SLOTS];
    uint8_t head, tail, count;
    uint32_t completed_bytes;
    uint8_t volume;
} wave_device_t;

typedef struct {
    bool used;
    uint32_t owner, id, delay, next, callback, user, mode;
} mm_timer_t;

static wave_device_t wave_devices[MM_WAVE_SLOTS];
static mm_timer_t mm_timers[MM_TIMER_SLOTS];
static uint32_t next_timer_id = 1U;

static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static uint32_t mm_milliseconds(void){uint32_t hz=pit_get_frequency_hz();return hz?(uint32_t)(((uint64_t)pit_get_ticks()*1000U)/hz):0U;}
static uint32_t WIN32_API mm_timeGetTime(void){return mm_milliseconds();}
static uint32_t WIN32_API mm_timeBeginPeriod(uint32_t period UNUSED){return TIMERR_NOERROR;}
static uint32_t WIN32_API mm_timeEndPeriod(uint32_t period UNUSED){return TIMERR_NOERROR;}
static uint32_t WIN32_API mm_timeGetDevCaps(void*raw,uint32_t size){if(!raw||size<8U)return TIMERR_NOCANDO;((uint32_t*)raw)[0]=1U;((uint32_t*)raw)[1]=1000U;return TIMERR_NOERROR;}
static uint32_t WIN32_API mm_timeGetSystemTime(void*raw,uint32_t size){if(!raw||size<8U)return TIMERR_NOCANDO;((uint32_t*)raw)[0]=1U;((uint32_t*)raw)[1]=mm_milliseconds();return TIMERR_NOERROR;}
static bool sound_path(const char*source,char*out){uint32_t i=0,j=0;if(!source||!out)return false;if(source[0]&&source[1]==':')i=2;while(source[i]&&j+1U<VFS_MAX_PATH){out[j++]=source[i]=='\\'?'/':source[i];i++;}out[j]=0;return source[i]==0;}
static int WIN32_API mm_PlaySoundA(const char*sound,void*module UNUSED,uint32_t flags){char path[VFS_MAX_PATH];if(flags&SND_PURGE){sound_stop();return 1;}if(!sound)return 0;if(flags&SND_MEMORY)return 0;if(!sound_path(sound,path))return 0;(void)(flags&SND_ASYNC);return sound_play_file(path)?1:0;}
static int WIN32_API mm_sndPlaySoundA(const char*sound,uint32_t flags){return mm_PlaySoundA(sound,NULL,flags);}
static int WIN32_API mm_PlaySoundW(const uint16_t*sound,void*module,uint32_t flags){char path[VFS_MAX_PATH];uint32_t i=0;if(!sound)return mm_PlaySoundA(NULL,module,flags);while(sound[i]&&i+1U<sizeof(path)){path[i]=(char)(sound[i]<=255U?sound[i]:'?');i++;}path[i]=0;return mm_PlaySoundA(path,module,flags);}
static int WIN32_API mm_sndPlaySoundW(const uint16_t*sound,uint32_t flags){return mm_PlaySoundW(sound,NULL,flags);}

static wave_device_t *wave_from(void *handle) {
    uint32_t value=(uint32_t)(uintptr_t)handle,slot;
    if(value<MM_WAVE_BASE||value>=MM_WAVE_BASE+MM_WAVE_SLOTS)return NULL;
    slot=value-MM_WAVE_BASE;
    return wave_devices[slot].used&&wave_devices[slot].owner==task_current_process_id()?&wave_devices[slot]:NULL;
}
static void *wave_handle(wave_device_t *device){return(void*)(uintptr_t)(MM_WAVE_BASE+(uint32_t)(device-wave_devices));}
static void wave_notify(wave_device_t *device,uint32_t message,wave_header_t *header){uint32_t type;if(!device||!device->callback)return;type=device->callback_flags&CALLBACK_TYPEMASK;if(type==CALLBACK_WINDOW)(void)win32_user_post_message((void*)(uintptr_t)device->callback,message,(uint32_t)(uintptr_t)wave_handle(device),(int32_t)(uintptr_t)header);else if(type==CALLBACK_THREAD)(void)win32_user_post_message(NULL,message,(uint32_t)(uintptr_t)wave_handle(device),(int32_t)(uintptr_t)header);else if(type==CALLBACK_FUNCTION)((wave_callback_t)(uintptr_t)device->callback)(wave_handle(device),message,device->instance,(uint32_t)(uintptr_t)header,0U);}
static uint8_t *wave_convert(const wave_format_t *format,const char *data,uint32_t bytes,uint32_t *samples_out){uint32_t frames,channels,bits,out_count;uint8_t*out;if(!format||!data||!samples_out||!format->block_align)return NULL;channels=format->channels;bits=format->bits_per_sample;frames=bytes/format->block_align;out_count=frames;out=(uint8_t*)kmalloc(out_count?out_count:1U);if(!out)return NULL;for(uint32_t i=0;i<frames;i++){uint32_t sum=0;if(bits==8U){const uint8_t*source=(const uint8_t*)data+i*format->block_align;for(uint32_t c=0;c<channels;c++)sum+=source[c];out[i]=(uint8_t)(sum/channels);}else{const int16_t*source=(const int16_t*)((const uint8_t*)data+i*format->block_align);int32_t signed_sum=0;for(uint32_t c=0;c<channels;c++)signed_sum+=source[c];signed_sum/=(int32_t)channels;out[i]=(uint8_t)((signed_sum>>8)+128);}}*samples_out=out_count;return out;}
static void wave_finish_current(wave_device_t *device){wave_queue_entry_t*entry;if(!device||!device->count)return;entry=&device->queue[device->head];if(entry->header){entry->header->flags&=~WHDR_INQUEUE;entry->header->flags|=WHDR_DONE;device->completed_bytes+=entry->header->buffer_length;}if(entry->samples)kfree(entry->samples);wave_notify(device,WOM_DONE,entry->header);kmemset(entry,0,sizeof(*entry));device->head=(uint8_t)((device->head+1U)%MM_QUEUE_SLOTS);device->count--;device->playing=false;}
static void wave_start_next(wave_device_t *device){wave_queue_entry_t*entry;if(!device||device->paused||device->playing||!device->count)return;entry=&device->queue[device->head];if(!entry->samples||!entry->sample_count){wave_finish_current(device);return;}if(sound_play_pcm_u8(entry->samples,entry->sample_count,(uint16_t)device->format.samples_per_second,device->volume))device->playing=true;}

static uint32_t WIN32_API mm_waveOutGetNumDevs(void){return sound_pcm_available()?1U:0U;}
static uint32_t WIN32_API mm_waveInGetNumDevs(void){return 0U;}
static uint32_t WIN32_API mm_midiOutGetNumDevs(void){return 0U;}
static uint32_t WIN32_API mm_midiInGetNumDevs(void){return 0U;}
static uint32_t WIN32_API mm_auxGetNumDevs(void){return 0U;}
static uint32_t WIN32_API mm_joyGetNumDevs(void){return 0U;}
static uint32_t WIN32_API mm_mixerGetNumDevs(void){return 0U;}
static uint32_t WIN32_API mm_mmsystemGetVersion(void){return 0x0400U;}
static uint32_t WIN32_API mm_waveOutGetDevCapsA(uint32_t device,void*raw,uint32_t size){uint8_t*out=(uint8_t*)raw;if((device!=0U&&device!=WAVE_MAPPER)||!raw||size<20U)return MMSYSERR_BADDEVICEID;kmemset(out,0,size);*(uint16_t*)(out+0)=0xFFFFU;*(uint16_t*)(out+2)=1U;*(uint32_t*)(out+4)=0x00040000U;kstrncpy((char*)out+8,sound_pcm_name(),size-8U);if(size>=52U){*(uint32_t*)(out+40)=0x00000FFFU;*(uint16_t*)(out+44)=1U;*(uint16_t*)(out+46)=0U;*(uint32_t*)(out+48)=0x00000003U;}return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutGetDevCapsW(uint32_t device,void*raw,uint32_t size){uint8_t temp[80];uint16_t*out=(uint16_t*)raw;uint32_t result=mm_waveOutGetDevCapsA(device,temp,sizeof(temp));if(result||!raw)return result;kmemset(raw,0,size);if(size>=8U)kmemcpy(raw,temp,8U);for(uint32_t i=0;i<31U&&8U+(i+1U)*2U<=size;i++)out[4U+i]=(uint8_t)temp[8U+i];if(size>=84U)kmemcpy((uint8_t*)raw+72,temp+40,12U);return result;}
static uint32_t WIN32_API mm_waveOutOpen(void**handle,uint32_t device,const wave_format_t*format,uint32_t callback,uint32_t instance,uint32_t flags){if(!handle||!format)return MMSYSERR_INVALPARAM;*handle=NULL;if((device!=0U&&device!=WAVE_MAPPER)||!sound_pcm_available())return MMSYSERR_NODRIVER;if(format->format_tag!=WAVE_FORMAT_PCM||!format->channels||format->channels>2U||(format->bits_per_sample!=8U&&format->bits_per_sample!=16U)||format->samples_per_second<4000U||format->samples_per_second>48000U)return WAVERR_BADFORMAT;for(uint32_t i=0;i<MM_WAVE_SLOTS;i++)if(!wave_devices[i].used){wave_device_t*d=&wave_devices[i];kmemset(d,0,sizeof(*d));d->used=true;d->owner=task_current_process_id();d->callback=callback;d->callback_flags=flags;d->instance=instance;d->format=*format;d->volume=255U;*handle=wave_handle(d);wave_notify(d,WOM_OPEN,NULL);return MMSYSERR_NOERROR;}return MMSYSERR_ALLOCATED;}
static uint32_t WIN32_API mm_waveOutPrepareHeader(void*handle,wave_header_t*header,uint32_t size){wave_device_t*d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;if(!header||size<sizeof(*header)||!header->data)return MMSYSERR_INVALPARAM;header->flags|=WHDR_PREPARED|WHDR_DONE;header->flags&=~WHDR_INQUEUE;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutUnprepareHeader(void*handle,wave_header_t*header,uint32_t size){wave_device_t*d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;if(!header||size<sizeof(*header))return MMSYSERR_INVALPARAM;if(header->flags&WHDR_INQUEUE)return WAVERR_STILLPLAYING;header->flags&=~WHDR_PREPARED;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutWrite(void*handle,wave_header_t*header,uint32_t size){wave_device_t*d=wave_from(handle);wave_queue_entry_t*entry;uint32_t samples;if(!d)return MMSYSERR_INVALHANDLE;if(!header||size<sizeof(*header)||!(header->flags&WHDR_PREPARED))return WAVERR_UNPREPARED;if(header->flags&WHDR_INQUEUE)return WAVERR_STILLPLAYING;if(d->count>=MM_QUEUE_SLOTS)return MMSYSERR_NOMEM;entry=&d->queue[d->tail];entry->samples=wave_convert(&d->format,header->data,header->buffer_length,&samples);if(!entry->samples)return MMSYSERR_NOMEM;entry->header=header;entry->sample_count=samples;header->flags&=~WHDR_DONE;header->flags|=WHDR_INQUEUE;d->tail=(uint8_t)((d->tail+1U)%MM_QUEUE_SLOTS);d->count++;wave_start_next(d);return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutPause(void*handle){wave_device_t*d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;d->paused=true;if(d->playing){sound_stop();d->playing=false;}return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutRestart(void*handle){wave_device_t*d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;d->paused=false;wave_start_next(d);return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutReset(void*handle){wave_device_t*d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;if(d->playing)sound_stop();d->playing=false;while(d->count)wave_finish_current(d);d->head=d->tail=0U;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutClose(void*handle){wave_device_t*d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;if(d->count||d->playing)return WAVERR_STILLPLAYING;wave_notify(d,WOM_CLOSE,NULL);kmemset(d,0,sizeof(*d));return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutGetPosition(void*handle,mm_time_t*time,uint32_t size){wave_device_t*d=wave_from(handle);uint32_t bytes;if(!d)return MMSYSERR_INVALHANDLE;if(!time||size<sizeof(*time))return MMSYSERR_INVALPARAM;bytes=d->completed_bytes;if(d->count&&d->queue[d->head].header)bytes+=d->queue[d->head].header->buffer_length;if(time->type==TIME_BYTES)time->value.bytes=bytes;else if(time->type==TIME_SAMPLES)time->value.sample=d->format.block_align?bytes/d->format.block_align:0U;else{time->type=TIME_MS;time->value.ms=d->format.average_bytes_per_second?(uint32_t)(((uint64_t)bytes*1000U)/d->format.average_bytes_per_second):0U;}return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutGetVolume(void*handle,uint32_t*volume){wave_device_t*d;if(!volume)return MMSYSERR_INVALPARAM;if(!handle){*volume=0xFFFFFFFFU;return MMSYSERR_NOERROR;}d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;*volume=(uint32_t)d->volume*0x0101U;*volume|=*volume<<16;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutSetVolume(void*handle,uint32_t volume){wave_device_t*d;if(!handle){for(uint32_t i=0;i<MM_WAVE_SLOTS;i++)if(wave_devices[i].used)wave_devices[i].volume=(uint8_t)(((volume&0xFFFFU)+(volume>>16))/514U);return MMSYSERR_NOERROR;}d=wave_from(handle);if(!d)return MMSYSERR_INVALHANDLE;d->volume=(uint8_t)(((volume&0xFFFFU)+(volume>>16))/514U);return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutGetID(void*handle,uint32_t*id){if(!wave_from(handle))return MMSYSERR_INVALHANDLE;if(!id)return MMSYSERR_INVALPARAM;*id=0U;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutBreakLoop(void*handle){return wave_from(handle)?MMSYSERR_NOERROR:MMSYSERR_INVALHANDLE;}
static const char *wave_error_text(uint32_t error){switch(error){case MMSYSERR_NOERROR:return"No error";case MMSYSERR_BADDEVICEID:return"Bad device ID";case MMSYSERR_ALLOCATED:return"Device already allocated";case MMSYSERR_INVALHANDLE:return"Invalid handle";case MMSYSERR_NODRIVER:return"No audio driver";case MMSYSERR_NOMEM:return"Out of memory";case WAVERR_BADFORMAT:return"Unsupported wave format";case WAVERR_STILLPLAYING:return"Buffers still playing";case WAVERR_UNPREPARED:return"Header not prepared";default:return"Multimedia error";}}
static uint32_t WIN32_API mm_waveOutGetErrorTextA(uint32_t error,char*out,uint32_t size){const char*text=wave_error_text(error);if(!out||!size)return MMSYSERR_INVALPARAM;kstrncpy(out,text,size-1U);out[size-1U]=0;return MMSYSERR_NOERROR;}
static uint32_t WIN32_API mm_waveOutGetErrorTextW(uint32_t error,uint16_t*out,uint32_t size){const char*text=wave_error_text(error);uint32_t i=0;if(!out||!size)return MMSYSERR_INVALPARAM;while(text[i]&&i+1U<size){out[i]=(uint8_t)text[i];i++;}out[i]=0;return MMSYSERR_NOERROR;}

static uint32_t WIN32_API mm_timeSetEvent(uint32_t delay,uint32_t resolution UNUSED,uint32_t callback,uint32_t user,uint32_t mode){uint32_t now=mm_milliseconds();if(!delay||!callback)return 0U;for(uint32_t i=0;i<MM_TIMER_SLOTS;i++)if(!mm_timers[i].used){mm_timer_t*t=&mm_timers[i];kmemset(t,0,sizeof(*t));t->used=true;t->owner=task_current_process_id();t->id=next_timer_id++;if(!t->id)t->id=next_timer_id++;t->delay=delay;t->next=now+delay;t->callback=callback;t->user=user;t->mode=mode;return t->id;}return 0U;}
static uint32_t WIN32_API mm_timeKillEvent(uint32_t id){for(uint32_t i=0;i<MM_TIMER_SLOTS;i++)if(mm_timers[i].used&&mm_timers[i].owner==task_current_process_id()&&mm_timers[i].id==id){kmemset(&mm_timers[i],0,sizeof(mm_timers[i]));return TIMERR_NOERROR;}return TIMERR_NOCANDO;}

void win32_winmm_poll(void){uint32_t owner=task_current_process_id(),now=mm_milliseconds();for(uint32_t i=0;i<MM_WAVE_SLOTS;i++){wave_device_t*d=&wave_devices[i];if(!d->used||d->owner!=owner)continue;if(d->playing&&!sound_pcm_is_busy())wave_finish_current(d);wave_start_next(d);}for(uint32_t i=0;i<MM_TIMER_SLOTS;i++){mm_timer_t*t=&mm_timers[i];if(!t->used||t->owner!=owner||(int32_t)(now-t->next)<0)continue;((timer_callback_t)(uintptr_t)t->callback)(t->id,0U,t->user,0U,0U);if((t->mode&1U)==TIME_PERIODIC)t->next=now+t->delay;else kmemset(t,0,sizeof(*t));}}
void win32_winmm_cleanup_process(uint32_t owner){for(uint32_t i=0;i<MM_WAVE_SLOTS;i++)if(wave_devices[i].used&&wave_devices[i].owner==owner){wave_device_t*d=&wave_devices[i];if(d->playing)sound_stop();while(d->count)wave_finish_current(d);kmemset(d,0,sizeof(*d));}for(uint32_t i=0;i<MM_TIMER_SLOTS;i++)if(mm_timers[i].used&&mm_timers[i].owner==owner)kmemset(&mm_timers[i],0,sizeof(mm_timers[i]));}

uint32_t win32_winmm_resolve(const char*name){
#define M(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&mm_##api
 M(timeGetTime);M(timeBeginPeriod);M(timeEndPeriod);M(timeGetDevCaps);M(timeGetSystemTime);M(timeSetEvent);M(timeKillEvent);M(PlaySoundA);M(PlaySoundW);M(sndPlaySoundA);M(sndPlaySoundW);M(waveOutGetNumDevs);M(waveOutGetDevCapsA);M(waveOutGetDevCapsW);M(waveOutOpen);M(waveOutPrepareHeader);M(waveOutUnprepareHeader);M(waveOutWrite);M(waveOutPause);M(waveOutRestart);M(waveOutReset);M(waveOutClose);M(waveOutGetPosition);M(waveOutGetErrorTextA);M(waveOutGetErrorTextW);M(waveOutGetID);M(waveOutBreakLoop);M(waveInGetNumDevs);M(midiOutGetNumDevs);M(midiInGetNumDevs);M(auxGetNumDevs);M(joyGetNumDevs);M(mixerGetNumDevs);M(mmsystemGetVersion);M(waveOutGetVolume);M(waveOutSetVolume);
#undef M
 return 0;
}
uint32_t win32_winmm_resolve_ordinal(uint16_t ordinal){return ordinal==2U?(uint32_t)(uintptr_t)&mm_PlaySoundA:0U;}
