/*
 * Baseline JPEG decoder for BlesKernOS.
 * Derived from NanoJPEG 1.3.5 by Martin J. Fiedler (MIT license).
 * The original copyright and permission notice are retained below.
 *
 * Copyright (c) 2009-2016 Martin J. Fiedler <martin.fiedler@gmx.net>
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "image.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/task.h"

#define NJ_CHROMA_FILTER 1
#define NJ_INLINE static inline
#define NJ_FORCE_INLINE static inline

typedef enum {
    NJ_OK = 0,
    NJ_NO_JPEG,
    NJ_UNSUPPORTED,
    NJ_OUT_OF_MEM,
    NJ_INTERNAL_ERR,
    NJ_SYNTAX_ERROR,
    NJ_FINISHED
} nj_result_t;

typedef struct { uint8_t bits, code; } nj_vlc_code_t;

typedef struct {
    int cid;
    int ssx, ssy;
    int width, height;
    int stride;
    int qtsel;
    int actabsel, dctabsel;
    int dcpred;
    uint8_t *pixels;
} nj_component_t;

typedef struct {
    nj_result_t error;
    const uint8_t *pos;
    int size;
    int length;
    int width, height;
    int mbwidth, mbheight;
    int mbsizex, mbsizey;
    int ncomp;
    nj_component_t comp[3];
    int qtused, qtavail;
    uint8_t qtab[4][64];
    nj_vlc_code_t vlctab[4][65536];
    uint32_t buf;
    int bufbits;
    int block[64];
    int rstinterval;
    uint8_t *rgb;
} nj_context_t;

/*
 * The VLC tables make this context about 513 KiB. Keeping it as a static
 * object inflated the kernel .bss until it collided with the stage-2 stack
 * at 0x1ff000. Allocate it only while a JPEG is being decoded instead.
 */
static nj_context_t *nj_ctx;
static bool nj_busy;
#define nj (*nj_ctx)

static bool nj_acquire(void) {
    bool acquired = false;
    task_preempt_disable();
    if (!nj_busy) {
        nj_busy = true;
        acquired = true;
    }
    task_preempt_enable();
    return acquired;
}

static void nj_release(void) {
    task_preempt_disable();
    nj_busy = false;
    task_preempt_enable();
}

