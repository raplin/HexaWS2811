/*  OctoWS2811 - High Performance WS2811 LED Display Library
    http://www.pjrc.com/teensy/td_libs_OctoWS2811.html
    Copyright (c) 2013 Paul Stoffregen, PJRC.COM, LLC

    Hacked around for 16 channel (HexaWS2811) and other stuff by DrTune

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
    
Hacked by DrTune as follows;
* Supports 16 strips (uses low 8 bits of both PortC & D) using tweaked DMA setup.
* Same update rate (e.g. 110hz with 300 leds per strip = 4800 LEDs)
* Added gamma correction
* Added temporal dithering (4 bits looks pretty good @ 110hz)
* Added source pixel framebuffer of RGB888 int32's; this is faster for your application to process than bit-sliced, to be super slick you can use the Arm's saturated 8-bit add/sub etc.

So it's a bit more like Fadecandy now in terms of visual quality, but with twice the channels. Unlike Fadecandy it doesn't have all the USB 


*/

#include <string.h>
#include "hexa2.h"
#include "gamma.h"

#define DEBUGSYNC  //wiggle pin 0 so we can see stuff on a scope

uint32_t *OctoWS2811::frameBuffer[2];
uint32_t OctoWS2811::bufSize;

static const uint8_t ones = 0xFF;
static volatile uint8_t update_in_progress = 0;
static uint32_t update_completed_at = 0;

OctoWS2811::OctoWS2811(uint32_t numPerStrip, void *frameBuf1,void *frameBuf2)
{
	frameBuffer[0]=(uint32_t*)frameBuf1;
	frameBuffer[1]=(uint32_t*)frameBuf2;
	bufSize = 3*numPerStrip*NUM_STRIPS;  //3 bytes per led
}

// Waveform timing: these set the high time for a 0 and 1 bit, as a fraction of
// the total 800 kHz or 400 kHz clock cycle.  The scale is 0 to 255.  The Worldsemi
// datasheet seems T1H should be 600 ns of a 1250 ns cycle, or 48%.  That may
// erroneous information?  Other sources reason the chip actually samples the
// line close to the center of each bit time, so T1H should be 80% if TOH is 20%.
// The chips appear to work based on a simple one-shot delay triggered by the
// rising edge.  At least 1 chip tested retransmits 0 as a 330 ns pulse (26%) and
// a 1 as a 660 ns pulse (53%).  Perhaps it's actually sampling near 500 ns?
// There doesn't seem to be any advantage to making T1H less, as long as there
// is sufficient low time before the end of the cycle, so the next rising edge
// can be detected.  T0H has been lengthened slightly, because the pulse can
// narrow if the DMA controller has extra latency during bus arbitration.  If you
// have an insight about tuning these parameters AND you have actually tested on
// real LED strips, please contact paul@pjrc.com.  Please do not email based only
// on reading the datasheets and purely theoretical analysis.

//DrTune note - the timing can be slightly more shifted due to extra DMA contention when running in 16 channel mode. YMMV
#define WS2811_TIMING_T0H  60
#define WS2811_TIMING_T1H  176

// Discussion about timing and flicker & color shift issues:
// http://forum.pjrc.com/threads/23877-WS2812B-compatible-with-OctoWS2811-library?p=38190&viewfull=1#post38190


