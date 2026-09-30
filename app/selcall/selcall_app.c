/* SelCall: CCIR 5-tone decoder for the Quansheng UV-K1 (F4HWN v6.0.0 Labs overlay app)
 * SPDX-License-Identifier: Apache-2.0
 *
 * The BK4829's built-in SelCall detector only reports the first of several back-to-back
 * tones, so decoding is done in software. Received audio is sampled on PA4 (voice DAC
 * output, also ADC channel 4) at 8 kHz, Goertzel filters pick the strongest CCIR tone per
 * 32 ms block, and tones are assembled into calls. Audio is passed through as NFM.
 *
 * Keys: 3 monitor, MENU clear history, EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#ifndef HOST_TEST
#include "py32f0xx.h"
#include "py32f071_ll_adc.h"
#include "py32f071_ll_dac.h"
#include "py32f071_ll_gpio.h"
#include "../app_api.h"
#else
typedef struct app_api app_api_t;               /* host test: decoder only */
#endif

#define REG_0C 0x0C
#define REG_43 0x43

#define FS        8000u
#define N         256u              /* samples per block (32 ms)            */
#define NT        15u
#define MIN_PUR   50u               /* min. % of block energy in the tone   */
#define MIN_PP    40u               /* min. level, ADC counts peak-to-peak  */
#define END_BLKS  3u                /* silent blocks that end a call        */
#define MIN_LEN   3u
#define MAX_LEN   10u
#define HIST_N    4u
#define LED_BLKS  8u
#define BAT_BLKS  375u              /* battery refresh interval (~12 s)     */
#define CFG_MAGIC 0x5D

/* Goertzel coefficients, 2*cos(2*pi*f/FS) in Q13. CCIR tones:
 * 0-9: 1981 1124 1197 1275 1358 1446 1540 1640 1747 1860 Hz,
 * A 2400, B 930, C 2247, D 991, E (repeat) 2110 Hz */
static const int16_t COEF[NT] = {244,10404,9661,8833,7916,6906,5791,4571,3234,1798,-5063,12204,-3158,11667,-1414};
static const char SYM[] = "0123456789ABCDE";

static const app_api_t *A;
static int16_t  buf[N];
static uint16_t pp, mean;

static char     cur[MAX_LEN + 1];
static uint8_t  curLen, quiet;
static char     lastSym;
static char     hist[HIST_N][MAX_LEN + 1];     /* [0] is the newest call */
static uint8_t  histCnt[HIST_N];

static bool     monitor, audioOn, sqOpen, running, dirty;
static uint8_t  prevKey, ledT;
static uint16_t batT;
static char     str[20];

static uint32_t saveCh, saveSmp;
static bool     dacWasOn;

/* ---- string helpers ---- */
static char *putu(char *o,uint32_t u){ char t[10]; uint8_t n=0; do{t[n++]=(char)('0'+u%10u);u/=10u;}while(u); while(n)*o++=t[--n]; *o='\0'; return o; }
static uint8_t slen(const char *s){ uint8_t n=0; while(s[n]) n++; return n; }
static bool    seq(const char *a,const char *b){ while(*a && *a==*b){a++;b++;} return *a==*b; }
static void    scpy(char *d,const char *s){ while((*d++=*s++)); }

/* ---- decoder (no hardware access, host-testable with -DHOST_TEST) ---- */
/* Store a finished call; repeats of the newest call only bump its count. */
static void commitCall(void){
    cur[curLen]='\0';
    if(curLen>=MIN_LEN){
        if(seq(cur,hist[0])){ if(histCnt[0]<99) histCnt[0]++; }
        else{
            for(uint8_t i=HIST_N-1;i>0;i--){ scpy(hist[i],hist[i-1]); histCnt[i]=histCnt[i-1]; }
            scpy(hist[0],cur); histCnt[0]=1;
        }
        ledT=LED_BLKS;
#ifndef HOST_TEST
        A->led(true); A->backlight_on();
#endif
    }
    curLen=0; cur[0]='\0'; lastSym=0;
    dirty=true;
}

