#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
// oslib
#include "font.h"
#include "socket.h"
#include "colourtran.h"
#include "osspriteop.h"
//
#include "cgi-lib.h"
#include "freegif.h"


#define BUFFERSIZE 2048

char outputbuffer[BUFFERSIZE];
int outputbytes;


int r0, r1, r2, r3;


//--------------------------------------------------------

osspriteop_area *createsprite(int x, int y) {

  osspriteop_area *spritearea;

  spritearea = malloc((x+3)*y+8*256+60+100);
  spritearea->size = (x+3)*y+8*256+60+100;
  spritearea->sprite_count = 0;
  spritearea->first = spritearea->used = 16;
  xosspriteop_create_sprite(osspriteop_USER_AREA, spritearea, "sprite", 0, x, y, (os_mode)28);
  xosspriteop_create_true_palette(osspriteop_USER_AREA, spritearea, (osspriteop_id)"sprite");

  return spritearea;
}


int spritepalette(int used, int *palette, int red, int green, int blue) {

  int rgb, i;

  rgb = (red<<8) | (green<<16) | (blue<<24);
  for (i = 0; i < used; i++)                      // check if it's already there...
    if (palette[2*i+0] == rgb)  return used;
  if (i >= 256)  return 256;                      // don't write too many entries
  palette[2*i+0] = palette[2*i+1] = rgb;          // add the value to the palette
  return i+1;
}


void switchtosprite(void *sprite) {

  xosspriteop_switch_output_to_sprite(osspriteop_USER_AREA, sprite, (osspriteop_id)"sprite",
                                      NULL, &r0, &r1, &r2, &r3);
}


void switchawayfromsprite() {
  xosspriteop_unswitch_output(r0, r1, r2, r3);
}



//--------------------------------------------------------

void writebytes(char *buffer, int bytes) {

  int i;

  for (i = 0; i < bytes; i++) {
    outputbuffer[outputbytes++] = buffer[i];
    if (outputbytes == BUFFERSIZE) {
      cgi_writebuffer(outputbuffer, outputbytes);
      outputbytes = 0;
    }
  }
}


void texttogif_info() {

  cgi_respons(200, "OK");
  cgi_writeheader("Content-Type: text/html");
  cgi_headerdone();

  cgi_writestring("<html><head><title>TextToGIF info</title></head>");
  cgi_writestring("<body bgcolor=\"#ffffff\">");
  cgi_writestring("<h3>TextToGIF info</h3>");
  cgi_writestring("This is version 0.02 of the TextToGIF cgi-script");
  cgi_writestring("</body></html>");
  cgi_quit();
}


void texttogif_error(char *text) {

  cgi_respons(400, "Bad request");
  cgi_writeheader("Content-Type: text/html");
  cgi_headerdone();

  cgi_writestring("<html><head><title>TextToGIF failed</title></head>");
  cgi_writestring("<body bgcolor=\"#ffffff\">");
  cgi_writestring("<h3>TextToGIF failed</h3>");
  cgi_writestring("<p>The following error was returned: <code>");
  cgi_writestring(text);
  cgi_writestring("</code></body></html>");
  cgi_quit();
}