void OctoWS2811::begin(void)
{
	uint32_t frequency;


	// set up the buffers
	memset(frameBuffer[0], 0, bufSize);
	memset(frameBuffer[1], 0, bufSize);
	
	// configure the 8 output pins
	GPIOD_PCOR = 0xFF;
	pinMode(2, OUTPUT);	// strip #1 
	pinMode(14, OUTPUT);	// strip #2
	pinMode(7, OUTPUT);	// strip #3
	pinMode(8, OUTPUT);	// strip #4
	pinMode(6, OUTPUT);	// strip #5
	pinMode(20, OUTPUT);	// strip #6
	pinMode(21, OUTPUT);	// strip #7
	pinMode(5, OUTPUT);	// strip #8

#ifdef TWOPORT
#ifdef C8SYNC //don't use pin 15-16 loopback, use something else. You still pretty much need to use pin16 as it has FTM1_CH0 muxed on it. Instead of B0 you could also use A12 (see datasheet)
	GPIOC_PCOR = 0xFF;
    	pinMode(28, OUTPUT);	// C8(pin28) -> B0 for sync instead of C0-B0 to free up C0 for an extra LED strip. 
#else
        //reserve C0 for pwm (this is default setup of the Octo2811 buffer board, but you can't use C0 for strip output so you only get 15 usable channels)
	GPIOC_PCOR = 0xFE;
#endif    
      	pinMode(22, OUTPUT);	// PC1 strip #9
	pinMode(23, OUTPUT);	// PC2 strip #10
	pinMode(9, OUTPUT);	// PC3 strip #11
	pinMode(10, OUTPUT);	// PC4 strip #12
	pinMode(13, OUTPUT);	// PC5 strip #13
	pinMode(11, OUTPUT);	// PC6 strip #14
	pinMode(12, OUTPUT);	// PC7 strip #15
#endif

#ifdef DEBUGSYNC
	pinMode(0, OUTPUT);	// B16 sync for scope testing
#endif

	// create the two waveforms for WS2811 low and high bits
	frequency = 800000;
	analogWriteResolution(8);
	analogWriteFrequency(3, frequency);
	analogWriteFrequency(4, frequency);
	analogWrite(3, WS2811_TIMING_T0H);
	analogWrite(4, WS2811_TIMING_T1H);

#ifdef C8SYNCxxxxx 
        // Optionally use A12 (pin 3) instead of B0 -  triggers DMA(port B) on rising edge (configure for pin 3's waveform)
#else
	// pin 16 (b0) is FTM1_CH0, triggers DMA(port B) on rising edge (configure mux to output pin 3's waveform)
	CORE_PIN16_CONFIG = PORT_PCR_IRQC(1)|PORT_PCR_MUX(3); 
        //pin35 (B0) , mux to FTM1_CH0  IRQC0001 = DMA on rising edge
	pinMode(3, INPUT_PULLUP); // pin 3 (A12, configured by the AnalogWrite(3..) above is no longer needed for PWM so set as input or whatever you like
#endif

#ifdef C8SYNC
	// pin 28 (C8) triggers DMA(port C) on falling edge of low duty waveform
	// pin 28 and 25 must be connected by the user: 25 is output, 28 is input
	pinMode(28, INPUT); //c8
        //PTC8 input,  IRQC0010 = DMA on rising edge
	CORE_PIN28_CONFIG = PORT_PCR_IRQC(2)|PORT_PCR_MUX(1);
#else
	// pin 15 (C0) triggers DMA(port C) on falling edge of low duty waveform
	// pin 15 and 16 must be connected by the user: 16 is output, 15 is input
	pinMode(15, INPUT); //c0
        //pin43 = PTC0 input,  IRQC0010 = DMA on rising edge
	CORE_PIN15_CONFIG = PORT_PCR_IRQC(2)|PORT_PCR_MUX(1);
#endif

	// pin 4 triggers DMA(port A) on falling edge of high duty waveform
        //pin29 = (A13) mux to FTM1_CH1 IRQC0010=DMA on falling edge
	CORE_PIN4_CONFIG = PORT_PCR_IRQC(2)|PORT_PCR_MUX(3);

	// enable clocks to the DMA controller and DMAMUX
	SIM_SCGC7 |= SIM_SCGC7_DMA;
	SIM_SCGC6 |= SIM_SCGC6_DMAMUX;
	DMA_CR = 0;
	DMA_ERQ = 0;

	// DMA channel #1 sets WS2811 high at the beginning of each cycle
#ifndef TWOPORT
        //original octo2811 8-channel DMA setup
	DMA_TCD1_SADDR = &ones;
	DMA_TCD1_SOFF = 0;
	DMA_TCD1_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
	DMA_TCD1_NBYTES_MLNO = 1;
	DMA_TCD1_SLAST = 0;
	DMA_TCD1_DADDR = &GPIOD_PSOR;
	DMA_TCD1_DOFF = 0;
	DMA_TCD1_CITER_ELINKNO = bufSize;
	DMA_TCD1_DLASTSGA = 0;
	DMA_TCD1_CSR = DMA_TCD_CSR_DREQ;
	DMA_TCD1_BITER_ELINKNO = bufSize;

	// DMA channel #2 writes the pixel data at 20% of the cycle
	DMA_TCD2_SADDR = frameBuffer[1];
	DMA_TCD2_SOFF = 1;
	DMA_TCD2_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
	DMA_TCD2_NBYTES_MLNO = 1;
	DMA_TCD2_SLAST = -bufSize;
	DMA_TCD2_DADDR = &GPIOD_PDOR;
	DMA_TCD2_DOFF = 0;
	DMA_TCD2_CITER_ELINKNO = bufSize;
	DMA_TCD2_DLASTSGA = 0;
	DMA_TCD2_CSR = DMA_TCD_CSR_DREQ;
	DMA_TCD2_BITER_ELINKNO = bufSize;

	// DMA channel #3 clear all the pins low at 48% of the cycle
	DMA_TCD3_SADDR = &ones;
	DMA_TCD3_SOFF = 0;
	DMA_TCD3_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
	DMA_TCD3_NBYTES_MLNO = 1;
	DMA_TCD3_SLAST = 0;
	DMA_TCD3_DADDR = &GPIOD_PCOR;
	DMA_TCD3_DOFF = 0;
	DMA_TCD3_CITER_ELINKNO = bufSize;
	DMA_TCD3_DLASTSGA = 0;
	DMA_TCD3_CSR = DMA_TCD_CSR_DREQ | DMA_TCD_CSR_INTMAJOR;
	DMA_TCD3_BITER_ELINKNO = bufSize;
#else
        //DrTune's 16-channel DMA setup
        
//0x40 byte address gap between sequential ports (a,b,c,d)
#define PORT_SPACING 0x40  /*between ports C and D */
#define MLOFFYES  (1+1) // 2 byte tranfers per minor loop (port C then D)
#define FREEZE_DEST_ADDR_BITS 7 /*force dest address to alternate between ports C+D */

	DMA_CR = (1<<7); //EMLM minor loop enabled;

        //write port C and D in a minor loop
	DMA_TCD1_SADDR = &ones;
	DMA_TCD1_SOFF = 0;
	DMA_TCD1_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0) | (FREEZE_DEST_ADDR_BITS<<3);
	DMA_TCD1_NBYTES_MLOFFYES = MLOFFYES;
	DMA_TCD1_SLAST = 0;
	DMA_TCD1_DADDR = &GPIOC_PSOR;
	DMA_TCD1_DOFF = PORT_SPACING;
	DMA_TCD1_CITER_ELINKNO = bufSize/2;
	DMA_TCD1_DLASTSGA = 0;
	DMA_TCD1_CSR = DMA_TCD_CSR_DREQ;
	DMA_TCD1_BITER_ELINKNO = bufSize/2;

	// DMA channel #2 writes the pixel data at 20% of the cycle
	DMA_TCD2_SADDR = frameBuffer[1];
	DMA_TCD2_SOFF = 1;
	DMA_TCD2_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0) | (FREEZE_DEST_ADDR_BITS<<3);;
	DMA_TCD2_NBYTES_MLOFFYES = MLOFFYES;
	DMA_TCD2_SLAST = -bufSize;
	DMA_TCD2_DADDR = &GPIOC_PDOR;
	DMA_TCD2_DOFF = PORT_SPACING;
	DMA_TCD2_CITER_ELINKNO = bufSize/2;
	DMA_TCD2_DLASTSGA = 0;
	DMA_TCD2_CSR = DMA_TCD_CSR_DREQ;
	DMA_TCD2_BITER_ELINKNO = bufSize/2;

	// DMA channel #3 clear all the pins low at 48% of the cycle
	DMA_TCD3_SADDR = &ones;
	DMA_TCD3_SOFF = 0;
	DMA_TCD3_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0) | (FREEZE_DEST_ADDR_BITS<<3);;
	DMA_TCD3_NBYTES_MLOFFYES = MLOFFYES;
	DMA_TCD3_SLAST = 0;
	DMA_TCD3_DADDR = &GPIOC_PCOR;
	DMA_TCD3_DOFF = PORT_SPACING;
	DMA_TCD3_CITER_ELINKNO = bufSize/2;
	DMA_TCD3_DLASTSGA = 0;
	DMA_TCD3_CSR = DMA_TCD_CSR_DREQ | DMA_TCD_CSR_INTMAJOR;
	DMA_TCD3_BITER_ELINKNO = bufSize/2;

