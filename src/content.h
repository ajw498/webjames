#ifndef CONTENT_H
#define CONTENT_H


struct varmap {
	char *uri;
	char *type;
	float qs;
	float score;
	char *language;
	char *charset;
	char *encoding;
	char *description;
	struct varmap *next;
};

#ifdef CONTENT

void content_init(void);
int content_negotiate(struct connection *conn);
void content_recordextension(char *extn);
void content_starthandler(struct connection *conn);

#else

#define content_init() 0
#define content_recordextension(x) 0
#define content_starthandler(x) 0
#define content_negotiate(x) 0

#endif

#endif

