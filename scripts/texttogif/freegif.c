
/*-----------------------------------------------------------------------
 *
 * miGIF Compression - mouse and ivo's GIF-compatible compression
 *
 *          -run length encoding compression routines-
 *
 * Copyright (C) 1998 Hutchison Avenue Software Corporation
 *               http://www.hasc.com
 *               info@hasc.com
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "AS IS." The Hutchison Avenue
 * Software Corporation disclaims all warranties, either express or implied,
 * including but not limited to implied warranties of merchantability and
 * fitness for a particular purpose, with respect to this code and accompanying
 * documentation.
 *
 * The miGIF compression routines do not, strictly speaking, generate files
 * conforming to the GIF spec, since the image data is not LZW-compressed
 * (this is the point: in order to avoid transgression of the Unisys patent
 * on the LZW algorithm.)  However, miGIF generates data streams that any
 * reasonably sane LZW decompresser will decompress to what we want.
 *
 * miGIF compression uses run length encoding. It compresses horizontal runs
 * of pixels of the same color. This type of compression gives good results
 * on images with many runs, for example images with lines, text and solid
 * shapes on a solid-colored background. It gives little or no compression
 * on images with few runs, for example digital or scanned photos.
 *
 *                               der Mouse
 *                      mouse@rodents.montreal.qc.ca
 *            7D C8 61 52 5D E7 2D 39  4E F1 31 3E E8 B3 27 4B
 *
 *                             ivo@hasc.com
 *
 * The Graphics Interchange Format(c) is the Copyright property of
 * CompuServe Incorporated.  GIF(sm) is a Service Mark property of
 * CompuServe Incorporated.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freegif.h"

int rl_pixel;
int rl_basecode;
int rl_count;
int rl_table_pixel;
int rl_table_max;
int just_cleared;
int out_bits;
int out_bits_init;
int out_count;
int out_bump;
int out_bump_init;
int out_clear;
int out_clear_init;
int max_ocodes;
int code_clear;
int code_eof;
unsigned int obuf;
int obits;
unsigned char oblock[256];
int oblen;


void write_block(void);
void write_byte(unsigned char c);
void output(int val);
void did_clear(void);
void output_plain(int c);
unsigned int isqrt(unsigned int x);
unsigned int compute_triangle_count(unsigned int count, unsigned int nrepcodes);
void reset_out_clear(void);
void rl_flush_fromclear(int count);
void rl_flush_clearorrep(int count);
void rl_flush_withtable(int count);
void rl_flush(void);


void (*writefn)(char *, int);



void putbyte(int val) {

  (writefn)((char *)&val, 1);
}


void putshort(int val) {

  putbyte(val &255);
  putbyte(val >>8);
}


int GIFEncode(void (*write)(char *, int), int width, int height, int back, int transparent, int bpp, int *palette, char *image, int bpl) {
// write        function to call to write a byte
// width        image width in pixels
// height       image height in pixels
// back         background colour
// transparent  transparent colour index or -1
// bpp          bits per pixel, 4 or 8
// palette      palette
// image        pointer to first pixel
// bpl          bytes per line
//
// return NULL (OK) or != NULL (error)
  int flags, i, c, y, x, initbits;

  writefn = write;

  if ((width < 0) || (width > 65535) || (height < 0) || (height > 65535) ||
      ((bpp != 4) && (bpp != 8)) ) return 1;

  // Write the Magic header
  if (transparent >= 0)
    (writefn)("GIF89a", 6);
  else
    (writefn)("GIF87a", 6);

  // Write out the screen width and height
  putshort(width);
  putshort(height);

  // Indicate that there is a global colour map
  flags = 0x80;       // Yes, there is a colour map

  // OR in the resolution
  flags |= (bpp - 1) << 4;

  // OR in the Bits per Pixel
  flags |= (bpp - 1);

  // Write it out
  putbyte(flags);

  // Write out the Background colour
  putbyte(back);

  // Byte of 0's (future expansion)
  putbyte(0);

  // Write out the Global Colour Map
  for(i = 0; i < (1 << bpp); ++i) {
    putbyte(palette[i] &255);
    putbyte((palette[i]>>8) &255);
    putbyte((palette[i]>>16) &255);
  }

  // write out extension for transparent colour index, if necessary.
  if (transparent >= 0) {
    putbyte('!');
    putbyte(0xf9);
    putbyte(4);
    putbyte(1);
    putbyte(0);
    putbyte(0);
    putbyte(transparent);
    putbyte(0);
  }

  // Write an Image separator
  putbyte(',');

  // Write the Image header
  putshort(0);
  putshort(0);
  putshort(width);
  putshort(height);

  // Write out whether or not the image is interlaced
  putbyte(0);

  // Write out the initial code size
  putbyte(bpp);

  // Go and actually compress the data
  initbits = bpp+1;

  obuf = 0;
  obits = 0;
  oblen = 0;
  rl_pixel = -1;
  code_clear = 1 << (initbits - 1);
  code_eof = code_clear + 1;
  rl_basecode = code_eof + 1;
  out_bump_init = (1 << (initbits - 1)) - 1;

 /* for images with a lot of runs, making out_clear_init larger will
    give better compression. */
  out_clear_init = (initbits <= 3) ? 9 : (out_bump_init-1);

  out_bits_init = initbits;
  max_ocodes = (1 << GIFBITS) - ((1 << (out_bits_init - 1)) + 3);
  did_clear();
  output(code_clear);
  rl_count = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      if (bpp == 4) {
        c = image[(x+bpl*y)>>1];
        if (x &1)
          c>>=4;
        else
          c&=15;
      } else
        c = image[x+bpl*y];

      if ((rl_count > 0) && (c != rl_pixel))  rl_flush();
      if (rl_pixel == c)
        rl_count++;
      else {
        rl_pixel = c;
        rl_count = 1;
      }
    }
  }

  if (rl_count > 0)  rl_flush();

  output(code_eof);
  if (obits > 0)  write_byte(obuf);
  if (oblen > 0)  write_block();

  // Write out a Zero-length packet (to end the series)
  putbyte(0);

  // Write the GIF file terminator
  putbyte(';');

  return NULL;
}



