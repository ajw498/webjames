/* Content negotiation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "oslib/osfile.h"
#include "oslib/osfscontrol.h"
#include "oslib/osgbpb.h"
#include "oslib/mimemap.h"

#include "webjames.h"
#include "write.h"
#include "report.h"
#include "content.h"

#define MAP_CACHE_SIZE 10


static struct {
	struct varmap *map;
	char *filename;
	size_t len;
	char date[5];
} mapcache[MAP_CACHE_SIZE];



struct accept {
	char type[20]; /*length checking*/
	float q;
	enum {
		normal,
		wild,
		semiwild
	} wild;
	struct accept *next;
};

enum field {
	field_accept,
	field_language,
	field_charset,
	field_encoding
};

void content_init(void)
{
	int i;
	for (i=0;i<MAP_CACHE_SIZE;i++) {
		mapcache[i].len=0;
		mapcache[i].filename=NULL;
		mapcache[i].map=NULL;
	}
}

static void content_freemap(struct varmap *map)
{
	struct varmap *next;
	if (map==NULL) return;
	do {
		next=map->next;
		if (map->uri) free(map->uri);
		if (map->type) free(map->type);
		if (map->language) free(map->language);
		if (map->charset) free(map->charset);
		if (map->encoding) free(map->encoding);
		if (map->description) free(map->description);
		free(map);
		map=next;
	} while (map);
}

static void content_freeaccept(struct accept *accept)
{
	struct accept *next;
	if (accept==NULL) return;
	do {
		next=accept->next;
		free(accept);
		accept=next;
	} while (accept);
}

static struct accept *content_parseaccept(char *a)
{
	struct accept *parsed=NULL,*parse;
	int fudgequality=1;
	char *b;

	if (a==NULL) return NULL;
	while (*a) {
		parse=malloc(sizeof(struct accept));
		if (parse==NULL) return NULL; /*better error?*/
		parse->next=parsed;
		parsed=parse;

		while (isspace(*a)) a++; /*skip leading spaces*/
		b=a;
		while (*b && !isspace(*b) && *b!=',' && *b!=';') b++; /*find end of first acceptable type*/
		memcpy(parse->type,a,b-a);
		parse->type[b-a]='\0';
		a=b;
		if (*a==';') {
			while (isspace(*++a));
			if (a[0]=='q' && a[1]=='=') {
				parse->q=(float)strtod(a+2,&a);
				fudgequality=0;
			}
		} else {
			parse->q=1;
		}
		while (*a && *a!=',') a++;
		if (*a==',') a++;
	}

	/*Find all the wildcards, alter their quality if the browser does not supply any qualities at all*/
	parse=parsed;
	while (parse) {
		if (strcmp(parse->type,"*")==0 || strcmp(parse->type,"*/*")==0) {
			parse->wild=wild;
			if (fudgequality) parse->q=(float)0.01;
		} else if (strncmp(parse->type+strlen(parse->type)-2,"/*",2)==0) {
			parse->wild=semiwild;
			if (fudgequality) parse->q=(float)0.02;
		} else {
			parse->wild=normal;
		}
		parse=parse->next;
	}
	return parsed;
}

static void content_updatescores(struct varmap *map,char *line,enum field field)
{
	struct accept *acceptlist;

	acceptlist=content_parseaccept(line);

	while (map) {
		int match=0;
		struct accept *accept;

		accept=acceptlist;
		while (accept && !match) {
			switch (accept->wild) {
				case normal:
					if (field==field_language) {
						size_t len=strlen(accept->type);
						if (map->language) {
							if (strncmp(map->language,accept->type,len)==0) {
								/*allow "en" to match "en" or "en-gb" */
								if (map->language[len]=='\0' || map->language[len]=='-') {
									match=1;
									map->score*=1 * (accept->q);
								}
							}
						} else {
							map->score*=(float)0.01; /*give variants with no language a low score*/
						}
					} else {
						char *str="";
						switch (field) {
							case field_accept:
								str=map->type;
								if (str==NULL) str="text/plain";
								break;
							case field_encoding:
								str=map->encoding;
								if (str==NULL) str="identity";
								break;
							case field_charset:
								str=map->charset;
								if (str==NULL) str="ISO-8859-1";
								break;
						}
						if (strcmp(str,accept->type)==0) {
							match=1;
							map->score*=(field==field_accept ? map->qs : 1) * (accept->q);
						}
					}
					break;
				case semiwild:
					if (strncmp(map->type,accept->type,strlen(accept->type)-2)==0) {
						match=1;
						map->score*=map->qs*accept->q;
					}
					break;
				case wild:
					match=1;
					map->score*=(field==field_accept ? map->qs : (field==field_language && map->language==NULL) ? (float)0.01 : 1)*accept->q;
					break;
			}
			accept=accept->next;
		}
		if (!match
			&& !(field==field_encoding && strcmp(map->encoding,"identity")==0)
			&& !(field==field_charset && strcmp(map->charset,"ISO-8859-1")==0)
			) map->score=0;
		/*identity encoding and ISO-8859-1 charset are always acceptable unless specifically mentioned as not acceptable*/
		map=map->next;
	}
	content_freeaccept(acceptlist);
}