static const int8_t nj_zz[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

static void nj_release_image_buffers(void) {
    int i;
    if (!nj_ctx) return;
    for (i = 0; i < 3; ++i) {
        if (nj.comp[i].pixels) {
            kfree(nj.comp[i].pixels);
            nj.comp[i].pixels = NULL;
        }
    }
    if (nj.rgb) {
        kfree(nj.rgb);
        nj.rgb = NULL;
    }
}

static bool nj_init(void) {
    if (!nj_ctx) {
        nj_ctx = (nj_context_t *)kmalloc((uint32_t)sizeof(*nj_ctx));
        if (!nj_ctx) return false;
    }
    kmemset(nj_ctx, 0, (uint32_t)sizeof(*nj_ctx));
    return true;
}

static void nj_reset(void) {
    if (!nj_ctx) return;
    nj_release_image_buffers();
    kmemset(nj_ctx, 0, (uint32_t)sizeof(*nj_ctx));
}

static void nj_done(void) {
    if (nj_ctx) {
        nj_release_image_buffers();
        kfree(nj_ctx);
        nj_ctx = NULL;
    }
    nj_release();
}

NJ_FORCE_INLINE uint8_t nj_clip(int x) {
    return x < 0 ? 0U : (x > 255 ? 255U : (uint8_t)x);
}

#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

NJ_INLINE void nj_row_idct(int *blk) {
    int x0,x1,x2,x3,x4,x5,x6,x7,x8;
    if (!((x1=blk[4]*2048)|(x2=blk[6])|(x3=blk[2])|(x4=blk[1])|
          (x5=blk[7])|(x6=blk[5])|(x7=blk[3]))) {
        blk[0]=blk[1]=blk[2]=blk[3]=blk[4]=blk[5]=blk[6]=blk[7]=blk[0]*8;
        return;
    }
    x0=blk[0]*2048+128;
    x8=W7*(x4+x5); x4=x8+(W1-W7)*x4; x5=x8-(W1+W7)*x5;
    x8=W3*(x6+x7); x6=x8-(W3-W5)*x6; x7=x8-(W3+W5)*x7;
    x8=x0+x1; x0-=x1;
    x1=W6*(x3+x2); x2=x1-(W2+W6)*x2; x3=x1+(W2-W6)*x3;
    x1=x4+x6; x4-=x6; x6=x5+x7; x5-=x7;
    x7=x8+x3; x8-=x3; x3=x0+x2; x0-=x2;
    x2=(181*(x4+x5)+128)>>8; x4=(181*(x4-x5)+128)>>8;
    blk[0]=(x7+x1)>>8; blk[1]=(x3+x2)>>8;
    blk[2]=(x0+x4)>>8; blk[3]=(x8+x6)>>8;
    blk[4]=(x8-x6)>>8; blk[5]=(x0-x4)>>8;
    blk[6]=(x3-x2)>>8; blk[7]=(x7-x1)>>8;
}

NJ_INLINE void nj_col_idct(const int *blk, uint8_t *out, int stride) {
    int x0,x1,x2,x3,x4,x5,x6,x7,x8;
    if (!((x1=blk[8*4]*256)|(x2=blk[8*6])|(x3=blk[8*2])|
          (x4=blk[8*1])|(x5=blk[8*7])|(x6=blk[8*5])|(x7=blk[8*3]))) {
        x1=nj_clip(((blk[0]+32)>>6)+128);
        for (x0=8; x0; --x0) { *out=(uint8_t)x1; out+=stride; }
        return;
    }
    x0=blk[0]*256+8192;
    x8=W7*(x4+x5)+4; x4=(x8+(W1-W7)*x4)>>3; x5=(x8-(W1+W7)*x5)>>3;
    x8=W3*(x6+x7)+4; x6=(x8-(W3-W5)*x6)>>3; x7=(x8-(W3+W5)*x7)>>3;
    x8=x0+x1; x0-=x1;
    x1=W6*(x3+x2)+4; x2=(x1-(W2+W6)*x2)>>3; x3=(x1+(W2-W6)*x3)>>3;
    x1=x4+x6; x4-=x6; x6=x5+x7; x5-=x7;
    x7=x8+x3; x8-=x3; x3=x0+x2; x0-=x2;
    x2=(181*(x4+x5)+128)>>8; x4=(181*(x4-x5)+128)>>8;
    *out=nj_clip(((x7+x1)>>14)+128); out+=stride;
    *out=nj_clip(((x3+x2)>>14)+128); out+=stride;
    *out=nj_clip(((x0+x4)>>14)+128); out+=stride;
    *out=nj_clip(((x8+x6)>>14)+128); out+=stride;
    *out=nj_clip(((x8-x6)>>14)+128); out+=stride;
    *out=nj_clip(((x0-x4)>>14)+128); out+=stride;
    *out=nj_clip(((x3-x2)>>14)+128); out+=stride;
    *out=nj_clip(((x7-x1)>>14)+128);
}

#define NJ_THROW(e) do { nj.error=(e); return; } while (0)
#define NJ_CHECK() do { if (nj.error) return; } while (0)

static int nj_show_bits(int bits) {
    uint8_t b;
    if (!bits) return 0;
    while (nj.bufbits < bits) {
        if (nj.size <= 0) {
            nj.buf=(nj.buf<<8)|0xff; nj.bufbits+=8; continue;
        }
        b=*nj.pos++; --nj.size;
        nj.bufbits+=8; nj.buf=(nj.buf<<8)|b;
        if (b==0xff) {
            if (nj.size) {
                uint8_t marker=*nj.pos++; --nj.size;
                if (marker==0x00 || marker==0xff) { }
                else if (marker==0xd9) nj.size=0;
                else if ((marker&0xf8)!=0xd0) nj.error=NJ_SYNTAX_ERROR;
                else { nj.buf=(nj.buf<<8)|marker; nj.bufbits+=8; }
            } else nj.error=NJ_SYNTAX_ERROR;
        }
    }
    return (nj.buf>>(nj.bufbits-bits))&((1<<bits)-1);
}

NJ_INLINE void nj_skip_bits(int bits) {
    if (nj.bufbits<bits) (void)nj_show_bits(bits);
    nj.bufbits-=bits;
}
NJ_INLINE int nj_get_bits(int bits) { int r=nj_show_bits(bits); nj_skip_bits(bits); return r; }
NJ_INLINE void nj_byte_align(void) { nj.bufbits&=0xf8; }

static void nj_skip(int count) {
    nj.pos+=count; nj.size-=count; nj.length-=count;
    if (nj.size<0) nj.error=NJ_SYNTAX_ERROR;
}
NJ_INLINE uint16_t nj_decode16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }

static void nj_decode_length(void) {
    if (nj.size<2) NJ_THROW(NJ_SYNTAX_ERROR);
    nj.length=nj_decode16(nj.pos);
    if (nj.length>nj.size) NJ_THROW(NJ_SYNTAX_ERROR);
    nj_skip(2);
}
NJ_INLINE void nj_skip_marker(void) { nj_decode_length(); if (!nj.error) nj_skip(nj.length); }

static void nj_decode_sof(void) {
    int i,ssxmax=0,ssymax=0;
    nj_component_t *c;
    nj_decode_length(); NJ_CHECK();
    if (nj.length<9 || nj.pos[0]!=8) NJ_THROW(nj.pos[0]!=8?NJ_UNSUPPORTED:NJ_SYNTAX_ERROR);
    nj.height=nj_decode16(nj.pos+1); nj.width=nj_decode16(nj.pos+3);
    if (!nj.width || !nj.height || nj.width>4096 || nj.height>4096 ||
        (uint32_t)nj.width*(uint32_t)nj.height>1000000U) NJ_THROW(NJ_UNSUPPORTED);
    nj.ncomp=nj.pos[5]; nj_skip(6);
    if (nj.ncomp!=1 && nj.ncomp!=3) NJ_THROW(NJ_UNSUPPORTED);
    if (nj.length<nj.ncomp*3) NJ_THROW(NJ_SYNTAX_ERROR);
    for (i=0,c=nj.comp;i<nj.ncomp;++i,++c) {
        c->cid=nj.pos[0]; c->ssx=nj.pos[1]>>4; c->ssy=nj.pos[1]&15;
        if (!c->ssx || (c->ssx&(c->ssx-1)) || !c->ssy || (c->ssy&(c->ssy-1)))
            NJ_THROW(NJ_UNSUPPORTED);
        c->qtsel=nj.pos[2]; if (c->qtsel&0xfc) NJ_THROW(NJ_SYNTAX_ERROR);
        nj_skip(3); nj.qtused|=1<<c->qtsel;
        if (c->ssx>ssxmax) ssxmax=c->ssx;
        if (c->ssy>ssymax) ssymax=c->ssy;
    }
    if (nj.ncomp==1) nj.comp[0].ssx=nj.comp[0].ssy=ssxmax=ssymax=1;
    nj.mbsizex=ssxmax<<3; nj.mbsizey=ssymax<<3;
    nj.mbwidth=(nj.width+nj.mbsizex-1)/nj.mbsizex;
    nj.mbheight=(nj.height+nj.mbsizey-1)/nj.mbsizey;
    for (i=0,c=nj.comp;i<nj.ncomp;++i,++c) {
        uint32_t bytes;
        c->width=(nj.width*c->ssx+ssxmax-1)/ssxmax;
        c->height=(nj.height*c->ssy+ssymax-1)/ssymax;
        c->stride=nj.mbwidth*c->ssx<<3;
        if (((c->width<3)&&(c->ssx!=ssxmax)) || ((c->height<3)&&(c->ssy!=ssymax)))
            NJ_THROW(NJ_UNSUPPORTED);
        bytes=(uint32_t)c->stride*(uint32_t)nj.mbheight*(uint32_t)c->ssy*8U;
        c->pixels=(uint8_t *)kmalloc(bytes);
        if (!c->pixels) NJ_THROW(NJ_OUT_OF_MEM);
    }
    if (nj.ncomp==3) {
        nj.rgb=(uint8_t *)kmalloc((uint32_t)nj.width*(uint32_t)nj.height*3U);
        if (!nj.rgb) NJ_THROW(NJ_OUT_OF_MEM);
    }
    nj_skip(nj.length);
}

