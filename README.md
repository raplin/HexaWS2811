HexaWS2811
==========

8 or 16 channel plus luminance-response correction  &amp; dither version of PJRC's OctoWS2811 library for Teensy.
You probably want to use a Teensy3.1 for this due to ram usage.

The two main differences between this and OctoWS2811 are that
a) this can drive 16 channels of LEDs at once
b) the pixel buffer is kept as RGB888 format and corrected+dithered each time the frame is output.

-- Gamma.py
Dithering and luminance correction is done with a set of 8 bit lookup tables; each table converts a pixel R,G or B component into something better suited to the eye's response. This generates the tables, as C header files you #include in your code.

Depending on the update rate you can achieve (which with WS2812's depends on how many LEDs are in each strip), you may want to experiment with adjusting "ditherBits"; if low-intensity colors have a visible flicker you should run edit "gamma.py" to reduce 'ditherBits' by one, and run it to regenerate the lookup table for the Teensy code.

(As pointed out this isn't Gamma correction, it's compensating for the eye's luminance response, but the curves are very similar so it's just the wrong thing to call much the same effect)

