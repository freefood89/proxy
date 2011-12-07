/*
 * ----------------------------------------------------------
 * proxy.c - A simple proxy server that prespawns threads and
 *           deploys request handler proc_request upon client 
 *           request.
 *
 * Utilizes threading:
 * 
 * - spawns NTHREADS threads
 * - threads block on sbufremove (in thread) until client's
 *   connection request is processed and pushed into FIFO 
 *   queue
 * - mutex prevents multiple threads accessing same client
 *   request
 * 
 * Supports Caching:
 *
 * by: Benjamin Shih (bshih1) & Rentaro Matsukata (rmatsuka)
 * ----------------------------------------------------------
 */

#define _GNU_SOURCE
#define PORT 80
#define TRUE 1
#define FALSE 0
#define NUMLINES 100
#define MAXCACHE 1048576
#define MAXOBJ 102400

#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"

typedef struct cacheLine{
	pthread_mutex_t mutex;
	int size;
	int LRUstamp;
	char *data;
	char url[MAXLINE];
} cacheLine;

/* FUNCTION PROTOTYPES */
void lockCacheW();
void lockCacheR();
void unlockCache();
int  cacheCheck(char *url);
int  cacheVacancy();
int  leastRecentlyUsed();
void cacheLineFree(int cacheIndex);
void *thread(void *vargp);
void cacheAlloc(int cacheIndex, char *content, char *url, size_t size);
void procRequest(int connfd);
void genRequest(int connfd, rio_t *browserio, char *uri, char* host, char* filePath, int numPort);
void initCache();

cacheLine cache[NUMLINES];
int overallCacheSize = 0;
int occupiedCacheLines = 0;
pthread_rwlock_t cacheLock;
pthread_mutex_t openLock;
int timeStamp = 1;
int port;

