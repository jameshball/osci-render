/*
   Beamcaster
   Compiling:
     cc -Wall -O3 -o beamcaster beamcaster.c -DBCPOW2=3 -DBCRAD=4 -DBCSKEW=1
   Usage:
*/
const char *usagestr =
  "beamcaster source.pbm"
;
/*

Copyright 2023-2024 Theron Tarigo

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.

*/

#if !( \
    defined( BCPOW2 )\
  &&defined( BCSKEW )\
  &&defined( BCRAD ) )
  #error One of required parameters not defined
#endif
static const float
  bcskew=(BCSKEW),
  bcrad=(BCRAD) ;
static const int
  bcpow2=(BCPOW2) ;

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <math.h>

#include <time.h>
#include <unistd.h>


//////// parameter constants

static int imgsizex=512;
static int imgsizey=512;
static int npoints=2000;
static float beamgain=.9; // size of the displayed image relative to gutters


//////// beam-jumping decision functions

// saved state used only by updatestate and probfn
static float jvx=1.,jvy=0.;

// a chance to update state according to how far the beam last moved
static void updatestate (float dx, float dy) {
  if(bcskew==1.)return;
  float w=.01; // tunable mixing parameter
  if(dx*jvx+dy*jvy<0)w=-w;
  jvx+=dx*w;
  jvy+=dy*w;
  float n=1./sqrtf(jvx*jvx+jvy*jvy);
  // renormalize the vector
  jvx*=n;
  jvy*=n;
}

// core rule for beam jumping: returns relative probability for a beam to jump
// by pixel displacement dx,dy
static float probfn (float dx, float dy) {
  float r2;
  if(bcskew!=1.) {
    float da=jvx*dx+jvy*dy;
    float db=jvy*dx-jvx*dy;
    r2=da*da/bcskew+db*db*bcskew; // biased point distance
  } else {
    r2=dx*dx+dy*dy;
  }
  float r2t=(bcrad*bcrad); // threshold radius squared
// reference probability level: equal chance to jump any target within radius
  if(r2<r2t) return 1.0;
  float a=r2t/r2; // inverse squared ratio
  float p=1.;
  for(int i=0;i<bcpow2;i++) p*=a; // p=pow(a,bcpow2)
  return p;
}

//////// data manipulation, point list structure, and main program

static void wri16 (FILE *f, int16_t d) {
  fwrite(&d,1,2,f);
}

struct point {
  uint16_t x,y;
};

// a pointlist is an *unordered* list of x,y points
struct pointlist {
  struct point *pts;
  int len;
};

// remove point at index i from pointlist
static void pointlist_remove (struct pointlist *ppl, int i) {
  // since pointlist is unordered, last point can be moved to replace
  // the deleted point
  // overwrite removed point with last point:
  ppl->pts[i]=ppl->pts[ppl->len-1];
  // forget last point
  ppl->len--;
};

static int readimage (FILE *f, int w, int h, char *imgbuf);
static uint8_t bitmap (const char *buf, int w, int x, int y);

// rescale x,y coordinate in 0,0 to w,h space to -gain,+gain range
// write that point as a stereo s16le pcm sample to stdout
static void beamto (int w, int h, float gain, int x, int y) {
  x=((float)x/(float)(w-1)*2.-1.)*gain*0x8000+0x8000;
  y=((float)y/(float)(h-1)*2.-1.)*gain*0x8000+0x8000;
  x=x<0?0:x>0xFFFF?0xFFFF:x; // clamp
  y=y<0?0:y>0xFFFF?0xFFFF:y; // clamp
  wri16(stdout,0x8000^x);
  wri16(stdout,0x8000^y);
}




