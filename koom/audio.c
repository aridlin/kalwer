// Kalwer Doom audio adapter. GPL-2.0-or-later.
// Playback consumes already mixed samples; synthesis advances only with game ticks.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#ifdef _WIN32
#define boolean windows_boolean
#endif
#include "../vendor/miniaudio/miniaudio.h"
#ifdef _WIN32
#undef boolean
#endif
#define TSF_IMPLEMENTATION
#include "../vendor/TinySoundFont/tsf.h"
#define TML_IMPLEMENTATION
#include "../vendor/TinySoundFont/tml.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "mus2mid.h"
#include <stdint.h>

#define RATE 48000
#define RING_FRAMES 8192
#define CHANNELS 16
static ma_device device;
static ma_mutex lock;
static int output_ready=0,lock_ready=0;
static float ring[RING_FRAMES*2];
static unsigned int read_frame=0,write_frame=0;
static FILE* capture;
static tsf* synth;
static tml_message *song,*event;
static double music_ms;
static int music_loop=0,music_paused=0,music_volume=90;
static int sound_prefix=1;
int use_libsamplerate=0;
float libsamplerate_scale=1.0f;
typedef struct {const unsigned char* pcm;unsigned int count;double offset,step;float left,right;} Channel;
static Channel channels[CHANNELS];
static void output(ma_device* d,void* buffer,const void* input,ma_uint32 count){
    (void)d;(void)input;float* out=buffer;memset(out,0,count*2*sizeof(float));
    ma_mutex_lock(&lock);
    for(unsigned int i=0;i<count && read_frame!=write_frame;i++,read_frame++){
        unsigned int at=read_frame%RING_FRAMES;out[i*2]=ring[at*2];out[i*2+1]=ring[at*2+1];
    }
    ma_mutex_unlock(&lock);
}
void KoomAudioShutdown(void){
    if(output_ready){ma_device_uninit(&device);output_ready=0;}
    if(lock_ready){ma_mutex_uninit(&lock);lock_ready=0;}
    if(synth){tsf_close(synth);synth=NULL;}
    if(capture){fclose(capture);capture=NULL;}
}
void KoomAudioInit(const char* font,int enabled){
    if(!enabled)return;
    synth=tsf_load_filename(font);
    if(synth){tsf_set_output(synth,TSF_STEREO_INTERLEAVED,RATE,-9);tsf_set_max_voices(synth,64);}
    const char* capture_path=getenv("KALWER_KOOM_AUDIO_CAPTURE");
    if(capture_path && *capture_path)capture=fopen(capture_path,"wb");
    if(!capture){
        ma_device_config config=ma_device_config_init(ma_device_type_playback);
        config.playback.format=ma_format_f32;config.playback.channels=2;config.sampleRate=RATE;config.dataCallback=output;
        config.periodSizeInMilliseconds=10;
        if(ma_mutex_init(&lock)==MA_SUCCESS){
            lock_ready=1;
            if(ma_device_init(NULL,&config,&device)==MA_SUCCESS){output_ready=1;if(ma_device_start(&device)!=MA_SUCCESS){ma_device_uninit(&device);output_ready=0;}}
        }
    }
    atexit(KoomAudioShutdown);
}
static void reset_instruments(void){
    if(!synth)return;tsf_reset(synth);
    for(int channel=0;channel<16;channel++)tsf_channel_set_presetnumber(synth,channel,0,channel==9);
}
static void midi_event(const tml_message* e){
    if(!synth)return;
    switch(e->type){
        case TML_NOTE_ON:tsf_channel_note_on(synth,e->channel,e->key,e->velocity/127.f);break;
        case TML_NOTE_OFF:tsf_channel_note_off(synth,e->channel,e->key);break;
        case TML_PROGRAM_CHANGE:tsf_channel_set_presetnumber(synth,e->channel,e->program,e->channel==9);break;
        case TML_CONTROL_CHANGE:tsf_channel_midi_control(synth,e->channel,e->control,e->control_value);break;
        case TML_PITCH_BEND:tsf_channel_set_pitchwheel(synth,e->channel,e->pitch_bend);break;
        default:break;
    }
}
void KoomAudioFrame(unsigned int milliseconds){
    if(milliseconds>100)milliseconds=100;
    unsigned int frames=RATE*milliseconds/1000;
    float mixed[4800*2];memset(mixed,0,frames*2*sizeof(float));
    if(synth && song && !music_paused){
        for(unsigned int offset=0;offset<frames;){
            while(event && event->time<=music_ms){midi_event(event);event=event->next;}
            if(!event && music_loop){event=song;music_ms=0;reset_instruments();while(event && event->time==0){midi_event(event);event=event->next;}}
            unsigned int count=frames-offset;if(count>64)count=64;
            tsf_render_float(synth,mixed+offset*2,count,0);offset+=count;music_ms+=count*1000./RATE;
        }
        float volume=music_volume/127.f;for(unsigned int i=0;i<frames*2;i++)mixed[i]*=volume;
    }
    for(int c=0;c<CHANNELS;c++){
        Channel* channel=&channels[c];if(!channel->pcm)continue;
        for(unsigned int i=0;i<frames;i++){
            unsigned int sample=(unsigned int)channel->offset;
            if(sample>=channel->count){channel->pcm=NULL;break;}
            unsigned int next=sample+1<channel->count?sample+1:sample;
            float part=(float)(channel->offset-sample);
            float value=((channel->pcm[sample]*(1-part)+channel->pcm[next]*part)-128)/128.f;
            mixed[i*2]+=value*channel->left;mixed[i*2+1]+=value*channel->right;channel->offset+=channel->step;
        }
    }
    for(unsigned int i=0;i<frames*2;i++){if(mixed[i]>1)mixed[i]=1;if(mixed[i]<-1)mixed[i]=-1;}
    if(capture){fwrite(mixed,sizeof(float),frames*2,capture);fflush(capture);}
    if(output_ready){
        ma_mutex_lock(&lock);
        for(unsigned int i=0;i<frames;i++,write_frame++){
            if(write_frame-read_frame>=RING_FRAMES)++read_frame;
            unsigned int at=write_frame%RING_FRAMES;ring[at*2]=mixed[i*2];ring[at*2+1]=mixed[i*2+1];
        }
        ma_mutex_unlock(&lock);
    }
}
static boolean sound_init(boolean prefix){sound_prefix=prefix;return true;}
static void noop(void){}
static int sound_lump(sfxinfo_t* info){char name[16];if(info->link)info=info->link;snprintf(name,sizeof(name),"%s%.8s",sound_prefix?"ds":"",info->name);return W_GetNumForName(name);}
static void parameters(int channel,int volume,int separation){
    if(channel<0 || channel>=CHANNELS)return;
    float pan=separation/255.f,level=volume/127.f*.45f;
    channels[channel].left=level*(1-pan);channels[channel].right=level*pan;
}
static int start_sound(sfxinfo_t* info,int channel,int volume,int separation){
    if(channel<0 || channel>=CHANNELS)return -1;
    int lump=info->lumpnum>=0?info->lumpnum:sound_lump(info);int length=W_LumpLength(lump);
    if(length<8)return -1;
    const unsigned char* data=W_CacheLumpNum(lump,PU_STATIC);
    unsigned int rate=data[2]|data[3]<<8,count=data[4]|data[5]<<8|data[6]<<16|(unsigned int)data[7]<<24;
    if(data[0]!=3 || data[1]!=0 || rate==0 || count>(unsigned int)length-8 || count<32)return -1;
    // DMX padding is not part of the audible sample.
    channels[channel]=(Channel){data+24,count-32,0,rate/(double)RATE,0,0};parameters(channel,volume,separation);return channel;
}
static void stop_sound(int channel){if(channel>=0 && channel<CHANNELS)channels[channel].pcm=NULL;}
static boolean sound_playing(int channel){return channel>=0 && channel<CHANNELS && channels[channel].pcm!=NULL;}
static snddevice_t devices[]={SNDDEVICE_SB};
sound_module_t DG_sound_module={devices,1,sound_init,noop,sound_lump,noop,parameters,start_sound,stop_sound,sound_playing,NULL};
static boolean music_init(void){return synth!=NULL;}
static void music_set_volume(int volume){music_volume=volume;}
static void music_pause(void){music_paused=1;}
static void music_resume(void){music_paused=0;}
static void* register_song(void* data,int length){
    if(length>=4 && memcmp(data,"MThd",4)==0)return tml_load_memory(data,length);
    MEMFILE* in=mem_fopen_read(data,length);MEMFILE* out=mem_fopen_write();void* bytes=NULL;size_t size=0;tml_message* result=NULL;
    if(!mus2mid(in,out)){mem_get_buf(out,&bytes,&size);result=tml_load_memory(bytes,(int)size);}
    mem_fclose(in);mem_fclose(out);return result;
}
static void unregister_song(void* handle){if(song==handle){song=event=NULL;}tml_free(handle);}
static void play_song(void* handle,boolean loop){song=event=handle;music_ms=0;music_loop=loop;music_paused=0;reset_instruments();}
static void stop_song(void){song=event=NULL;if(synth)tsf_reset(synth);}
static boolean music_playing(void){return song && (event || music_loop);}
music_module_t DG_music_module={devices,1,music_init,noop,music_set_volume,music_pause,music_resume,register_song,unregister_song,play_song,stop_song,music_playing,noop};
