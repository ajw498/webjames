#define GIFBITS 12

int GIFEncode(void (*write)(char *, int), int width, int height, int back, int transparent, int bpp, int *palette, char *image, int bpl);
