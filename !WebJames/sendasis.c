#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "webjames.h"
#include "report.h"
#include "cache.h"
#include "sendasis.h"

void sendasis_start(struct connection *conn)
{
	if ((conn->method == METHOD_GET) || (conn->method == METHOD_PUT) || (conn->method == METHOD_DELETE)) {
		/* was there a valid If-Modified-Since field in the request?? */
		if (conn->if_modified_since.tm_year > 0) {
			if (compare_time(&conn->fileinfo.date, &conn->if_modified_since) < 0) {
				report_notmodified(conn);
				return;
			}
		}
		/* if everything OK until now, prepare to send file */
		conn->fileused = 0;
		if (conn->cache) {
			conn->filebuffer = conn->cache->buffer;
			conn->fileinfo.size = conn->cache->size;
			conn->flags.releasefilebuffer = 0;
			conn->file = NULL;
			conn->cache->accesses++;

		} else {
			FILE *handle;
			
			handle = fopen(conn->filename, "rb");
			if (!handle) {
				report_notfound(conn);
				return;
			}
			/* attempt to get a read-ahead buffer for the file */
			/* notice: things will still work if malloc fails */
			conn->filebuffer = malloc(configuration.readaheadbuffer*1024);
			conn->flags.releasefilebuffer = 1;
			conn->leftinbuffer = 0;
			/* set the fields in the structure, and that's it! */
			conn->file = handle;
		}
	}
}