#endif



#ifdef __MK20DX256__
	MCM_CR = MCM_CR_SRAMLAP(1) | MCM_CR_SRAMUAP(0);
	AXBS_PRS0 = 0x1032;
#endif

	// route the edge detect interrupts to trigger the 3 channels
	DMAMUX0_CHCFG1 = 0;
	DMAMUX0_CHCFG1 = DMAMUX_SOURCE_PORTB | DMAMUX_ENABLE;
	DMAMUX0_CHCFG2 = 0;
	DMAMUX0_CHCFG2 = DMAMUX_SOURCE_PORTC | DMAMUX_ENABLE;
	DMAMUX0_CHCFG3 = 0;
	DMAMUX0_CHCFG3 = DMAMUX_SOURCE_PORTA | DMAMUX_ENABLE;


	// enable a done interrupts when channel #3 completes
	NVIC_ENABLE_IRQ(IRQ_DMA_CH3);
	//pinMode(1, OUTPUT); // testing: oscilloscope trigger
}

void dma_ch3_isr(void)
{
	DMA_CINT = 3;
	update_completed_at = micros();
	update_in_progress = 0;
}

int OctoWS2811::busy(void)
{
	//if (DMA_ERQ & 0xE) return 1;
	if (update_in_progress) return 1;
	// busy for 50 us after the done interrupt, for WS2811 reset
	if (micros() - update_completed_at < 50) return 1;
	return 0;
}