static struct varmap *content_multiviews(char *dirname,char *leafname)
/* Creates a map structure based on directory contents */
{
	char buffer[256];
	int count;
	int more = 0;
	size_t len = strlen(leafname);
	struct varmap *maps=NULL;

	do {
		if (xosgbpb_dir_entries(dirname,(osgbpb_string_list *)buffer,1,more,256,NULL,&count,&more)) return 0;
		if (count) {
			if (strncmp(buffer,leafname,len) == 0) {
				struct varmap *map;
				size_t len2;
				char buf2[256];
				char *extn,*lang;

				extn=strrchr(buffer,'/');
				if (extn==NULL) continue;
				lang=strchr(buffer,'/'); /*assume all files are name/lang/extn or name/extn */
				if (lang==extn) lang=NULL;

				map=malloc(sizeof(struct varmap));
				if (map==NULL) return NULL;
				map->next=maps;
				maps=map;
				len2=strlen(buffer);
				map->uri=malloc(len2+1);
				if (map->uri==NULL) return NULL;
				memcpy(map->uri,buffer,len2+1);

				map->language=map->charset=map->encoding=map->description=NULL;
				map->qs=1;
				if (lang) {
					map->language=malloc(extn-lang);
					if (map->language==NULL) return NULL;
					memcpy(map->language,lang+1,extn-lang-1);
					map->language[extn-lang-1]='\0';
				}

				if (xmimemaptranslate_extension_to_mime_type(extn+1,buf2)) strcpy(buf2,"text/plain");
				len2=strlen(buf2);
				map->type=malloc(len2+1);
				if (map->type==NULL) return NULL;
				memcpy(map->type,buf2,len2+1);
			}
		}
	} while(more != -1);
	return maps;
}

static struct varmap *content_readmap(char *filename)
/*Reads a /var map file, or uses a cached copy*/
{
	FILE *varfile;
	char line[256];
	size_t len;
	fileswitch_object_type objtype;
	bits load,exec;
	char date[5];
	int valid=0,i;

	if (xosfile_read_stamped_no_path(filename, &objtype, &load, &exec, NULL,NULL, NULL) || objtype&1!=1)  return NULL;

	if (configuration.casesensitive) {
		if (xosfscontrol_canonicalise_path(filename,line,0,0,256,NULL)) return NULL;
		if (check_case(line) == 0) return NULL;
	}

	date[4] = load &255;
	date[3] = (exec>>24) &255;
	date[2] = (exec>>16) &255;
	date[1] = (exec>>8) &255;
	date[0] = exec &255;

	len=strlen(filename);
	for (i=0;i<MAP_CACHE_SIZE;i++) {
		if (mapcache[i].len==len) {
			char *s=mapcache[i].filename;
			char *t=filename;

			while (*s && tolower(*s)==tolower(*t)) s++,t++;
			if (*t=='\0') {
				/*filenames matched*/
				if (memcmp(date,mapcache[i].date,5)==0) {
					/*map file has not changed*/
					valid=1;
				}
				break;
			}
		}
	}

	if (valid) return mapcache[i].map; /*use cache entry*/

	/*Use existing entry if file has just been modified, otherwise choose an entry at random to replace*/
	if (i==MAP_CACHE_SIZE) i=rand() % MAP_CACHE_SIZE;

	/*free any existing entry*/
	if (mapcache[i].filename) {
		free(mapcache[i].filename);
		mapcache[i].filename=NULL;
		content_freemap(mapcache[i].map);
	}

	varfile=fopen(filename,"r");
	if (varfile==NULL) return NULL;

	mapcache[i].filename=malloc(len+1);
	if (mapcache[i].filename==NULL) return NULL;
	memcpy(mapcache[i].filename,filename,len+1);
	mapcache[i].len=len;
	memcpy(mapcache[i].date,date,5);
	mapcache[i].map=NULL;

	while (fgets(line,256,varfile)!=NULL) {
		char *category, *p;

		p=line;
		while (isspace(*p)) p++; /* skip initial whitespace */
		category=p;
		while (*p!='\0' && *p!=':') {
			*p=tolower(*p);
			p++;
		}
		if (*p) *p++='\0';
		while (isspace(*p)) p++; /* skip whitespace */
		if (strcmp(category,"uri")==0) {
			if (strchr(p,'.')) { /* ignore if the URI does not contain an extention */
				struct varmap *map;
				size_t len;
				char *q;

				map=malloc(sizeof(struct varmap));
				if (map==NULL) break;
				if (*p=='\0') continue;
				map->type=map->language=map->charset=map->encoding=map->description=NULL;
				map->qs=1;
				map->score=1;
				/* add to head of linked list*/
				map->next=mapcache[i].map;
				mapcache[i].map=map;
				/* copy uri */
				len=strlen(p)+1;
				q=map->uri=malloc(len);
				if (map->uri==NULL) break;
				while (*p!='\0' && *p!='\n') {
					if (*p=='.') *q='/'; else *q=*p;
					q++;
					p++;
				}
				*q='\0';
			}
		} else if (strcmp(category,"content-type")==0) {
			char *d;

			if (mapcache[i].map==NULL) break;
			if (*p=='\0') continue;
			d=mapcache[i].map->type=malloc(strlen(p)+1);
			if (mapcache[i].map->type==NULL) return NULL;
			while (*p!='\0' && *p!='\n' && *p!=';') *d++=*p++; /* copy mime type */
			*d='\0';
			while (*p) {
				if (*p==';') {
					while (isspace(*++p));
					if (strncmp(p,"qs=",3)==0) {
						mapcache[i].map->qs=(float)atof(p+3);
					} else if (strncmp(p,"charset=",8)==0) {
						d=mapcache[i].map->charset=malloc(strlen(p)+1);
						if (mapcache[i].map->charset==NULL) return NULL;
						while (*p!='\0' && *p!='\n' && *p!=';') *d++=*p++; /* copy charset */
						*d='\0';
					}
				}
				while (*p && *p!=';') p++;
			}
		} else if (strcmp(category,"content-language")==0) {
			size_t len;

			if (mapcache[i].map==NULL) break;
			if (*p=='\0') continue;
			len=strlen(p)+1;
			mapcache[i].map->language=malloc(len);
			if (mapcache[i].map->language==NULL) return NULL;
			memcpy(mapcache[i].map->language,p,len);
			if (mapcache[i].map->language[len-2]=='\n') mapcache[i].map->language[len-2]='\0';
		} else if (strcmp(category,"content-encoding")==0) {
			size_t len;

			if (mapcache[i].map==NULL) break;
			if (*p=='\0') continue;
			len=strlen(p)+1;
			mapcache[i].map->encoding=malloc(len);
			if (mapcache[i].map->encoding==NULL) return NULL;
			memcpy(mapcache[i].map->encoding,p,len);
			if (mapcache[i].map->encoding[len-2]=='\n') mapcache[i].map->encoding[len-2]='\0';
		} else if (strcmp(category,"description")==0) {
			size_t len;

			if (mapcache[i].map==NULL) break;
			if (*p=='\0') continue;
			len=strlen(p)+1;
			mapcache[i].map->description=malloc(len);
			if (mapcache[i].map->description==NULL) return NULL;
			memcpy(mapcache[i].map->description,p,len);
			if (mapcache[i].map->description[len-2]=='\n') mapcache[i].map->description[len-2]='\0';
		}
	}
	fclose(varfile);
	return mapcache[i].map;
}

