#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "oslib/os.h"
#include "oslib/wimp.h"
#include "oslib/fileswitch.h"
#include "oslib/osfile.h"
#include "oslib/osfscontrol.h"

#include "webjames.h"
#include "cache.h"
#include "write.h"
#include "cacheheap.h"
#include "stat.h"


static os_dynamic_area_no cachedynamicarea = -1;
static byte *cachestart;
static struct cache *cachedfiles[MAXCACHEFILES];


struct cache *get_file_through_cache(struct connection *conn)
/* return pointer to cache structure or NULL */
{
	int i, namelen, checksum, freesize, use;
	char *name;

	if (!cachestart)  return NULL;

	/* calculate namelen and checksum - these are used to speed up the */
	/* cache-scan: names are only compared with strcmp() if namelen and */
	/* checksum are the same */
	namelen = 0;
	checksum = 0;
	name = conn->filename;
	while (name[namelen]) {
		checksum += (name[namelen])<<(namelen &15);
		namelen++;
	}

	if (conn->fileinfo.size > configuration.maxcachefilesize)  return NULL;  /* too big */
	if (conn->fileinfo.filetype == -1)  return NULL;     /* cache only files with filetypes/datestamps */

	/* scan the cache */
	for (i = 0; i < MAXCACHEFILES; i++) {
		if (!cachedfiles[i])  continue;
		if ( (cachedfiles[i]->checksum != checksum) || (cachedfiles[i]->namelen != namelen) )  continue;
		if (strcmp(name, cachedfiles[i]->name)) continue;
		if (compare_time(&cachedfiles[i]->date, &conn->fileinfo.date) < 0) {
#ifdef LOG
			sprintf(temp, "UN-CACHING %s", cachedfiles[i]->name);
			writelog(LOGLEVEL_CACHE, temp);
#endif
			cachedfiles[i]->removewhenidle = 1;         /* flush when possible */
			i = MAXCACHEFILES;                          /* abort */

		} else {
			cachedfiles[i]->timeoflastaccess = clock()/100;
			return cachedfiles[i];
		}
	}

	/* get size of largest block that can be allocated from the cache */
	freesize = heap_largest();
	if (!freesize)  return NULL;

	if (freesize < conn->fileinfo.size) {
		/* remove the oldest file that is large enough to make a difference */
		int lastaccess;

		use = -1;
		lastaccess = clock()/100;
		for (i = 0; i < MAXCACHEFILES; i++) {
			if (!cachedfiles[i])  continue;
			if ((freesize + cachedfiles[i]->size > conn->fileinfo.size)    &&
				(cachedfiles[i]->timeoflastaccess < lastaccess) &&
				(cachedfiles[i]->accesses == 0) )  {
				lastaccess = cachedfiles[i]->timeoflastaccess;
				use = i;
			}
		}
		if (use == -1)  return NULL;        /* no suitable entry found */

#ifdef LOG
		sprintf(temp, "UN-CACHING %s", cachedfiles[use]->name);
		writelog(LOGLEVEL_CACHE, temp);
#endif
		remove_from_cache(use);

		freesize = heap_largest();
		if (freesize < conn->fileinfo.size)  return NULL;   /* still not enough room */

	} else {

		/* find an empty cache-slot */
		for (use = 0; use < MAXCACHEFILES; use++)
			if (!cachedfiles[use])  break;
		if (use == MAXCACHEFILES)  return NULL;
	}

	cachedfiles[use] = heap_allocate(sizeof(struct cache));
	if (!cachedfiles[use])  return NULL;

	cachedfiles[use]->buffer = heap_allocate(conn->fileinfo.size);
	if (!cachedfiles[use]->buffer) {
		remove_from_cache(use);
		return NULL;
	}

	if (xosfile_load_stamped_no_path(conn->filename, (byte *)cachedfiles[use]->buffer, NULL, NULL, NULL, NULL, NULL)) {
		remove_from_cache(use);
		return NULL;
	}

	cachedfiles[use]->filetype = conn->fileinfo.filetype;

	strcpy(cachedfiles[use]->mimetype, conn->fileinfo.mimetype);

	memcpy(&cachedfiles[use]->date, &conn->fileinfo.date, sizeof(struct tm));

	cachedfiles[use]->size = conn->fileinfo.size;
	cachedfiles[use]->timeoflastaccess = clock()/100;
	cachedfiles[use]->accesses = cachedfiles[use]->totalaccesses = 0;
	cachedfiles[use]->namelen = namelen;
	cachedfiles[use]->checksum = checksum;
	cachedfiles[use]->removewhenidle = 0;
	strcpy(cachedfiles[use]->name, name);

#ifdef LOG
	sprintf(temp, "CACHING %s", name);
	writelog(LOGLEVEL_CACHE, temp);
#endif

	return cachedfiles[use];
}


