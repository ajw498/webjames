/*
	$Id: content.c,v 1.8 2001/09/01 12:22:27 AJW Exp $
	Content negotiation
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "oslib/osfscontrol.h"
#include "oslib/osgbpb.h"
#include "oslib/mimemap.h"

#include "webjames.h"
#include "write.h"
#include "report.h"
#include "content.h"

#define MAP_CACHE_SIZE 10

#ifdef CONTENT

/*create a cache of recently accessed varmap files, to prevent rereading and parsing them unnessecerily*/
static struct {
	struct varmap *map;
	char *filename;
	size_t len;
	char date[5];
} mapcache[MAP_CACHE_SIZE];

/*structure to hold the parsed values of an accept, accept_language, etc. header*/
struct accept {
	char type[256]; /*length checking? - MimeMap_Translate does not seem to give a length for the buffer needed*/
	float q;
	enum {
		normal,
		wild,
		semiwild
	} wild; /*is there a wildcard*/
	struct accept *next;
};

enum field {
	field_accept,
	field_language,
	field_charset,
	field_encoding
};

static char *varextn=NULL;
static size_t varextnlen=0;

void content_init(void)
/*reset the cache*/
{
	int i;
	for (i=0;i<MAP_CACHE_SIZE;i++) {
		mapcache[i].len=0;
		mapcache[i].filename=NULL;
		mapcache[i].map=NULL;
	}
}

static void content_freemap(struct varmap *map)
/*free all memory allocated to a varmap structure*/
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
/*free all memory allocated to an accept structure*/
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
/*parse an accept header line and store in a struct accept linked list*/
{
	struct accept *parsed=NULL,*parse;
	struct accept *wildlist=NULL, *semiwildlist=NULL, *normallist=NULL;
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
	/*This is to catch cases such as text/html, text/plain, * / *  where the wildcard would have equal*/
	/*priority, which was not intended*/
	/*Also, split into separate lists for each type (normal/semi/wild)*/
	parse=parsed;
	while (parse) {
		if (strcmp(parse->type,"*")==0 || strcmp(parse->type,"*/*")==0) {
			struct accept *tmp;

			parse->wild=wild;
			if (fudgequality) parse->q=(float)0.01;
			tmp=parse;
			parse=parse->next;
			tmp->next=wildlist;
			wildlist=tmp;
		} else if (strncmp(parse->type+strlen(parse->type)-2,"/*",2)==0) {
			struct accept *tmp;

			parse->wild=semiwild;
			if (fudgequality) parse->q=(float)0.02;
			tmp=parse;
			parse=parse->next;
			tmp->next=semiwildlist;
			semiwildlist=tmp;
		} else {
			struct accept *tmp;

			parse->wild=normal;
			tmp=parse;
			parse=parse->next;
			tmp->next=normallist;
			normallist=tmp;
		}
	}
	/*Now re-join the three lists into one, so all wildcards are at the end of the list*/
	parse=parsed=normallist;
	while (parse && parse->next) parse=parse->next;
	if (parse) parse->next=semiwildlist; else parsed=parse=semiwildlist;
	while (parse && parse->next) parse=parse->next;
	if (parse) parse->next=wildlist; else parsed=parse=wildlist;
	return parsed;
}

