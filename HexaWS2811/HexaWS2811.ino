

/*  OctoWS2811 BasicTest.ino - Basic RGB LED Test
 http://www.pjrc.com/teensy/td_libs_OctoWS2811.html
 Copyright (c) 2013 Paul Stoffregen, PJRC.COM, LLC

 Hacked around for 16 channel (HexaWS2811) and other stuff by DrTune

 VERY HACKED ABOUT INDEED BY DrTune
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 
 Required Connections
 --------------------
 pin 2:  LED Strip #1    OctoWS2811 drives 8 LED Strips.
 pin 14: LED strip #2    All 8 are the same length.
 pin 7:  LED strip #3
 pin 8:  LED strip #4    A 100 ohm resistor should used
 pin 6:  LED strip #5    between each Teensy pin and the
 pin 20: LED strip #6    wire to the LED strip, to minimize
 pin 21: LED strip #7    high frequency ringining & noise.
 pin 5:  LED strip #8
 pin 15 & 16 - Connect together, but do not use
 pin 4 - Do not useO
 pin 3 - Do not use as PWM.  Normal use is ok.

 This code also optionally runs in "TWO_PORT" mode (16 channel) see #define in hexa2.h

 ALTERNATIVELY For 16-port mode; basically we use PortC and D not just D as above...
 ==in led order
	2	// strip #1 
	14	// strip #2
	7	// strip #3
	8	// strip #4
	6	// strip #5
	20	// strip #6
	21	// strip #7
	5	// strip #8

   	15	// PC0 strip #9
   	22	// PC1 strip #10
	23	// PC2 strip #11
	9	// PC3 strip #12
	10	// PC4 strip #13
	13	// PC5 strip #14
	11	// PC6 strip #15
	12	// PC7 strip #16

==in pin order
	2	// PD0 strip #1 
	5	// PD7 strip #8
	6	// PD4 strip #5
	7	// PD2 strip #3
	8	// PD3 strip #4
	9	// PC3 strip #12
	10	// PC4 strip #13
	11	// PC6 strip #15
	12	// PC7 strip #16

	13	// PC5 strip #14
	14	// PD1 strip #2
   	15	// PC0 strip #9
	20	// PD5 strip #6
	21	// PD6 strip #7
   	22	// PC1 strip #10
	23	// PC2 strip #11

  Optionally you can set "C8SYNC" define in hexa2.h to use B0(16)<->C8(28) for PWM instead of OctoWS2811's pins 15<->16, freeing up C0 for LED use
 
 */

#include "hexa2.h"


#define MOVE_SPEED 4  //demo pixels

//#define NO_LEDS  //test

//upper SRAM memory to DMA from; minimize contention
DMAMEM int dmaMemory[(LEDS_PER_STRIP*NUM_STRIPS*3)/4];  //3 bytes per led

//everything else in lower (default) sram so cpu can get it ready without contention
int conversionMemory[(LEDS_PER_STRIP*NUM_STRIPS*3)/4];  //3 bytes per led

uint32_t drawBuffer[LEDS_PER_STRIP*NUM_STRIPS]; //4 bytes per led


OctoWS2811 leds(LEDS_PER_STRIP, conversionMemory, dmaMemory);



#define RED    0xFF0000
#define GREEN  0x00FF00
#define BLUE   0xffFFff
#define YELLOW 0xFFFF00
//#define PINK   0xFF1088
#define PINK   0xFFFF00
#define ORANGE 0xE05800
#define WHITE  0xFFFFFF


int scaleColor(int color,uint8_t scale)
{
  //#define scaleColorSingle(n,shift,scale) (((( ((n>>shift)&0xff) *scale)>>8)&0xff) <<scale)
#define scaleColorSingle(n,shift,scale) ((((((n>>shift)&0xff)*scale)>>8)&0xff)<<shift)
  return scaleColorSingle(color,0,scale) | scaleColorSingle(color,8,scale) | scaleColorSingle(color,16,scale);
}

#define NUM_COLORS 8
int COLORS[NUM_COLORS]={ 
   WHITE,
   RED,
   GREEN,
   BLUE,
   YELLOW,
   PINK,
   ORANGE,
   0x2345aa,
};

#define FADE_RATE 4 //6 //10/2


#define PSHIFT 8
#define PONE (1<<PSHIFT)
#define PMASK (PONE-1)
#define PCOVERAGE(n) (PMASK-((n)&PMASK))

typedef struct {
  int pos,dir,color,strip,intensity;
}
PIX;
#define PIX_PER_STRIP 4
#define NUM_PIX (PIX_PER_STRIP * NUM_STRIPS)
PIX pixels[NUM_PIX];



int ftime=1;