int main(int argc, char *argv[]) {

  char text[256];
  char *fonts[12] = {
                    "Homerton.Medium",
                    "Homerton.Medium.Oblique",
                    "Homerton.Bold",
                    "Homerton.Bold.Oblique",
                    "Trinity.Medium",
                    "Trinity.Medium.Italic",
                    "Trinity.Bold",
                    "Trinity.Bold.Italic",
                    "Corpus.Medium",
                    "Corpus.Medium.Oblique",
                    "Corpus.Bold",
                    "Corpus.Bold.Oblique"  };

  int palette[256], *palptr;
  osspriteop_area *spritearea;
  font_scan_block block;
  font_string_flags flags;
  os_colour bgcolour, fgcolour, dropcolour;
  int varlen, vallen, i, j, n, x, y, plotx, ploty;
  int br, bg, bb, fr, fg, fb, sizex, sizey, chars;
  int drop, dropx, dropy, dropi, dropr, dropg, dropb;
  int font, fontsize, fmversion;
  char *next, *var, *val;
  font_f fonthandle;
  osspriteop_header *header;

  // possible arugments:
  // text=...
  // fr=red                   0..255
  // fg=green                 0..255
  // fb=blue                  0..255
  // br=red                   0..255
  // bg=green                 0..255
  // bb=blue                  0..255
  // size=pts                 4..100
  // font=fontfaceindex       0..11
  // drop=type                0 or 1
  // dropi=intensity          0..100
  // dropx=offset             -100..100 (%)
  // dropy=offset             -100..100 (%)

  // default values
  strcpy(text, "TextToGIF");
  br = bg = bb = 255;
  fr = fg = fb = 0;
  fontsize = 30;
  drop = dropx = dropy = dropi = 0;
  font = 0;

  if (cgi_init(argc, argv))  return 0;

  // read the arguments
  next = _cgi_parameters;
  do {
    next = (char *)cgi_extractparameter(next, &var, &varlen, &val, &vallen);
    if (varlen) {
      if (strcmp(var, "font") == 0)
        font = atoi(val);
      else if (strcmp(var, "size") == 0)
        fontsize = atoi(val);
      else if (strcmp(var, "text") == 0) {
        if (vallen > 255)  vallen = 255;
        chars = cgi_decodeescaped(val, text, vallen);
        text[chars] = '\0';
        for (i = 0; i < strlen(text); i++)
          if (text[i] < ' ')  text[i] = ' ';
      } else if (strcmp(var, "fr") == 0)
        fr = atoi(val);
      else if (strcmp(var, "fg") == 0)
        fg = atoi(val);
      else if (strcmp(var, "fb") == 0)
        fb = atoi(val);
      else if (strcmp(var, "br") == 0)
        br = atoi(val);
      else if (strcmp(var, "bg") == 0)
        bg = atoi(val);
      else if (strcmp(var, "bb") == 0)
        bb = atoi(val);
      else if (strcmp(var, "drop") == 0)
        drop = atoi(val);
      else if (strcmp(var, "dropx") == 0)
        dropx = atoi(val);
      else if (strcmp(var, "dropy") == 0)
        dropy = atoi(val);
      else if (strcmp(var, "dropi") == 0)
        dropi = atoi(val);
      else if (strcmp(var, "info") == 0)
        texttogif_info();
    }
  } while (next);

  // ensure the arguments are legal
  if ((fr < 0) || (fg < 0) || (fb < 0) || (br < 0) || (bg < 0) || (bb < 0))
    texttogif_error("Illegal colour value in the request.");
  if ((fr > 255) || (fg > 255) || (fb > 255) || (br > 255) || (bg > 255) || (bb > 255))
    texttogif_error("Illegal colour value in the request.");

  if ((drop != 0) & (drop != 1))
    texttogif_error("Unsupported drop-shadow type.");
  if ((drop == 1) && ((dropx < -100) || (dropx > 100) || (dropy < -100) || (dropy > 100) ||
                      (dropi < 0) || (dropi > 100)))
    texttogif_error("Illegal drop-shadow parameter.");

  if ((fontsize < 4) || (fontsize > 100) || (font < 0) || (font > 11))
    texttogif_error("Illegal font size/face in the request.");

  if (xfont_find_font(fonts[font], fontsize*16, fontsize*16, 0, 0, &fonthandle, NULL, NULL))
    texttogif_error("Couldn't get font handle");

  // colours
  bgcolour = (os_colour)((br<<8) | (bg<<16) | (bb<<24));
  fgcolour = (os_colour)((fr<<8) | (fg<<16) | (fb<<24));

  block.space.x = block.space.y = block.letter.x = block.letter.y = 0;
  block.split_char = -1;
  if (xfont_scan_string(fonthandle, text, 0x401a0, 0x7f000000, 0x7f000000,
                        &block, NULL, strlen(text), NULL, NULL, NULL, NULL)) {
    xfont_lose_font(fonthandle);
    texttogif_error("Couldn't read bounding box");
  }

  if (xfont_convertto_os(block.bbox.x1 - block.bbox.x0, block.bbox.y1 - block.bbox.y0, &x, &y)) {
    xfont_lose_font(fonthandle);
    texttogif_error("Bad bounding box");
  }
  sizex = x/2 + 2;                      // values are in pixels
  sizey = y/2 + 2;

  if (dropi == 0)  drop = 0;            // no shadow
  if (drop == 1) {
    dropx = dropx*fontsize/40;
    dropy = dropy*fontsize/40;
    sizex += abs(dropx)/2;
    sizey += abs(dropy)/2;
    dropr = (dropi*fr + (100-dropi)*br)/100;
    dropg = (dropi*fg + (100-dropi)*bg)/100;
    dropb = (dropi*fb + (100-dropi)*bb)/100;
    dropcolour = (os_colour)((dropr<<8) | (dropg<<16) | (dropb<<24));
  }
  if ((sizex < 10) || (sizey < 5) || (sizex > 1200) || (sizey > 250) ||
      (sizex*sizey > 100000)) {
    xfont_lose_font(fonthandle);
    texttogif_error("Text is too small or too big!");
  }

  // create a sprite with palette
  spritearea = createsprite(sizex, sizey);
  if (!spritearea) {
    xfont_lose_font(fonthandle);
    texttogif_error("malloc() failed");
  }

  xosspriteop_select_sprite(osspriteop_USER_AREA, spritearea, (osspriteop_id)"sprite", &header);
  palptr = (int *)(header + 1);
  for (i = 0; i < 256; i++)  palptr[2*i+0] = palptr[2*i+1] = 0;

  // create the palette
  n = 0;
  for (i = 0; i < 16; i++)
    n = spritepalette(n, palptr,
                      (br*(15-i) + (fr*i))/15,
                      (bg*(15-i) + (fg*i))/15,
                      (bb*(15-i) + (fb*i))/15);
  if (drop == 1) {
    for (j = 16; j < 32; j++) {
      int i;

      i = j-16;
      n = spritepalette(n, palptr,
                        (br*(15-i) + (dropr*i))/15,
                        (bg*(15-i) + (dropg*i))/15,
                        (bb*(15-i) + (dropb*i))/15);
    }
  }

  cgi_multitask("texttogif", 250);

  xfont_convertto_os(-block.bbox.x0 + 400,  -block.bbox.y0 + 400, &plotx, &ploty);
  if (drop == 1) {
    if (dropx < 0)  plotx -= dropx;
    if (dropy < 0)  ploty -= dropy;
  }

  // does the fontmanager support backgrund blending?
  xfont_cache_addr(&fmversion, NULL, NULL);

  // paint the text
  flags = font_OS_UNITS;
  if (fmversion >= 337)  flags |= 1<<11;
  switchtosprite(spritearea);
  xcolourtrans_set_gcol(bgcolour, 0, 0, NULL, NULL);
  xos_plot(os_MOVE_TO, 0, 0);
  xos_plot(os_PLOT_RECTANGLE | os_PLOT_TO, sizex*2, sizey*2);
  if (drop == 1) {
    xcolourtrans_set_font_colours(fonthandle, bgcolour, dropcolour, 14, NULL, NULL, NULL);
    xfont_paint(fonthandle, text, flags, plotx+dropx, ploty+dropy, NULL, NULL, NULL);
  }
  xcolourtrans_set_font_colours(fonthandle, bgcolour, fgcolour, 14, NULL, NULL, NULL);
  xfont_paint(fonthandle, text, flags, plotx, ploty, NULL, NULL, NULL);
  switchawayfromsprite();

  outputbytes = 0;

  // HTTP respons header
  cgi_respons(200, "OK");
  cgi_writeheader("Content-Type: image/gif");
  cgi_headerdone();

// debugging: xosspriteop_save_sprite_file(osspriteop_USER_AREA,spritearea,"$.dump");
  // GIF palette
  for (i = 0; i < 255; i++)   palette[i] = (palptr[2*i+0])>>8;

  // make the GIF
  GIFEncode((void (*)(char *, int))writebytes, sizex, sizey, 255, 255, 8, palette, (char *)(palptr + 2*256), (sizex + 3) & ~3);
  if (outputbytes)  cgi_writebuffer(outputbuffer, outputbytes);

  // tidy and quit
  xfont_lose_font(fonthandle);
  free(spritearea);
  cgi_quit();
}
