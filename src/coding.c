#include "coding.h"

int decode_base64(char *out, char *in, int bytes) {
/* decodes a base64 stream which previously must have been */
/* converted from seperate lines to a single stream */
/* returns the number of bytes output by the decoder */

	int i, c, written, left;
	char a[4], b[4], o[3];
	char dtable[255];

	for (i = 0; i < 255; i++)      dtable[i] = 0x80;
	for (i = 'A'; i <= 'Z'; i++)   dtable[i] = 0 + (i - 'A');
	for (i = 'a'; i <= 'z'; i++)   dtable[i] = 26 + (i - 'a');
	for (i = '0'; i <= '9'; i++)   dtable[i] = 52 + (i - '0');
	dtable['+'] = 62;
	dtable['/'] = 63;
	dtable['='] = 0;

	left = bytes;
	written = 0;
	while (left > 0) {

		for (i = 0; i < 4; i++) {

			c = *in++;

			if (dtable[c] & 0x80)
				i--;
			else {
				a[i] = c;
				b[i] = dtable[c];

				left--;
				if (left == 0) {
					/* done ?? */
				}
			}
		}
		o[0] = (b[0] << 2) | (b[1] >> 4);
		o[1] = (b[1] << 4) | (b[2] >> 2);
		o[2] = (b[2] << 6) | b[3];
		i = a[2] == '=' ? 1 : (a[3] == '=' ? 2 : 3);

		for (c = 0; c < i; c++)  *out++ = o[c];
		written += c;
		if (i < 3)   return written;
	}

	return written;
}