void OctoWS2811::show()
{
	uint32_t cv, sc;

#ifdef DEBUGSYNC
	digitalWrite(0, 1);	// toggle B16 (pin0) so we can see on a scope
#endif
	// wait for any prior DMA operation
	while (update_in_progress) ; 

	memcpy(frameBuffer[1], frameBuffer[0], bufSize);

	// wait for WS2811 reset
	while (micros() - update_completed_at < 50) ;



	// ok to start, but we must be very careful to begin
	// without any prior 3 x 800kHz DMA requests pending
	sc = FTM1_SC;
	cv = FTM1_C1V;
	noInterrupts();

	DMA_TCD2_SADDR = frameBuffer[1];

	// CAUTION: this code is timing critical.  Any editing should be
	// tested by verifying the oscilloscope trigger pulse at the end
	// always occurs while both waveforms are still low.  Simply
	// counting CPU cycles does not take into account other complex
	// factors, like flash cache misses and bus arbitration from USB
	// or other DMA.  Testing should be done with the oscilloscope
	// display set at infinite persistence and a variety of other I/O
	// performed to create realistic bus usage.  Even then, you really
	// should not mess with this timing critical code!
	update_in_progress = 1;
	while (FTM1_CNT <= cv) ; 
	while (FTM1_CNT > cv) ; // wait for beginning of an 800 kHz cycle
	while (FTM1_CNT < cv) ;
	FTM1_SC = sc & 0xE7;	// stop FTM1 timer (hopefully before it rolls over)
	//digitalWriteFast(1, HIGH); // oscilloscope trigger
	PORTB_ISFR = (1<<0);    // clear any prior rising edge
	PORTC_ISFR = (1<<0);	// clear any prior low duty falling edge
	PORTA_ISFR = (1<<13);	// clear any prior high duty falling edge
	DMA_ERQ = 0x0E;		// enable all 3 DMA channels
	FTM1_SC = sc;		// restart FTM1 timer
	//digitalWriteFast(1, LOW);

#ifdef DEBUGSYNC
	digitalWrite(0, 0);	// B16 sync
#endif
	interrupts();

}

extern const uint8_t gammaTable[];

#define fb2color(n,shiftin,shiftout) ((gammaTableDithered[ (n>>shiftin)&0xff ])<<shiftout)
#ifndef OPTIMIZE_FOR_BLACK
#define adjustRGB(rgb)  (rgb ? (fb2color(rgb,16,0) | fb2color(rgb,8,8) | fb2color(rgb,0,16)) : 0)
#else
#define adjustRGB(rgb)  (fb2color(rgb,16,0) | fb2color(rgb,8,8) | fb2color(rgb,0,16))
#endif