void initPix()
{
  PIX *pix=pixels;
  int color=0;
  int dir=0;
  int pos=0;
  int intens=0;
  for(int strip=0;strip<NUM_STRIPS;strip++){
    for(int t=0;t<PIX_PER_STRIP;t++){  
      pix->pos=(pos++)<<(PSHIFT+4);
      pix->color=color++;
      pix->intensity=0xff; //intens+0x80;
      intens=(intens+28)&0x7f;
      color=color%NUM_COLORS;
      pix->strip=strip;
      pix->dir=dir++; //((1<<PSHIFT)-(t));
      pix++;
    }
  }
}





void setup() {
#ifndef NO_LEDS
  leds.begin();
  leds.show();
#endif
  initPix();
}


//draw anti-aliased moving pixel into buffer - demo code
void drawPixLine(int startPos,int endPos,int strip,int color)
{
  int stripOff=(strip*LEDS_PER_STRIP);
  if (endPos<startPos){
    int t=startPos;
    startPos=endPos;
    endPos=startPos;
  }
  int len=endPos-startPos;
  if (len>>PSHIFT) color=scaleColor(color,0xff/(len>>PSHIFT));

  int intPos=startPos>>PSHIFT;
  if (intPos>=LEDS_PER_STRIP) return;
  if (intPos<0) intPos=0;

  int intEndPos=endPos>>PSHIFT;
  if (intEndPos >= LEDS_PER_STRIP) intEndPos=LEDS_PER_STRIP-1;
  if (intEndPos<0) return;

  int coverage=PCOVERAGE(startPos);

  drawBuffer[ intPos+stripOff ] |= scaleColor(color, coverage );
  while(1){
    intPos++;
    if (intPos>=intEndPos){
      drawBuffer[ intPos+stripOff ] |= scaleColor(color, PMASK-coverage); //PCOVERAGE(endPos) );
      break;
    }
    drawBuffer[ intPos+stripOff ] |= color;
  }

}

//zip some pretty pixels around - just demo code
void processPix()
{
  int newPos;

  for(int t=0;t<NUM_PIX;t++){  
    PIX *pix=&pixels[t];
    int oldPos= pix->pos;
    int color=scaleColor( COLORS[pix->color], pix->intensity);
    pix->pos+=pix->dir;
    newPos=pix->pos;
    drawPixLine(oldPos,newPos,pix->strip,color);

    if ((ftime%MOVE_SPEED)==0){
      int ldir=pix->dir;
      pix->dir+=1;
      if (ldir<0 && pix->dir>=0) pix->strip=(pix->strip+1)%NUM_STRIPS; //hop strip
    }
    if ((pix->pos>>PSHIFT)>=LEDS_PER_STRIP){
      pix->dir=-pix->dir;
      //pix->pos=LEDS_PER_STRIP<<PSHIFT;
      //pix->pos+=pix->dir;
      //pix->color=(pix->color++) % NUM_COLORS;
    }
  }
}

// Fade pixel buffer by one step so everything we draw kinda fades away - demo code
void fade()
{
  uint32_t *pixelPtr=drawBuffer;
  int ft=ftime;
  if (ft&1) return;
  //randomSeed(12345);
  
  for (int s=0; s < NUM_STRIPS; s++) {
    for (int i=0; i < LEDS_PER_STRIP; i++) {
      int pix;
#if 1
  //fade mode
      pix=*pixelPtr;
      if (pix){
        //could be optimized with asm to use UQSUB8 instruction on ARM M4
        int r=pix>>16;
        int g=(pix>>8)&0xff;
        int b=(pix&0xff);
#define rgbfade(r) r-=FADE_RATE; if (r<0)r=0;        
        rgbfade(r);
        rgbfade(g);
        rgbfade(b);
        pix=((r&0xff)<<16) |  ((g&0xff)<<8) | (b&0xff);
      }
#else      

  //test mode - instead of fading, write patterns to test strips
         //  if(((ft>>9)&15)==(s&15)) 
          if (1)
          {
        if (ft&0x100)
          pix=0xff-(ft&0xff); //0x00;
        else
          pix=ft; //0x00;
        pix&=0xff;

        pix=(ft+i)&0xff;  //trails
        
//        if (pix>0x80)pix=0x80;


      //random
       //pix=random(256);
       //if ((pix+(ft&31)) & 0x20) pix=0;
       

        pix|=(pix<<8)|(pix<<16); 
      }
      else pix=0;
#endif      
      *pixelPtr++=pix;
    } 
  }
}



void loop() {
  ftime++;

  //You need these two lines
  leds.RGB888ToDrawBuffer(drawBuffer,ftime);
#ifndef NO_LEDS
  leds.show();
#endif

  processPix();  //draw some zippy pixels for demo
  fade();        //fade out trails for demo


}





