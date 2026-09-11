// Kalwer Doomgeneric adapter, GPL-2.0-or-later (same terms as the engine).
#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
void KoomAudioInit(const char*,int);
void KoomAudioFrame(unsigned int);
static uint32_t ticks=0;
static unsigned char keys[256],previous[256];
void DG_Init(void){}
void DG_DrawFrame(void){}
void DG_SleepMs(uint32_t ms){ticks+=ms;}
uint32_t DG_GetTicksMs(void){return ticks;}
void DG_SetWindowTitle(const char* title){(void)title;}
int DG_GetKey(int* pressed,unsigned char* key){for(int i=0;i<256;i++)if(keys[i]!=previous[i]){*key=i;*pressed=keys[i];previous[i]=keys[i];return 1;}return 0;}
int main(int argc,char** argv){
    int framefd=dup(1);dup2(2,1);
#ifdef _WIN32
    _setmode(framefd,_O_BINARY);_setmode(0,_O_BINARY);
#endif
    FILE* frames=fdopen(framefd,"wb");setvbuf(frames,NULL,_IONBF,0);
    const char* font="TimGM6mb.sf2";int audio=1,fieldkit=0;
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"-soundfont") && i+1<argc)font=argv[++i];else if(!strcmp(argv[i],"-nosound"))audio=0;else if(!strcmp(argv[i],"-kalwer-fieldkit"))fieldkit=1;}
    KoomAudioInit(font,audio);
    doomgeneric_Create(argc,argv);
    uint32_t completed_maps=0;int intermission=0,kit_map=-1,kit_episode=-1,previous_leveltime=-1;
    while(fread(keys,1,sizeof(keys),stdin)==sizeof(keys)) {
        ticks+=29;doomgeneric_Tick();KoomAudioFrame(29);
        if(fieldkit && gamestate==GS_LEVEL && players[consoleplayer].mo && !demoplayback){
            if(kit_map!=gamemap || kit_episode!=gameepisode || leveltime<previous_leveltime){
                player_t* player=&players[consoleplayer];
                if(player->health<150)player->health=150;
                player->mo->health=player->health;
                if(player->armorpoints<150){player->armorpoints=150;player->armortype=2;}
                kit_map=gamemap;kit_episode=gameepisode;
                fprintf(stderr,"Kalwer field kit: health %d, armor %d, map %d:%d\n",player->health,player->armorpoints,gameepisode,gamemap);
            }
            previous_leveltime=leveltime;
        }
        if(gamestate==GS_INTERMISSION && !intermission)++completed_maps;
        intermission=gamestate==GS_INTERMISSION;
        uint32_t header[4]={0x4b4f4f4d,DOOMGENERIC_RESX,DOOMGENERIC_RESY,completed_maps};
        if(fwrite(header,sizeof(header),1,frames)!=1 || fwrite(DG_ScreenBuffer,4,DOOMGENERIC_RESX*DOOMGENERIC_RESY,frames)!=DOOMGENERIC_RESX*DOOMGENERIC_RESY)break;
    }
    return 0;
}