static void nj_decode_dht(void) {
    int codelen,currcnt,remain,spread,i,j;
    nj_vlc_code_t *vlc;
    uint8_t counts[16];
    nj_decode_length(); NJ_CHECK();
    while (nj.length>=17) {
        i=nj.pos[0];
        if ((i&0xec)||(i&2)) NJ_THROW((i&2)?NJ_UNSUPPORTED:NJ_SYNTAX_ERROR);
        i=(i|(i>>3))&3;
        for (codelen=1;codelen<=16;++codelen) counts[codelen-1]=nj.pos[codelen];
        nj_skip(17); vlc=&nj.vlctab[i][0]; remain=spread=65536;
        for (codelen=1;codelen<=16;++codelen) {
            spread>>=1; currcnt=counts[codelen-1]; if (!currcnt) continue;
            if (nj.length<currcnt) NJ_THROW(NJ_SYNTAX_ERROR);
            remain-=currcnt<<(16-codelen); if (remain<0) NJ_THROW(NJ_SYNTAX_ERROR);
            for (i=0;i<currcnt;++i) {
                uint8_t code=nj.pos[i];
                for (j=spread;j;--j) { vlc->bits=(uint8_t)codelen; vlc->code=code; ++vlc; }
            }
            nj_skip(currcnt);
        }
        while (remain--) { vlc->bits=0; ++vlc; }
    }
    if (nj.length) NJ_THROW(NJ_SYNTAX_ERROR);
}

static void nj_decode_dqt(void) {
    int i; uint8_t *t;
    nj_decode_length(); NJ_CHECK();
    while (nj.length>=65) {
        i=nj.pos[0]; if (i&0xfc) NJ_THROW(NJ_SYNTAX_ERROR);
        nj.qtavail|=1<<i; t=&nj.qtab[i][0];
        for (i=0;i<64;++i) t[i]=nj.pos[i+1];
        nj_skip(65);
    }
    if (nj.length) NJ_THROW(NJ_SYNTAX_ERROR);
}

static void nj_decode_dri(void) {
    nj_decode_length(); NJ_CHECK();
    if (nj.length<2) NJ_THROW(NJ_SYNTAX_ERROR);
    nj.rstinterval=nj_decode16(nj.pos); nj_skip(nj.length);
}

static int nj_get_vlc(nj_vlc_code_t *vlc, uint8_t *code) {
    int value=nj_show_bits(16),bits=vlc[value].bits;
    if (!bits) { nj.error=NJ_SYNTAX_ERROR; return 0; }
    nj_skip_bits(bits); value=vlc[value].code; if (code) *code=(uint8_t)value;
    bits=value&15; if (!bits) return 0;
    value=nj_get_bits(bits);
    if (value<(1<<(bits-1))) value-=((1<<bits)-1);
    return value;
}

static void nj_decode_block(nj_component_t *c, uint8_t *out) {
    uint8_t code=0; int value,coef=0;
    kmemset(nj.block,0,sizeof(nj.block));
    c->dcpred+=nj_get_vlc(&nj.vlctab[c->dctabsel][0],NULL);
    nj.block[0]=c->dcpred*nj.qtab[c->qtsel][0];
    do {
        value=nj_get_vlc(&nj.vlctab[c->actabsel][0],&code);
        if (!code) break;
        if (!(code&15) && code!=0xf0) NJ_THROW(NJ_SYNTAX_ERROR);
        coef+=(code>>4)+1; if (coef>63) NJ_THROW(NJ_SYNTAX_ERROR);
        nj.block[(int)nj_zz[coef]]=value*nj.qtab[c->qtsel][coef];
    } while (coef<63);
    for (coef=0;coef<64;coef+=8) nj_row_idct(&nj.block[coef]);
    for (coef=0;coef<8;++coef) nj_col_idct(&nj.block[coef],&out[coef],c->stride);
}

static void nj_decode_scan(void) {
    int i,mbx,mby,sbx,sby,rstcount=nj.rstinterval,nextrst=0;
    nj_component_t *c;
    nj_decode_length(); NJ_CHECK();
    if (nj.length<(4+2*nj.ncomp) || nj.pos[0]!=nj.ncomp) NJ_THROW(NJ_UNSUPPORTED);
    nj_skip(1);
    for (i=0,c=nj.comp;i<nj.ncomp;++i,++c) {
        if (nj.pos[0]!=c->cid || (nj.pos[1]&0xee)) NJ_THROW(NJ_SYNTAX_ERROR);
        c->dctabsel=nj.pos[1]>>4; c->actabsel=(nj.pos[1]&1)|2; nj_skip(2);
    }
    if (nj.pos[0] || nj.pos[1]!=63 || nj.pos[2]) NJ_THROW(NJ_UNSUPPORTED);
    nj_skip(nj.length);
    for (mbx=mby=0;;) {
        for (i=0,c=nj.comp;i<nj.ncomp;++i,++c)
            for (sby=0;sby<c->ssy;++sby)
                for (sbx=0;sbx<c->ssx;++sbx) {
                    nj_decode_block(c,&c->pixels[((mby*c->ssy+sby)*c->stride+
                                                  mbx*c->ssx+sbx)<<3]);
                    NJ_CHECK();
                }
        if (++mbx>=nj.mbwidth) { mbx=0; if (++mby>=nj.mbheight) break; }
        if (nj.rstinterval && !(--rstcount)) {
            nj_byte_align(); i=nj_get_bits(16);
            if ((i&0xfff8)!=0xffd0 || (i&7)!=nextrst) NJ_THROW(NJ_SYNTAX_ERROR);
            nextrst=(nextrst+1)&7; rstcount=nj.rstinterval;
            for (i=0;i<3;++i) nj.comp[i].dcpred=0;
        }
    }
    nj.error=NJ_FINISHED;
}

