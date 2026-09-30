/* Host test: run SelCall's decoder on raw 12-bit ADC samples (8 kHz, uint16 LE).
 * Build: cc -O2 -DHOST_TEST -o host_decode test/host_decode.c
 * Use:   ./host_decode samples.u16 [gap_ms]   (gap_ms simulates sampling gaps per block)
 * Prints each decoded call; a repeat of the newest call is printed again. */
#include <stdio.h>
#include <stdlib.h>
#include "../app/selcall/selcall_app.c"

int main(int argc, char **argv){
    if(argc<2){ fprintf(stderr,"usage: %s samples.u16 [gap_ms]\n",argv[0]); return 2; }
    FILE *f=fopen(argv[1],"rb");
    if(!f){ perror(argv[1]); return 1; }
    fseek(f,0,SEEK_END); long n=ftell(f)/2; fseek(f,0,SEEK_SET);
    uint16_t *s=malloc((size_t)n*2);
    if(!s || fread(s,2,(size_t)n,f)!=(size_t)n){ fprintf(stderr,"read error\n"); return 1; }
    fclose(f);
    long gap = argc>2 ? atol(argv[2])*(long)FS/1000 : 0;

    for(uint8_t i=0;i<HIST_N;i++){ hist[i][0]='\0'; histCnt[i]=0; }
    curLen=0; lastSym=0; quiet=END_BLKS;
    char last[MAX_LEN+1]=""; uint8_t lastCnt=0;

    for(long b=0; b+(long)N<=n; b+=N+gap){
        uint32_t sum=0; uint16_t mn=4095, mx=0;
        for(uint16_t i=0;i<N;i++){ uint16_t v=s[b+i]; buf[i]=(int16_t)v; sum+=v; if(v<mn)mn=v; if(v>mx)mx=v; }
        mean=(uint16_t)(sum/N); pp=(uint16_t)(mx-mn);
        analyse(true);
        if(!seq(hist[0],last) || histCnt[0]!=lastCnt){ puts(hist[0]); scpy(last,hist[0]); lastCnt=histCnt[0]; }
    }
    free(s);
    return 0;
}