void write_block(void) {

  putbyte(oblen);
  (writefn)((char *)oblock, oblen);
  oblen = 0;
}


void write_byte(unsigned char c) {

  oblock[oblen++] = c;
  if (oblen >= 255)  write_block();
}


void output(int val) {

  obuf |= val << obits;
  obits += out_bits;
  while (obits >= 8) {
    write_byte(obuf & 0xff);
    obuf >>= 8;
    obits -= 8;
  }
}


void did_clear(void) {

  out_bits = out_bits_init;
  out_bump = out_bump_init;
  out_clear = out_clear_init;
  out_count = 0;
  rl_table_max = 0;
  just_cleared = 1;
}


void output_plain(int c) {

  just_cleared = 0;
  output(c);
  out_count ++;
  if (out_count >= out_bump) {
    out_bits ++;
    out_bump += 1 << (out_bits - 1);
  }
  if (out_count >= out_clear) {
    output(code_clear);
    did_clear();
  }
}


unsigned int isqrt(unsigned int x) {

  unsigned int r;
  unsigned int v;

  if (x < 2)  return x;
  for (v = x, r = 1; v ; v >>= 2, r <<= 1);
  while (1) {
    v = ((x / r) + r) / 2;
    if ((v == r) || (v == r+1))  return r;
    r = v;
  }
}


unsigned int compute_triangle_count(unsigned int count, unsigned int nrepcodes) {
  unsigned int perrep;
  unsigned int cost;

  cost = 0;
  perrep = (nrepcodes * (nrepcodes+1)) / 2;
  while (count >= perrep) {
    cost += nrepcodes;
    count -= perrep;
  }
  if (count > 0) {
    unsigned int n;

    n = isqrt(count);
    while ((n*(n+1)) >= 2*count) n --;
    while ((n*(n+1)) < 2*count) n ++;
    cost += n;
  }

  return cost;
}


void reset_out_clear(void) {

  out_clear = out_clear_init;
  if (out_count >= out_clear) {
    output(code_clear);
    did_clear();
  }
}


void rl_flush_fromclear(int count) {

  int n;

  out_clear = max_ocodes;
  rl_table_pixel = rl_pixel;
  n = 1;
  while (count > 0) {
    if (n == 1) {
      rl_table_max = 1;
      output_plain(rl_pixel);
      count --;
    } else if (count >= n) {
      rl_table_max = n;
      output_plain(rl_basecode+n-2);
      count -= n;
    } else if (count == 1) {
      rl_table_max ++;
      output_plain(rl_pixel);
      count = 0;
    } else {
      rl_table_max ++;
      output_plain(rl_basecode+count-2);
      count = 0;
    }

    if (out_count == 0)
      n = 1;
    else
      n ++;

  }

  reset_out_clear();
}


void rl_flush_clearorrep(int count) {

  int withclr;

  withclr = 1 + compute_triangle_count(count,max_ocodes);
  if (withclr < count) {
    output(code_clear);
    did_clear();
    rl_flush_fromclear(count);
  } else
    for ( ; count > 0; count--)  output_plain(rl_pixel);
}


void rl_flush_withtable(int count) {

  int repmax;
  int repleft;
  int leftover;

  repmax = count / rl_table_max;
  leftover = count % rl_table_max;
  repleft = (leftover ? 1 : 0);
  if (out_count+repmax+repleft > max_ocodes) {
    repmax = max_ocodes - out_count;
    leftover = count - (repmax * rl_table_max);
    repleft = 1 + compute_triangle_count(leftover,max_ocodes);
  }
  if (1+compute_triangle_count(count,max_ocodes) < repmax+repleft) {
    output(code_clear);
    did_clear();
    rl_flush_fromclear(count);
    return;
  }

  out_clear = max_ocodes;

  for ( ; repmax > 0 ; repmax--)
    output_plain(rl_basecode+rl_table_max-2);

  if (leftover) {
    if (just_cleared)
      rl_flush_fromclear(leftover);
    else if (leftover == 1)
      output_plain(rl_pixel);
    else
      output_plain(rl_basecode+leftover-2);
  }

  reset_out_clear();
}


void rl_flush(void) {

  if (rl_count == 1) {
    output_plain(rl_pixel);
    rl_count = 0;
    return;
  }

  if (just_cleared)
    rl_flush_fromclear(rl_count);
  else if ((rl_table_max < 2) || (rl_table_pixel != rl_pixel))
    rl_flush_clearorrep(rl_count);
  else
    rl_flush_withtable(rl_count);

  rl_count = 0;
}