#if NJ_CHROMA_FILTER
#define CF4A (-9)
#define CF4B 111
#define CF4C 29
#define CF4D (-3)
#define CF3A 28
#define CF3B 109
#define CF3C (-9)
#define CF3X 104
#define CF3Y 27
#define CF3Z (-3)
#define CF2A 139
#define CF2B (-11)
#define CF(x) nj_clip(((x)+64)>>7)

static void nj_upsample_h(nj_component_t *c) {
    int xmax=c->width-3,x,y; uint8_t *out,*lin,*lout;
    out=(uint8_t *)kmalloc((uint32_t)c->width*(uint32_t)c->height*2U);
    if (!out) NJ_THROW(NJ_OUT_OF_MEM);
    lin=c->pixels; lout=out;
    for (y=c->height;y;--y) {
        lout[0]=CF(CF2A*lin[0]+CF2B*lin[1]);
        lout[1]=CF(CF3X*lin[0]+CF3Y*lin[1]+CF3Z*lin[2]);
        lout[2]=CF(CF3A*lin[0]+CF3B*lin[1]+CF3C*lin[2]);
        for (x=0;x<xmax;++x) {
            lout[(x<<1)+3]=CF(CF4A*lin[x]+CF4B*lin[x+1]+CF4C*lin[x+2]+CF4D*lin[x+3]);
            lout[(x<<1)+4]=CF(CF4D*lin[x]+CF4C*lin[x+1]+CF4B*lin[x+2]+CF4A*lin[x+3]);
        }
        lin+=c->stride; lout+=c->width<<1;
        lout[-3]=CF(CF3A*lin[-1]+CF3B*lin[-2]+CF3C*lin[-3]);
        lout[-2]=CF(CF3X*lin[-1]+CF3Y*lin[-2]+CF3Z*lin[-3]);
        lout[-1]=CF(CF2A*lin[-1]+CF2B*lin[-2]);
    }
    c->width<<=1; c->stride=c->width; kfree(c->pixels); c->pixels=out;
}

static void nj_upsample_v(nj_component_t *c) {
    int w=c->width,s1=c->stride,s2=s1+s1,x,y; uint8_t *out,*cin,*cout;
    out=(uint8_t *)kmalloc((uint32_t)c->width*(uint32_t)c->height*2U);
    if (!out) NJ_THROW(NJ_OUT_OF_MEM);
    for (x=0;x<w;++x) {
        cin=&c->pixels[x]; cout=&out[x];
        *cout=CF(CF2A*cin[0]+CF2B*cin[s1]); cout+=w;
        *cout=CF(CF3X*cin[0]+CF3Y*cin[s1]+CF3Z*cin[s2]); cout+=w;
        *cout=CF(CF3A*cin[0]+CF3B*cin[s1]+CF3C*cin[s2]); cout+=w; cin+=s1;
        for (y=c->height-3;y;--y) {
            *cout=CF(CF4A*cin[-s1]+CF4B*cin[0]+CF4C*cin[s1]+CF4D*cin[s2]); cout+=w;
            *cout=CF(CF4D*cin[-s1]+CF4C*cin[0]+CF4B*cin[s1]+CF4A*cin[s2]); cout+=w; cin+=s1;
        }
        cin+=s1;
        *cout=CF(CF3A*cin[0]+CF3B*cin[-s1]+CF3C*cin[-s2]); cout+=w;
        *cout=CF(CF3X*cin[0]+CF3Y*cin[-s1]+CF3Z*cin[-s2]); cout+=w;
        *cout=CF(CF2A*cin[0]+CF2B*cin[-s1]);
    }
    c->height<<=1; c->stride=c->width; kfree(c->pixels); c->pixels=out;
}
#endif