/* Find the strongest CCIR tone in buf[] and feed it to the call assembler. */
static void analyse(bool open){
    int32_t energy=0;
    for(uint16_t i=0;i<N;i++){ int32_t x=(buf[i]-(int32_t)mean)>>2; buf[i]=(int16_t)x; energy+=x*x; }
    int64_t bestP=0; uint8_t best=0;
    for(uint8_t k=0;k<NT;k++){                     /* Goertzel filter per tone */
        int32_t s1=0,s2=0,c=COEF[k];
        for(uint16_t i=0;i<N;i++){ int32_t s=buf[i]+((c*s1)>>13)-s2; s2=s1; s1=s; }
        int64_t p=(int64_t)s1*s1+(int64_t)s2*s2-(int64_t)((c*s1)>>13)*s2;
        if(p>bestP){ bestP=p; best=k; }
    }
    /* purity: a pure tone gives |X|^2 = energy*N/2, i.e. 100 % */
    uint64_t ref=(uint64_t)(uint32_t)energy*(N/2u);
    uint64_t bp=(uint64_t)bestP;
    while(ref>0xFFFFFFu){ ref>>=1; bp>>=1; }
    uint32_t pur = ref ? (uint32_t)(bp*100u)/(uint32_t)ref : 0;

    if(open && pp>=MIN_PP && pur>=MIN_PUR){
        quiet=0;
        char s=SYM[best];
        if(s!=lastSym){                            /* new tone; E repeats the last digit */
            lastSym=s;
            char d = (s=='E') ? (curLen ? cur[curLen-1] : 0) : s;
            if(d){ cur[curLen++]=d; cur[curLen]='\0'; dirty=true; }
            if(curLen>=MAX_LEN) commitCall();
        }
    } else if(quiet<END_BLKS && ++quiet==END_BLKS && (curLen || lastSym)){  /* call ended */
        commitCall();
    }
}

#ifndef HOST_TEST
/* ---- audio and ADC ---- */
static void setAudio(bool on){
    if(on==audioOn) return;
    audioOn=on;
    if(on){ A->audio_path(true); A->set_af(APP_AF_FM); }
    else  { A->set_af(APP_AF_MUTE); A->audio_path(false); }
}

static uint16_t adcRead(void){
    LL_ADC_ClearFlag_EOS(ADC1);
    LL_ADC_REG_StartConversionSWStart(ADC1);
    uint16_t guard=2000;
    while(!LL_ADC_IsActiveFlag_EOS(ADC1) && --guard);
    LL_ADC_ClearFlag_EOS(ADC1);
    return (uint16_t)LL_ADC_REG_ReadConversionData12(ADC1);
}

/* Fill buf[] with N samples at FS, paced by the SysTick counter. */
static void acquire(void){
    const uint32_t load=SysTick->LOAD+1u;
    const uint32_t step=load/(FS/100u);
    uint32_t prev=SysTick->VAL, acc=0, sum=0;
    uint16_t mn=4095, mx=0;
    for(uint16_t i=0;i<N;i++){
        while(acc<step){
            uint32_t c=SysTick->VAL;
            acc+=(prev>=c)?(prev-c):(prev+load-c);
            prev=c;
        }
        acc-=step;
        uint16_t v=adcRead();
        buf[i]=(int16_t)v; sum+=v;
        if(v<mn) mn=v;
        if(v>mx) mx=v;
    }
    mean=(uint16_t)(sum/N);
    pp=(uint16_t)(mx-mn);
}

/* Point ADC rank 1 at PA4, or give it back to the battery channel. */
static void adcTake(void){
    LL_ADC_REG_SetSequencerRanks(ADC1,LL_ADC_REG_RANK_1,LL_ADC_CHANNEL_4);
    LL_ADC_SetChannelSamplingTime(ADC1,LL_ADC_CHANNEL_4,LL_ADC_SAMPLINGTIME_41CYCLES_5);
}
static void adcGive(void){
    LL_ADC_REG_SetSequencerRanks(ADC1,LL_ADC_REG_RANK_1,saveCh);
    LL_ADC_SetChannelSamplingTime(ADC1,LL_ADC_CHANNEL_4,saveSmp);
}

/* ---- UI ---- */
static void tag(const char *s,uint8_t x,uint8_t line){
    A->print_inverse(s,x,line,false,true,(uint8_t)(x+slen(s)*4));
}