int content_negotiate(struct connection *conn)
/* Try to negotiate the content*/
/* returns non-zero if it failed to find any suitable content*/
{
	char *leafname;
	char filename[256];
	size_t len;
	struct varmap *map,*bestmap,*tempmap;
	float bestquality;

	leafname=strrchr(conn->filename,'.'); /* find start of leafname */
	if (leafname==NULL) return 0; /*There should always be at least one '/' */
	if (strchr(leafname,'/')!=NULL) return 0; /* leafname does have an extention, so we don't need to negotiate the content */
	strcpy(filename,conn->filename);
	strcat(filename,"/var"); /*should be configurable/use addhandler like apache */
	map=content_readmap(filename);

	if (map==NULL) {
		size_t len;
		/*Try multiviews if configured */
		filename[strlen(filename)-3]='\0';
		len=leafname-conn->filename;
		filename[len]='\0';
		map=content_multiviews(filename,filename+len+1);
	}

	if (map==NULL) return 0; /* will give a 404 not found report*/

	/*reset all scores*/
	tempmap=map;
	while (tempmap) {
		tempmap->score=1;
		tempmap=tempmap->next;
	}

	if (conn->accept) content_updatescores(map,conn->accept,field_accept);
	if (conn->acceptlanguage) content_updatescores(map,conn->acceptlanguage,field_language);
	if (conn->acceptencoding) content_updatescores(map,conn->acceptencoding,field_encoding);
	if (conn->acceptcharset) content_updatescores(map,conn->acceptcharset,field_charset);

	bestquality=0;
	bestmap=NULL;
	tempmap=map;
	while (tempmap) {
		if (tempmap->score>0 && tempmap->score>bestquality) {
			bestquality=tempmap->score;
			bestmap=tempmap;
		}
		tempmap=tempmap->next;
	}
	if (bestmap==NULL) {
		report_notacceptable(conn,map);
		return 1; /* give a no suitable content report */
	}

	len=(leafname-conn->filename);
	strcpy(conn->filename+len+1,bestmap->uri);

	return 0;
}