static void nj_convert(void) {
    int i; nj_component_t *c;
    for (i=0,c=nj.comp;i<nj.ncomp;++i,++c) {
        while (c->width<nj.width || c->height<nj.height) {
            if (c->width<nj.width) nj_upsample_h(c);
            NJ_CHECK();
            if (c->height<nj.height) nj_upsample_v(c);
            NJ_CHECK();
        }
        if (c->width<nj.width || c->height<nj.height) NJ_THROW(NJ_INTERNAL_ERR);
    }
    if (nj.ncomp==3) {
        int x,y; uint8_t *dst=nj.rgb;
        const uint8_t *py=nj.comp[0].pixels,*pcb=nj.comp[1].pixels,*pcr=nj.comp[2].pixels;
        for (y=nj.height;y;--y) {
            for (x=0;x<nj.width;++x) {
                int yy=py[x]<<8,cb=pcb[x]-128,cr=pcr[x]-128;
                *dst++=nj_clip((yy+359*cr+128)>>8);
                *dst++=nj_clip((yy-88*cb-183*cr+128)>>8);
                *dst++=nj_clip((yy+454*cb+128)>>8);
            }
            py+=nj.comp[0].stride; pcb+=nj.comp[1].stride; pcr+=nj.comp[2].stride;
        }
    } else if (nj.comp[0].width!=nj.comp[0].stride) {
        uint8_t *in=&nj.comp[0].pixels[nj.comp[0].stride];
        uint8_t *out=&nj.comp[0].pixels[nj.comp[0].width]; int y;
        for (y=nj.comp[0].height-1;y;--y) {
            kmemcpy(out,in,(uint32_t)nj.comp[0].width);
            in+=nj.comp[0].stride; out+=nj.comp[0].width;
        }
        nj.comp[0].stride=nj.comp[0].width;
    }
}

static nj_result_t nj_decode(const void *jpeg, int size) {
    nj_reset(); nj.pos=(const uint8_t *)jpeg; nj.size=size&0x7fffffff;
    if (nj.size<2 || nj.pos[0]!=0xff || nj.pos[1]!=0xd8) return NJ_NO_JPEG;
    nj_skip(2);
    while (!nj.error) {
        if (nj.size<2 || nj.pos[0]!=0xff) return NJ_SYNTAX_ERROR;
        nj_skip(2);
        switch (nj.pos[-1]) {
            case 0xc0: nj_decode_sof(); break;
            case 0xc4: nj_decode_dht(); break;
            case 0xdb: nj_decode_dqt(); break;
            case 0xdd: nj_decode_dri(); break;
            case 0xda: nj_decode_scan(); break;
            case 0xfe: nj_skip_marker(); break;
            default:
                if ((nj.pos[-1]&0xf0)==0xe0) nj_skip_marker();
                else return NJ_UNSUPPORTED;
        }
    }
    if (nj.error!=NJ_FINISHED) return nj.error;
    nj.error=NJ_OK; nj_convert(); return nj.error;
}

bool gui_jpeg_decode(gui_image_t *image, const uint8_t *data, uint32_t length) {
    uint32_t count,i; uint32_t *pixels; const uint8_t *source; bool color;
    if (!image || !data || length<4U || length>0x7fffffffU) return false;
    image->pixels=NULL; image->width=0; image->height=0;
    if (!nj_acquire()) return false;
    if (!nj_init()) { nj_release(); return false; }
    if (nj_decode(data,(int)length)!=NJ_OK || nj.width<=0 || nj.height<=0 ||
        nj.width>65535 || nj.height>65535) { nj_done(); return false; }
    count=(uint32_t)nj.width*(uint32_t)nj.height;
    if (count>1000000U) { nj_done(); return false; }
    pixels=(uint32_t *)kmalloc(count*4U);
    if (!pixels) { nj_done(); return false; }
    source=nj.ncomp==1?nj.comp[0].pixels:nj.rgb; color=nj.ncomp!=1;
    for (i=0;i<count;++i) {
        uint8_t r,g,b;
        if (color) { r=source[i*3U]; g=source[i*3U+1U]; b=source[i*3U+2U]; }
        else r=g=b=source[i];
        pixels[i]=0xff000000U|((uint32_t)r<<16)|((uint32_t)g<<8)|b;
    }
    image->pixels=pixels; image->width=(uint16_t)nj.width; image->height=(uint16_t)nj.height;
    nj_done(); return true;
}
