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
*/


#include <Arduino.h>

// ############################################### Main switches #############################################
//>>>>> Select 8channel (original) or 16channel (TWOPORT defined) <<<<
//#define TWOPORT

//If defined, Use C8(28)->B0 instead of C0->B0 for DMA trigger input (so we can use PC0-7 entirely for leds)
//#define C8SYNC

//300 leds = 110hz, 150 leds=220hz, etc.
#define LEDS_PER_STRIP (300)


// If you have a large % of black (000RGB) pixels this will make the bit-mangling much faster - but variable speed depending on pixel contents so beware
#define OPTIMIZE_FOR_BLACK

// ##########################################################################################################

#ifdef TWOPORT
#define NUM_STRIPS 16
#else
#define NUM_STRIPS 8
#endif


class OctoWS2811 {
public:
	OctoWS2811(uint32_t numPerStrip, void *frameBuf1,void *frameBuf2);
	void begin(void);

        void RGB888ToDrawBuffer(const uint32_t *in,int ditherCycle);

        void show();

	int busy(void);

	int color(uint8_t red, uint8_t green, uint8_t blue) {
		return (red << 16) | (green << 8) | blue;
	}
	

private:
        static uint32_t bufSize;

	static uint32_t *frameBuffer[2];
	static uint8_t params;
};