static void draw(void){
    char *o;
    A->display_clear();
    A->status_clear();
    A->print_inverse("SELCALL",2,0,true,true,30);
    A->print_tiny("CCIR",34,1,true,true);
    A->draw_battery();

    /* large digits: the call being received, otherwise the newest call */
    const char *c = curLen ? cur : (hist[0][0] ? hist[0] : "-----");
    bool digitsOnly=true;
    for(const char *p=c;*p;p++) if(!((*p>='0'&&*p<='9')||*p=='-')) digitsOnly=false;
    uint8_t len=slen(c);
    uint8_t cols = len<5 ? 5 : len;              /* fixed 5-digit field */
    if(digitsOnly) A->display_freq(c,(uint8_t)((128-cols*13)/2),0,false);
    else           A->print_string(c,0,127,0,10);

    /* repeat count of the newest call */
    if(!curLen && histCnt[0]>1){ o=str; *o++='x'; putu(o,histCnt[0]); A->print_normal(str,(uint8_t)(126-slen(str)*7),0,2); }

    /* earlier calls (the newest moves down while a call is coming in) */
    for(uint8_t row=0, i=curLen?0:1; row<3 && i<HIST_N; row++, i++){
        if(!hist[i][0]) break;
        A->print_normal(hist[i],10,0,(uint8_t)(3+row));
        if(histCnt[i]>1){ o=str; *o++='x'; putu(o,histCnt[i]); A->print_normal(str,(uint8_t)(126-slen(str)*7),0,(uint8_t)(3+row)); }
    }

    /* frequency and indicators */
    uint32_t f=A->rx_freq();
    o=putu(str,f/100000u); *o++='.';
    for(uint32_t p=10000u, r=f%100000u; p; p/=10u) *o++=(char)('0'+(r/p)%10u);
    *o='\0';
    A->print_normal(str,0,0,6);
    tag("NFM",78,6);
    if(sqOpen) tag("RX",96,6);
    if(monitor) tag("MON",110,6);

    A->blit_status();
    A->blit_full();
}

static void handleKeys(void){
    uint8_t key=A->get_key();
    if(key==prevKey) return;
    prevKey=key;
    if(key==APP_KEY_INVALID) return;
    A->backlight_on();
    switch(key){
        case APP_KEY_EXIT: running=false; return;
        case APP_KEY_3:    monitor=!monitor; break;
        case APP_KEY_MENU:
            for(uint8_t i=0;i<HIST_N;i++){ hist[i][0]='\0'; histCnt[i]=0; }
            curLen=0; cur[0]='\0'; lastSym=0; break;
        default: return;
    }
    dirty=true;
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    A=api;
    uint8_t cfg[2];
    A->cfg_load(cfg,2);
    monitor = (cfg[0]==CFG_MAGIC) && (cfg[1]&1u);
    for(uint8_t i=0;i<HIST_N;i++){ hist[i][0]='\0'; histCnt[i]=0; }
    curLen=0; cur[0]='\0'; lastSym=0; quiet=END_BLKS; ledT=0; batT=0;
    audioOn=true; sqOpen=false; prevKey=APP_KEY_INVALID;

    A->backlight_on();
    A->bk_write(REG_43,0x4048);                      /* NFM filter */

    /* take over PA4 and the ADC; restored on exit */
    saveCh =LL_ADC_REG_GetSequencerRanks(ADC1,LL_ADC_REG_RANK_1);
    saveSmp=LL_ADC_GetChannelSamplingTime(ADC1,LL_ADC_CHANNEL_4);
    dacWasOn=LL_DAC_IsEnabled(DAC1,LL_DAC_CHANNEL_1)!=0;
    if(dacWasOn) LL_DAC_Disable(DAC1,LL_DAC_CHANNEL_1);
    LL_GPIO_SetPinMode(GPIOA,LL_GPIO_PIN_4,LL_GPIO_MODE_ANALOG);
    adcTake();
    setAudio(false);

    running=true; dirty=true;
    while(running){
        handleKeys();
        if(!running) break;

        bool sq=(A->bk_read(REG_0C)&2u)!=0;
        if(sq!=sqOpen){ sqOpen=sq; dirty=true; }
        setAudio(monitor||sq);

        acquire();
        analyse(monitor||sq);

        if(ledT && --ledT==0) A->led(false);
        if(++batT>=BAT_BLKS){ batT=0; adcGive(); A->battery_sample(); adcTake(); dirty=true; }
        if(dirty){ dirty=false; draw(); }
        A->backlight_update();
    }

    cfg[0]=CFG_MAGIC; cfg[1]=monitor;
    A->cfg_save(cfg,2);
    adcGive();
    if(dacWasOn) LL_DAC_Enable(DAC1,LL_DAC_CHANNEL_1);
    A->led(false);
    setAudio(false);
}
#endif