void OctoWS2811::RGB888ToDrawBuffer(const uint32_t *in,int ditherCycle)
{  
    /* Converts RGB to planar bits and gamma corrects+dithers; neato - partically borrowed and tweaked from FadeCandy */

    uint32_t *out;
    out=frameBuffer[0];
    int ft=ditherCycle&((1<<DITHER_BITS)-1);
    const uint8_t *gammaTableDithered= gammaTable+(ft<<8);
    

    for (int i = 0; i < LEDS_PER_STRIP; ++i) {
        uint32_t p0;

#ifdef TWOPORT
        // 12 output words (16 strips)
        union {
            struct {
                uint32_t word1,word2;
            };
            struct {
                uint32_t p0a:1, p1a:1, p2a:1, p3a:1, p4a:1, p5a:1, p6a:1, p7a:1,
                         p8a:1, p9a:1, paa:1, pba:1, pca:1, pda:1, pea:1, pfa:1,
                         p0b:1, p1b:1, p2b:1, p3b:1, p4b:1, p5b:1, p6b:1, p7b:1,
                         p8b:1, p9b:1, pab:1, pbb:1, pcb:1, pdb:1, peb:1, pfb:1;
                uint32_t p0c:1, p1c:1, p2c:1, p3c:1, p4c:1, p5c:1, p6c:1, p7c:1,
                         p8c:1, p9c:1, pac:1, pbc:1, pcc:1, pdc:1, pec:1, pfc:1,
                         p0d:1, p1d:1, p2d:1, p3d:1, p4d:1, p5d:1, p6d:1, p7d:1,
                         p8d:1, p9d:1, pad:1, pbd:1, pcd:1, pdd:1, ped:1, pfd:1;
            };
        } o[6];
#else
        // Six output words (8 strips)
        union {
            struct {
              uint32_t word1;
            };
            struct {
                uint32_t p0a:1, p1a:1, p2a:1, p3a:1, p4a:1, p5a:1, p6a:1, p7a:1,
                         p0b:1, p1b:1, p2b:1, p3b:1, p4b:1, p5b:1, p6b:1, p7b:1,
                         p0c:1, p1c:1, p2c:1, p3c:1, p4c:1, p5c:1, p6c:1, p7c:1,
                         p0d:1, p1d:1, p2d:1, p3d:1, p4d:1, p5d:1, p6d:1, p7d:1;
            };
        } o[6];
#endif
        /*
         * Remap bits.
         *
         * This generates compact and efficient code using the BFI instruction (Fadecandy inspired)
         */

#ifdef OPTIMIZE_FOR_BLACK
        //clear output bytes because we may skip writing some of the bits if they're black
        o[0].word1=o[1].word1=o[2].word1=o[3].word1=o[4].word1=o[5].word1=0;
#ifdef TWOPORT
        o[0].word2=o[1].word2=o[2].word2=o[3].word2=o[4].word2=o[5].word2=0;
#endif       
#define BLACK_CHECK p0
#else
#define BLACK_CHECK 1
#endif         

// Read one RGB0888 pixel as an int and spread its bits out over the parallel writes
#define PIXEL_SLICE(n) \
        p0=in[ 0x0 ## n *LEDS_PER_STRIP]; \
        if (BLACK_CHECK) \
          { p0 = adjustRGB( p0 );  \
          o[5].p ## n ## d = p0; \
          o[5].p ## n ## c = p0 >> 1; \
          o[5].p ## n ## b = p0 >> 2; \
          o[5].p ## n ## a = p0 >> 3; \
          o[4].p ## n ## d = p0 >> 4; \
          o[4].p ## n ## c = p0 >> 5; \
          o[4].p ## n ## b = p0 >> 6; \
          o[4].p ## n ## a = p0 >> 7; \
          o[3].p ## n ## d = p0 >> 8; \
          o[3].p ## n ## c = p0 >> 9; \
          o[3].p ## n ## b = p0 >> 10; \
          o[3].p ## n ## a = p0 >> 11; \
          o[2].p ## n ## d = p0 >> 12; \
          o[2].p ## n ## c = p0 >> 13; \
          o[2].p ## n ## b = p0 >> 14; \
          o[2].p ## n ## a = p0 >> 15; \
          o[1].p ## n ## d = p0 >> 16; \
          o[1].p ## n ## c = p0 >> 17; \
          o[1].p ## n ## b = p0 >> 18; \
          o[1].p ## n ## a = p0 >> 19; \
          o[0].p ## n ## d = p0 >> 20; \
          o[0].p ## n ## c = p0 >> 21; \
          o[0].p ## n ## b = p0 >> 22; \
          o[0].p ## n ## a = p0 >> 23; \
        }

        PIXEL_SLICE(0)
        PIXEL_SLICE(1)
        PIXEL_SLICE(2)
        PIXEL_SLICE(3)
        PIXEL_SLICE(4)
        PIXEL_SLICE(5)
        PIXEL_SLICE(6)
        PIXEL_SLICE(7)
#ifdef TWOPORT
        PIXEL_SLICE(8)
        PIXEL_SLICE(9)
        PIXEL_SLICE(a)
        PIXEL_SLICE(b)
        PIXEL_SLICE(c)
        PIXEL_SLICE(d)
        PIXEL_SLICE(e)
        PIXEL_SLICE(f)
#endif

#ifdef TWOPORT
#define WRITE_OUT(n) *out++=o[n].word1; *out++=o[n].word2;
#else
#define WRITE_OUT(n) *out++=o[n].word1;
#endif
        WRITE_OUT(0);
        WRITE_OUT(1);
        WRITE_OUT(2);
        WRITE_OUT(3);
        WRITE_OUT(4);
        WRITE_OUT(5);

        in++;
    }
}