static int content_updatescores(struct varmap *map,char *line,enum field field)
/*Update the score of each varient based on the given accept header line*/
/*returns non zero if this field caused the selection to vary*/
{
	struct accept *acceptlist;
	int vary=0;

	acceptlist=content_parseaccept(line);

	while (map) {
		int match=0;
		struct accept *accept;

		accept=acceptlist;
		while (accept && !match) {
			switch (accept->wild) {
				case normal:
					if (field==field_language) {
						if (map->language) {
							size_t tlen=strlen(accept->type);
							size_t mlen=strlen(map->language);
							if (strncmp(map->language,accept->type,tlen)==0 || strncmp(map->language,accept->type,mlen)==0) {
								/*allow "en" to match "en" or "en-gb" and vice-versa*/
								if (tlen<mlen ? (map->language[tlen]=='\0' || map->language[tlen]=='-') : (accept->type[mlen]=='\0' || accept->type[mlen]=='-')) {
									match=1;
									map->score*=1 * (accept->q);
								}
							}
						} else {
							match=1;
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
								if (str==NULL) str="iso-8859-1";
								break;
							default:
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
			&& !(field==field_encoding && (map->encoding==NULL ||  strcmp(map->encoding,"identity")==0))
			&& !(field==field_charset && (map->charset==NULL || strcmp(map->charset,"ISO-8859-1")==0 || strcmp(map->charset,"iso-8859-1")==0))) {
				map->score=0;
				vary=1;
		}
		if (match) vary=1;
		/*identity encoding and ISO-8859-1 charset are always acceptable unless specifically mentioned as not acceptable*/
		map=map->next;
	}
	content_freeaccept(acceptlist);
	return vary;
}

static struct varmap *content_multiviews(char *dirname,char *leafname)
/* Creates a map structure based on directory contents */
{
	char buf[256];
	osgbpb_info_stamped_list *info=(osgbpb_info_stamped_list *)buf;
	int count;
	int more = 0;
	size_t len = strlen(leafname);
	struct varmap *maps=NULL;

	do {
		if (xosgbpb_dir_entries_info_stamped(dirname,info,1,more,256,NULL,&count,&more)) return NULL;
		if (count) {
			if (strncmp(info->info[0].name,leafname,len) == 0) {
				struct varmap *map;
				size_t len2;
				char buf2[256];
				char *extn,*lang;

				extn=strrchr(info->info[0].name,'/');
				if (extn==NULL) continue;
				lang=strchr(info->info[0].name,'/'); /*assume all files are name/lang/extn or name/extn */
				if (lang==extn) lang=NULL;

				map=malloc(sizeof(struct varmap));
				if (map==NULL) {
					content_freemap(maps);
					return NULL;
				}
				map->next=maps;
				maps=map;
				map->language=map->charset=map->encoding=map->description=NULL;
				len2=strlen(info->info[0].name);
				map->uri=malloc(len2+1);
				if (map->uri==NULL) {
					content_freemap(maps);
					return NULL;
				}
				memcpy(map->uri,info->info[0].name,len2+1);

				map->qs=1;
				if (lang) {
					map->language=malloc(extn-lang);
					if (map->language==NULL) {
						content_freemap(maps);
						return NULL;
					}
					memcpy(map->language,lang+1,extn-lang-1);
					map->language[extn-lang-1]='\0';
				}

				if (xmimemaptranslate_filetype_to_mime_type(info->info[0].file_type,buf2)) strcpy(buf2,"text/plain");
				len2=strlen(buf2);
				map->type=malloc(len2+1);
				if (map->type==NULL) {
					content_freemap(maps);
					return NULL;
				}
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
	os_date_and_time date;
	int valid=0,i;
	int filetype;

	filetype=get_file_info(filename, NULL, NULL, &date, NULL, 1);

	if (filetype<0 || filetype>=FILE_NO_MIMETYPE) return NULL;

	len=strlen(filename);
	for (i=0;i<MAP_CACHE_SIZE;i++) {
		if (mapcache[i].len==len && mapcache[i].filename) {
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
	if (mapcache[i].filename==NULL) {
		fclose(varfile);
		return NULL;
	}
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
			if (mapcache[i].map->type==NULL) break;
			while (*p!='\0' && *p!='\n' && *p!=';') *d++=*p++; /* copy mime type */
			*d='\0';
			while (*p) {
				if (*p==';') {
					while (isspace(*++p));
					if (strncmp(p,"qs=",3)==0) {
						mapcache[i].map->qs=(float)strtod(p+3,NULL);
					} else if (strncmp(p,"charset=",8)==0) {
						d=mapcache[i].map->charset=malloc(strlen(p)+1);
						if (mapcache[i].map->charset==NULL) break;
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
			if (mapcache[i].map->language==NULL) break;
			memcpy(mapcache[i].map->language,p,len);
			if (mapcache[i].map->language[len-2]=='\n') mapcache[i].map->language[len-2]='\0';
		} else if (strcmp(category,"content-encoding")==0) {
			size_t len;

			if (mapcache[i].map==NULL) break;
			if (*p=='\0') continue;
			len=strlen(p)+1;
			mapcache[i].map->encoding=malloc(len);
			if (mapcache[i].map->encoding==NULL) break;
			memcpy(mapcache[i].map->encoding,p,len);
			if (mapcache[i].map->encoding[len-2]=='\n') mapcache[i].map->encoding[len-2]='\0';
		} else if (strcmp(category,"description")==0) {
			size_t len;

			if (mapcache[i].map==NULL) break;
			if (*p=='\0') continue;
			len=strlen(p)+1;
			mapcache[i].map->description=malloc(len);
			if (mapcache[i].map->description==NULL) break;
			memcpy(mapcache[i].map->description,p,len);
			if (mapcache[i].map->description[len-2]=='\n') mapcache[i].map->description[len-2]='\0';
		}
	}
	fclose(varfile);
	return mapcache[i].map;
}


void content_recordextension(char *extn)
/*store the extension to use to find the var-map*/
{
	if (extn==NULL) return;
	if (extn[0]=='.') extn++; /*accept either .var or var*/
	if (extn[0]=='\0') return;
	varextnlen=strlen(extn);
	varextn=malloc(varextnlen+1);
	if (varextn==NULL) return;
	memcpy(varextn,extn,varextnlen+1);
}


void content_starthandler(struct connection *conn)
/*Handler for the type-map handler type*/
{
	report_notfound(conn);
}

int content_negotiate(struct connection *conn)
/* Try to negotiate the content*/
/* returns non-zero if it failed to find any suitable content*/
{
	char *leafname;
	char filename[256];
	size_t len;
	struct varmap *map=NULL,*bestmap,*tempmap;
	float bestquality;

	leafname=strrchr(conn->filename,'.'); /* find start of leafname */
	if (leafname==NULL) return 0; /*There should always be at least one '/' */
	if (strchr(leafname,'/')!=NULL) return 0; /* leafname does have an extention, so we don't need to negotiate the content */
	strcpy(filename,conn->filename);
	if (varextn) {
		strcat(filename,"/");
		strcat(filename,varextn);
		map=content_readmap(filename);
	}

	if (map==NULL && conn->flags.multiviews) {
		/*Try multiviews if configured */
		size_t len;

		filename[strlen(filename)-varextnlen]='\0';
		len=leafname-conn->filename;
		filename[len]='\0';
		map=content_multiviews(filename,filename+len+1);
	}

	if (map==NULL) return 0; /* no content to negotiate. will give a 404 not found report unless file actually exists*/

	/*reset all scores*/
	tempmap=map;
	while (tempmap) {
		tempmap->score=1;
		tempmap=tempmap->next;
	}

	/*Give each variant a score*/
	if (conn->accept)         if (content_updatescores(map,conn->accept,field_accept))           strcat(conn->vary," Accept,");
	if (conn->acceptlanguage) if (content_updatescores(map,conn->acceptlanguage,field_language)) strcat(conn->vary," Accept-Language,");
	if (conn->acceptencoding) if (content_updatescores(map,conn->acceptencoding,field_encoding)) strcat(conn->vary," Accept-Encoding,");
	if (conn->acceptcharset)  if (content_updatescores(map,conn->acceptcharset,field_charset))   strcat(conn->vary," Accept-Charset,");
	len=strlen(conn->vary);
	if (len==0) {
		strcpy(conn->vary," *");
	} else {
		if (conn->vary[len-1]==',') conn->vary[len-1]='\0';
	}

	/*find the variant with the best score*/
	bestquality=0;
	bestmap=NULL;
	tempmap=map;
	while (tempmap) {
		if (tempmap->score>0 && tempmap->score>=bestquality) {
			bestquality=tempmap->score;
			bestmap=tempmap;
		}
		tempmap=tempmap->next;
	}
	if (bestmap==NULL) {
		/*no acceptable content was found*/
		report_notacceptable(conn,map);
		return 1;
	}

	/*change the filename to point to the chosen variant*/
	len=(leafname-conn->filename);
	strcpy(conn->filename+len+1,bestmap->uri);

	return 0;
}

#endif