void flushcache(char *name) {
/* if name == NULL, clear cache else clear just that named object */

	int i;

	if (name) {
		for (i = 0; i < MAXCACHEFILES; i++) {
			if (!cachedfiles[i])  continue;
			if (strcmp(name, cachedfiles[i]->name))
				cachedfiles[i]->removewhenidle = 1;
		}
	} else {
		for (i = 0; i < MAXCACHEFILES; i++)
			if (cachedfiles[i])  cachedfiles[i]->removewhenidle = 1;
	}
}



void kill_cache() {

	if (cachedynamicarea == -1)  return;
	xosdynamicarea_delete(cachedynamicarea);
	cachedynamicarea = -1;
}



void init_cache(char *list) {
/* get memory for cache; pre-load the list of files */

	int i, limit, next, curr, freeslot;

	list = list;

	cachestart = NULL;
	if (configuration.cachesize < 10)  return;

	configuration.cachesize *= 1024;
	configuration.maxcachefilesize *= 1024;
	if (configuration.maxcachefilesize > configuration.cachesize/2)  configuration.maxcachefilesize = configuration.cachesize/2;
	for (i = 0; i < MAXCACHEFILES; i++)  cachedfiles[i] = NULL;

	if (xosdynamicarea_create((os_dynamic_area_no)-1, configuration.cachesize, (byte *)-1, 128, configuration.cachesize, 0, 0, "WebJames cache", &cachedynamicarea, &cachestart, &limit)) {
		if (xwimp_slot_size(-1, -1, &curr, &next, &freeslot))  return;
		curr += configuration.cachesize;
		if (xwimp_slot_size(curr, -1, &curr, &next, &freeslot))  return;
		cachestart = malloc(configuration.cachesize);
	} else {
#ifdef MemCheck_MEMCHECK
		MemCheck_RegisterMiscBlock(cachestart,configuration.cachesize);
#endif
	}
	if (!cachestart)  return;

	memset(cachestart, 0, configuration.cachesize);
	heap_initialise(cachestart, configuration.cachesize);

/*
	if (list) {
		FILE *file;
		struct cache *entry;
		char filename[256], name[256];
		file = fopen(list);
		while (!feof(file)) {
			if (fscanf(file, "%s %s", filename, name) == 2) {
				entry = get_file_through_cache(name, filename);
				if (entry)  cache_release_file(entry);
			}
		}
		fclose(file);
	} */
}


void cache_release_file(struct cache *entry) {

	int clk, i;

	clk = clock()/100;

	entry->accesses--;
	entry->totalaccesses++;
	entry->timeoflastaccess = clk;
	if ((entry->accesses > 0) || (!entry->removewhenidle))  return;

	for (i = 0; i < MAXCACHEFILES; i++)
		if (cachedfiles[i] == entry)  remove_from_cache(i);
}



void remove_from_cache(int i) {

	if (!cachedfiles[i])  return;
	if (cachedfiles[i]->buffer)  heap_release(cachedfiles[i]->buffer);
	heap_release(cachedfiles[i]);
	cachedfiles[i] = NULL;
}