int main (int argc, char **argv) {

  if(argc<1+1) {
    fprintf(stderr, "Usage: %s\n", usagestr);
    return 1;
  }
  const char *imgfname =argv[1];
  FILE *imgf =fopen(imgfname,"r");
  if (!imgf) return errno;
  // allocate space for image, rounding up as needed
  char *imgbuf=malloc((imgsizex*imgsizey-1)/8+1);
  if (readimage(imgf,imgsizex,imgsizey,imgbuf)) {
    fclose(imgf);
    return 1;
  }
  fclose(imgf);

  // make sure to randomize between runs even if they start at same time
  srand(time(0)^getpid());

  struct pointlist pl;
  // allocate point list as large as if whole image is filled
  pl.pts=malloc(sizeof(*pl.pts)*imgsizex*imgsizey);

  // one probability will be calculated and stored for each point
  float *probs =malloc(sizeof(*probs)*npoints);

  // current beam position
  int beamx=imgsizex>>1;
  int beamy=imgsizex>>1;

  // build the point list from scanning the image
  pl.len=0;
  for(int x,y=0;y<imgsizey;y+=1) for(x=0;x<imgsizex;x+=1) {
    if(
        !bitmap(imgbuf,imgsizex,x,imgsizey-1-y)
      ){
      struct point p;
      p.x=x;
      p.y=y;
      pl.pts[pl.len]=p;
      pl.len++;
    }
  }

  // remove points at random until number of points is correct
  while(pl.len>npoints) {
    // random point index
    int i=(float)rand()*(float)pl.len/(float)RAND_MAX;
    // easier to trust integer comparison that float arithmetic
    // that we aren't overflowing the buffer
    if (i>=pl.len) continue;
    pointlist_remove(&pl,i);
  }

  // number of points if any that must be added in gutters to fill
  // remaining beamtime
  int nfill=npoints-pl.len;

  // outer loop: while there is still another point to reach
  while(pl.len) {
    double probsum=0.;
    // inner loop 1: calculate probabilities
    // this evaluates probfn for each possible displacement
    // and builds an (unnormalized) CDF *cumulative distribution function*
    for(int i=0;i<pl.len;i++) {
      struct point p=pl.pts[i];
      probsum+=probfn(beamx-p.x,beamy-p.y);
      probs[i]=probsum;
    }
    // random selection from 0 to probsum
    float z=(float)rand()*probsum/(float)RAND_MAX;
    int i=0;
    // inner loop 2: select the point based on probability
    // this is called "Inverse transform sampling"
    while(i<pl.len&&probs[i]<z) i++;
    struct point p=pl.pts[i];
    // tell "updatestate" helper function how far we moved
    updatestate(beamx-p.x,beamy-p.y);
    beamx=p.x;
    beamy=p.y;
    // output a point for the oscilloscope
    beamto(imgsizex,imgsizey,beamgain,beamx,beamy);
    // and remove this point now that it is visited
    pointlist_remove(&pl,i);
  }

  // fill gutters, half on left, half on right
  int hnfill=nfill/2;
  while(nfill) {
    int x=(nfill<hnfill);
    int y=(float)rand()/(float)RAND_MAX*0xFFFF;
    beamto(2,0x10000,1.,x,y);
    --nfill;
  }

  return 0;
}

// retrieve as 0 or 1 the pixel value at position x,y
// for an image of width w, in the buffer format created by "readimage"
static uint8_t bitmap (const char *buf, int w, int x, int y) {
  return (buf[y*(w>>3)+(x>>3)]>>(~x&7))&1;
}

// generic read-until-can't-read-anymore from stdio file f
// allocating a buffer as needed
// pbuf must point to a char pointer which receives the allocated buffer
// plen must point to size_t which receives the total size read
static void readfile (FILE *f, char **pbuf, size_t *plen) {
  char * buf =0;
  size_t buf_cap=0,len=0,readsize;
  do {
    buf_cap=(len+0x2000)&~0xFFF;
    buf=realloc(buf,buf_cap);
    readsize=fread(buf+len,1,0x1000,f);
    len+=readsize;
  } while(readsize);
  buf[len]=0;
  *pbuf=buf;
  *plen=len;
}

// reads a PBM which must be size w*h into bitmap imgbuf
// imgbuf must point to a buffer with space for w*h bits = (w*h)/8 bytes
// return value:
//   0 if the complete image is read
//   1 if the PBM is not the required dimensions
//   1 if a file input error occurs, or the PBM format is not as expected.
static int readimage (FILE *f, int w, int h, char *imgbuf) {
  char buf[32];
  char buf2[32];
  char *txt =0;
  if (3!=fread(buf,1,3,f)) goto fmterror;
  int bin;
  if (!memcmp(buf,"P1\n",3)) bin=0;
  else if (!memcmp(buf,"P4\n",3)) bin=1;
  else goto fmterror;
  if (1!=fread(buf,1,1,f)) goto fmterror;
  if (buf[0]=='#') do {
    if (1!=fread(buf,1,1,f)) goto fmterror;
  } while (buf[0]!='\n');
  else fseek(f,-1,SEEK_CUR);
  int l=snprintf(buf2,sizeof(buf2),"%d %d\n",w,h);
  if (l<1) return 2; // usage error
  if (l!=fread(buf,1,l,f)) goto fmterror;
  if (memcmp(buf,buf2,l)) goto fmterror;
  size_t sz=w*h/8;
  if (bin) {
    if (sz!=fread(imgbuf,1,sz,f)) goto fmterror;
  } else {
    size_t tsz;
    readfile(f,&txt,&tsz);
    char *r = txt;
    for (size_t i=0; i<w*h; i+=8) {
      uint8_t byte=0;
      for (int j=0;j<8;j++) {
        while (*r=='\n') ++r;
        uint8_t b=*r-'0'; ++r;
        if (b>1) goto fmterror;
        byte<<=1;
        byte|=b;
      }
      imgbuf[i>>3]=byte;
    }
  }
  return 0;
  fmterror:
    if (txt) free(txt);
    fprintf(stderr, "image pbm format error\n");
    return 1;
}