int main(int argc, char *argv [])
{
	int *connfd, listenfd;
	struct sockaddr_in addr;
	unsigned int len;

	if (2 != argc){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	/* Create mutex. */
	if (0 != (pthread_rwlock_init(&cacheLock, NULL)) || 0 != (pthread_mutex_init(&openLock, NULL))){
		fprintf(stderr, "Error opening listenfd\n");
		exit(1);
	}

	port = atoi(argv[1]);
	Signal(SIGPIPE, SIG_IGN);
    initCache();
	pthread_t concurrThread;

	/* Opens the port provided on the command line. */
	if(0 > (listenfd = open_listenfd(port))){
		fprintf(stderr, "Error opening port with open_listenfd.\n");
		exit(1);
	}

	while(1){
		if (NULL == (connfd = malloc(sizeof(int)))){
			fprintf(stderr, "Error allocating memory for connfd.\n");
			continue;
		}

		len = sizeof(addr);

		if(0 > (*connfd = accept(listenfd, (SA *) &addr, &len))){
			fprintf(stderr, "Error accepting connfd.\n");
			continue;
		}

		if (0 != pthread_create(&concurrThread, NULL, thread, connfd)){
			fprintf(stderr, "Error creating multiple threads.\n");
			continue;
		}
	}
	exit(EXIT_SUCCESS);
}

/* Request a webpage from the server. */
void genRequest(int connfd, rio_t *browserio, char *uri, char* host, char* filePath, int numPort){
	char header[MAXLINE];
	char content[MAXLINE];
	char pageBuf[MAXLINE];
	char cacheBuf[MAXOBJ];
	char forward[MAXLINE];
	char req[MAXLINE];

	int cacheIndex;
	size_t size = 0;
	int bufSize;	
	rio_t read_response;

	pthread_mutex_lock(&openLock);
	int serverfd = open_clientfd(host, numPort);
	pthread_mutex_unlock(&openLock);

    /* Ignores the request if there is an error. */
	if (0 > serverfd){
		if (-1 != serverfd){
			fprintf(stderr, "DNS error\n");
		}
		else{
			fprintf(stderr, "Unix error\n");
		}
		return;
	}
    /* Read and parse HTTP request headers. */
	sprintf(req, "GET /%s HTTP/1.0\r\n", filePath);
	rio_writen(serverfd, req, strlen(req));
	sprintf(req, "Host: %s\r\n", host);
	rio_writen(serverfd, req, strlen(req));

	while (0 < (bufSize = rio_readlineb(browserio, forward, MAXLINE))){
		if (0 == strcmp("\r\n", forward)){
			break;
        }

		*header = *content = 0;
		sscanf(forward, "%[A-Za-z0-9-]: %s", header, content);


		if (0 == strcasecmp("Connection", header) || 0 == strcasecmp("Proxy-Connection", header)){
			sprintf(forward, "%s: %s\r\n", header, "close");
		}
		/* Parse the headers for host and keep alive. */
		if (0 != strcasecmp("Host", header) && 0 != strcasecmp("Keep-Alive", header)){
			rio_writen(serverfd, forward, strlen(forward));
		}
	}
	
	sprintf(req, "Connection: close\r\n");
	rio_writen(serverfd, req, strlen(req));
	
	/* Append carriage return. */
	sprintf(req, "\r\n");
	rio_writen(serverfd, req, strlen(req));
	rio_readinitb(&read_response, serverfd);

	/* Display web page. */
	while(0 < (bufSize = rio_readlineb(&read_response, pageBuf, MAXLINE))){
		rio_writen(connfd, pageBuf, bufSize);
		if(MAXOBJ >= (size + bufSize)){
			memcpy (cacheBuf + size, pageBuf, bufSize);
		}
		size += bufSize;
	} 

    /* Lock cache before writing to it, and unlock once the writing has been completed. */
	lockCacheW();
	if (MAXOBJ >= size){
		/* Evict the least recently used cache line so that we can insert the new data. */
		while (MAXCACHE < (overallCacheSize + size) || NUMLINES <= occupiedCacheLines){
			cacheIndex = leastRecentlyUsed();
			cacheLineFree(cacheIndex);
		}

		cacheIndex = cacheVacancy();
		if (0 <= cacheIndex){
			cacheAlloc(cacheIndex, cacheBuf, uri, size);
		}
	}
	unlockCache();
	close(serverfd);
}


/* If there are no vacant lines, returns -1. Otherwise, returns the index of the vacant line. */
int cacheVacancy(){
	int i;
	for(i = 0; i < NUMLINES; i++){
		if(0 == cache[i].LRUstamp){
			return i;
		}
	}
	return -1;
}

/* Checks the cache for the url. */
int cacheCheck(char* url){
    int i;
	for(i = 0; i < NUMLINES; i++){
		if(0 < cache[i].LRUstamp && 0 == strcmp(url, cache[i].url)){
			return i;
		}
	}
	return -1;
}

/* Find the LRU cached object. */
int leastRecentlyUsed(){
	int i;
	int leastRecent = -1;
	for(i= 0; i < NUMLINES; i++){
		if(0 < cache[i].LRUstamp && (0 > leastRecent ||  cache[leastRecent].LRUstamp > cache[i].LRUstamp)){
			leastRecent = i;
        }
	}
	return leastRecent;
}

/* Lock cache for writing. */
void lockCacheW(){
    if(pthread_rwlock_wrlock(&cacheLock)){
        fprintf(stderr, "Error during cache write lock.\n");
        exit(-1);
    }
}

/* Lock cache for reading. */
void lockCacheR(){
    if(pthread_rwlock_rdlock(&cacheLock)){
        fprintf(stderr, "Error during cache read lock.\n");
        exit(-1);
    }
}

/* Unlocks the cache if it has been locked for reading or writing. */
void unlockCache(){
    if(pthread_rwlock_unlock(&cacheLock)){
        fprintf(stderr, "Error during cache unlock.\n");
        exit(-1);
    }
}

/* Frees all memory associated with the specified cache line and updates the properties of the cache. */
void cacheLineFree(int index){
	free(cache[index].data);
	cache[index].data = NULL;

	occupiedCacheLines--;
	overallCacheSize -= cache[index].size;
	cache[index].LRUstamp = 0;
}

/* Initializes the cache. */
void initCache(){
    int i;
	for(i = 0; i < NUMLINES; i++){
    	cache[i].LRUstamp = 0;
		pthread_mutex_init(&cache[i].mutex, NULL);
	}
}

 /* Processes requests that use GET. */
void procRequest(int connfd){
	int numPort = 0;
	int cacheIndex;
	size_t n;
	char scheme[MAXLINE];
	char req[MAXLINE];
	char uri[MAXLINE];
	char method[MAXLINE];
	char host[MAXLINE];
	char filePath[MAXLINE];
	char version[MAXLINE];

	rio_t browserio;
	rio_readinitb(&browserio, connfd);
	n = rio_readlineb(&browserio, req, MAXLINE);

	filePath[0] = '\0';
	host[0] = '\0';

	if (0 < n && 0 != strcmp("\r\n", req)){
		printf("%s", req);

		/* Parse the method, uri, and version into their own separate variables. */
		if (3 != sscanf(req, "%s %s %s", method, uri, version)){
			fprintf(stderr, "Error parsing GET instruction.\n");
			return;
		}
		
		/* Subsequently, parse uri into host, path, and port number. */
		if (0 == strcasecmp("GET", method)){
			if (1 >= sscanf(uri, "%[A-Za-z0-9+.-]://%[^/]%s", scheme, host, filePath)){
				return;
			}

			if (strcasecmp("http", scheme)){
				fprintf(stderr, "Error using specified transfer protocol.\n");
				return;
			}

			/* Delete the / from the file path. */
			if ('/' == filePath[0]){
				memmove(filePath, 1 + filePath, strlen(filePath));
			}

			char *p = strchr(host, ':');
			if (NULL == p){
				numPort = PORT;
			}
			else{
				sscanf(1 + p, "%d", &numPort);
				*p = 0;
            }

			/* Read the cache. Lock while it's in use, and unlock when finished. */
			lockCacheR();
			cacheIndex = cacheCheck(uri);
			unlockCache();

			if(0 <= cacheIndex){
				rio_writen(connfd, cache[cacheIndex].data, cache[cacheIndex].size);
				pthread_mutex_lock(&cache[cacheIndex].mutex);
				cache[cacheIndex].LRUstamp = timeStamp++;
				pthread_mutex_unlock(&cache[cacheIndex].mutex);
				printf("Cache hit. Reading from cache.\n");
			} 
            else{
				if (0 != numPort && NULL != filePath && NULL != host){
					genRequest(connfd, &browserio, uri, host, filePath, numPort);
					printf("Cache miss. Reading from server.\n");
				} 
				else{
					fprintf(stderr, "Error during url parsing.\n");
				}	
			}
		}
		printf("Request successfully processed.\n");
	}
}

/* Spawn threads. */
void *thread(void *vargp){
	int connfd = *((int *)vargp);
    
	Pthread_detach(pthread_self());
    procRequest(connfd);
    printf("Closing connection.\n\n");
    close(connfd);
    free(vargp);
    
	return NULL;
}

/* Dynamically allocating memory for a cache line that will hold the given web page. */
void cacheAlloc(int cacheIndex, char *content, char *url, size_t size){
	strcpy(cache[cacheIndex].url, url);
	cache[cacheIndex].data = malloc(size);
	if(!cache[cacheIndex].data){
		printf("Error allocating memory for the data. Malloc unsuccessful.\n");
		return;
	}

	memcpy(cache[cacheIndex].data, content, size);
	
	cache[cacheIndex].LRUstamp = timeStamp;
	cache[cacheIndex].size = size;

    occupiedCacheLines++;
	timeStamp++;
	overallCacheSize += size;
}
