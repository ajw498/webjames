#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cgi-lib.h"
#include "freegif.h"


char outputbuffer[2048];
int outputbytes;


int r0, r1, r2, r3;


//--------------------------------------------------------

void *createsprite(int x, int y) {

  void *spritearea;
  int *spr, i;

  spritearea = malloc((x+3)*y+8*256+60+100);
  spr = spritearea;
  spr[0] = (x+3)*y+8*256+60+100;
  spr[1] = 0;
  spr[2] = 16;
  spr[3] = 16;
  registers[0] = 256+15;
  registers[1] = (int)spritearea;
  registers[2] = (int)"sprite";
  registers[3] = 0;
  registers[4] = x;
  registers[5] = y;
  registers[6] = 28;
  swix(OS_SpriteOp);
  registers[0] = 256+37;
  registers[1] = (int)spritearea;
  registers[2] = (int)"sprite";
  registers[3] = 0x80000001;
  swix(OS_SpriteOp);

  for (i = 0; i < 255; i++) {
    spr[2*i+0+15] = 0;
    spr[2*i+1+15] = 0;
  }

  return spritearea;
}


void spritepalette(void *sprite, int i, int red, int green, int blue) {

  int *palette;

  if ((i < 0) || (i > 255))  return;

  palette = sprite;
  palette[2*i+0+15] = (red<<8) | (green<<16) | (blue<<24);
  palette[2*i+1+15] = (red<<8) | (green<<16) | (blue<<24);
}


void switchtosprite(void *sprite) {

  registers[0] = 256+60;
  registers[1] = (int)sprite;
  registers[2] = (int)"sprite";
  registers[3] = 0;
  swix(OS_SpriteOp);
  r0 = registers[0];
  r1 = registers[1];
  r2 = registers[2];
  r3 = registers[3];
}


void switchawayfromsprite() {

  registers[0] = r0;
  registers[1] = r1;
  registers[2] = r2;
  registers[3] = r3;
  swix(OS_SpriteOp)
}



//--------------------------------------------------------

void writebytes(char *buffer, int bytes) {

  int i;

  for (i = 0; i < bytes; i++) {
    outputbuffer[outputbytes++] = buffer[i];
    if (outputbytes == 2048) {
      cgi_writebuffer(outputbuffer, outputbytes);
      outputbytes = 0;
    }
  }
}



void counter_error() {

  cgi_respons(400, "Bad request");
  cgi_headerdone();

  cgi_quit();
}



int main(int argc, char *argv[]) {

  char text[16];
  int palette[256], *sprite;
  void *spritearea;
  int bgcolour, fgcolour, varlen, vallen, i;
  int sizex, sizey, count, id, fonthandle;
  int fr, fg, fb, br, bg, bb, file;
  char *next, *var, *val;

  // default values
  fgcolour = 0x00000000;
  bgcolour = 0xffffff00;
  id = -1;

  if (cgi_init(argc, argv))  return 0;

  next = _cgi_parameters;
  do {
    next = (char *)cgi_extractparameter(next, &var, &varlen, &val, &vallen);
    if (varlen) {
      if (strcmp(var, "id") == 0) {
        id = atoi(val);
      } else if ((strcmp(var, "fg") == 0) && (vallen > 0) && (vallen <= 6)) {
        sscanf(val, "%x", &fgcolour);
        fgcolour <<= 8;
      } else if ((strcmp(var, "bg") == 0) && (vallen > 0) && (vallen <= 6)) {
        sscanf(val, "%x", &bgcolour);
        bgcolour <<= 8;
      }
    }
  } while (next);

  if ((id < 0) || (id > 4095))   counter_error();

  registers[0] = 0xc3;
  registers[1] = (int)"<WebJames$Dir>.cgi-res.counter";
  registers[2] = 0;
  if (os_swix(OS_Find, registers))  counter_error();
  file = registers[0];
  if (file == 0)  counter_error();
  registers[0] = 3;
  registers[1] = file;
  registers[2] = (int)&count;
  registers[3] = 4;
  registers[4] = 4*id;
  if (os_swix(OS_GBPB, registers)) {
    registers[0] = 0;
    registers[1] = file;
    swix(OS_Find);
    counter_error();
  }
  if (registers[3] != 0) {
    registers[0] = 0;
    registers[1] = file;
    swix(OS_Find);
    counter_error();
  }
  count++;
  registers[0] = 1;
  registers[1] = file;
  registers[2] = (int)&count;
  registers[3] = 4;
  registers[4] = 4*id;
  swix(OS_GBPB);

  registers[0] = 0;
  registers[1] = file;
  swix(OS_Find);

  // file opening/reading/closing using stdio is much
  // slower than using OS_Find/OS_GBPB
  //file = fopen("<WebJames$Dir>.cgi-res.counter", "a+b");
  //if (!file)  counter_error();
  //fseek(file, (long)(4*id), SEEK_SET);
  //fread(&count, 1, 4, file);
  //count++;
  //fseek(file, (long)(4*id), SEEK_SET);
  //fwrite(&count, 1, 4, file);
  //fclose(file);

  sprintf(text, "%08d", count);

  registers[1] = (int)"Corpus.Medium";
  registers[2] = 20*16;
  registers[3] = 20*16;
  registers[4] = 0;
  registers[5] = 0;
  if (os_swix(Font_FindFont, registers))  counter_error();
  fonthandle = registers[0];

  sizex = 120;
  sizey = 20;

  spritearea = createsprite(sizex, sizey);
  if (!spritearea) {
    registers[0] = fonthandle;
    swix(Font_LoseFont);
    counter_error();
  }

  fr = (fgcolour>>8)&255;
  fg = (fgcolour>>16)&255;
  fb = (fgcolour>>24)&255;
  br = (bgcolour>>8)&255;
  bg = (bgcolour>>16)&255;
  bb = (bgcolour>>24)&255;

  for (i = 0; i < 15; i++) {
    spritepalette(spritearea, i,
                  (br*(15-i) + (fr*i))/15,
                  (bg*(15-i) + (fg*i))/15,
                  (bb*(15-i) + (fb*i))/15);
  }

  cgi_multitask("CGI/counter", 100);

  switchtosprite(spritearea);
  registers[0] = bgcolour;
  registers[3] = 0;
  registers[4] = 0;
  swix(ColourTrans_SetGCOL);
  bbc_rectanglefill(0,0, sizex*2, sizey*2);
  registers[0] = fonthandle;
  registers[1] = bgcolour;
  registers[2] = fgcolour;
  registers[3] = 14;
  swix(ColourTrans_SetFontColours);
  registers[0] = fonthandle;
  registers[1] = (int)text;
  registers[2] = 16;
  registers[3] = 0;
  registers[4] = 4;
  swix(Font_Paint);
  switchawayfromsprite();


  cgi_respons(200, "OK");
  cgi_writeheader("Content-Type: image/gif");
  cgi_headerdone();

  sprite = spritearea;
  for (i = 0; i < 255; i++)
    palette[i] = (sprite[15+2*i])>>8;

  outputbytes = 0;
  GIFEncode((void (*)(char *, int))writebytes, sizex, sizey, 255, 255, 8, palette, (char *)(sprite+15+2*256), (sizex + 3) & ~3);
  if (outputbytes)  cgi_writebuffer(outputbuffer, outputbytes);

  registers[0] = fonthandle;
  swix(Font_LoseFont);

  cgi_quit();
}
