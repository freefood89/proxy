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
 *
 *
 * by: Benjamin Shih (bshih1) & Rentaro Matsukata (rmatsuka)
 * ----------------------------------------------------------
 */

#define _GNU_SOURCE
//#define DEBUG
#define PORT 80
#define TRUE 1
#define FALSE 0

#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"
#include "sbuf.h"

/* Shared buffer size and number of threads. */
#define NTHREADS 16
#define SBUFSIZE 16
#define MAX_VERSION 8

/* FUNCTION PROTOTYPES */
void read_requesthdrs(rio_t *rp, int hostfd);
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg);

int isURL(char *buf);

void proc_request(void *arg);
void genheader(char *host, char *header); 
void genrequest(char *request, char *method, char *uri, char *version); 
void getHost(char *url, char *host);
void getURI(char *url, char *uri);
void parseHeaderType(char* header, char* type);
int clientconnected(rio_t *rio_c);

/* Function Prototypes for Multiple Requests */
void echo_cnt(int connfd);
void* thread(void *vargp);

/* Shared buffer for all of the connected descriptors */
sbuf_t sbuf;

/* 
 * MAIN CODE AREA 
 */
int main(int argc, char **argv) 
{
    /* Variables for Multiple Requests */
    int i,  connfd;
    socklen_t  sockclientlen = sizeof(struct sockaddr_in);
    pthread_t tid;

    /* END Variables for Multiple Requests */

    int port, clientlen, listenfd;
    //int *clientfd;
    struct sockaddr_in clientaddr;
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);


    port = atoi(argv[1]);

    listenfd = Open_listenfd(port); 
    clientlen = sizeof(clientaddr);

    /* Code for Multiple Requests inside main method. */
    sbuf_init(&sbuf, SBUFSIZE);
    /* Create the worker threads. */
    for(i = 0; i < NTHREADS; i++)
    {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while(1)
    {
        connfd = Accept(listenfd, (SA *) &clientaddr, &sockclientlen);
        //        printf("client queued.\n");
        /* Insert connfd into the buffer. */
        sbuf_insert(&sbuf, connfd);
    }

    /* END Code for Multiple Requests inside main method. */
    return 0;
}

void proc_request(void *arg){
    int hostfd, clientfd, len;
    char buf[MAXLINE], url[MAXLINE], host[MAXLINE], uri[MAXLINE];
    char method[MAXLINE], version[MAX_VERSION];
    rio_t rio_c, rio_h;

    clientfd = *(int *)arg;
    free(arg);

    printf("new connection fd: %d\n", clientfd);
    //receive request
    Rio_readinitb(&rio_c, clientfd);
    Rio_readlineb(&rio_c, buf, MAXLINE);
    dbg_printf("%sRECEIVED REQUEST\n",buf);
    sscanf(buf, "%s %s %s", method, url, version);
    
    //setup host
    getHost(url, host);
    if((hostfd = open_clientfd(host, PORT)) < 0){
	    /* if error: thread will stay alive and wait 
	       for a connection request by a client*/
	    fprintf(stderr, "ERROR: Could not Connect to Host\n");
	    Close(clientfd);
	    return; 
    }
    Rio_readinitb(&rio_h, hostfd);
    dbg_printf("CONNECTED TO HOST\n");
    getURI(url,uri);

    //relay request to host
    genrequest(buf, method, uri, version);
    rio_writen(hostfd, buf, strlen(buf));
    dbg_printf("REQUEST SENT\n");

    //loop through headers and relay them to host
    read_requesthdrs(&rio_c, hostfd);
    dbg_printf("HOST RESPONDING\n");

    //parse response and header
    do{
        Rio_readlineb(&rio_h,buf,MAXLINE);
        dbg_printf("%s", buf);
        rio_writen(clientfd,buf, strlen(buf));
    }while(strcmp(buf,"\r\n"));
    
    //loop data
    while((len = rio_readnb(&rio_h,buf,MAXLINE))>0){
        dbg_printf("READ: %s", buf);
        rio_writen(clientfd, buf, len);
    }
    rio_writen(clientfd, buf, len);         
    //printf("client: %d\n",clientconnected(&rio_c));
    Close(clientfd);
    Close(hostfd);
}

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
void read_requesthdrs(rio_t *rp, int hostfd) 
{
    char buf[MAXLINE];
    char type[MAXLINE];
    char option[MAXLINE];

    /* loop until the buf is just \r\n */
    do{ 
	    Rio_readlineb(rp, buf, MAXLINE);

        parseHeaderType(buf,type);
        if(!strcmp(type,"Proxy-Connection") || !strcmp(type,"Connection")){
            sscanf(buf, "%*s %s",option);
            /* forward connection option to server */
            bzero(buf, MAXLINE);            
            strcpy(buf, type);
            strcat(buf, ": close\r\n");
            rio_writen(hostfd, buf, strlen(buf));
            dbg_printf("%s", buf);
        }
        else{ /* just send it */
            rio_writen(hostfd, buf, strlen(buf));
            dbg_printf("%s", buf);
        }
    }while(strcmp(buf, "\r\n"));
    return;
}


/* genrequest - compiles a HTTP request */
void genrequest(char *request, char *method, char *uri, char *version){
    /* create request string */
    //bzero(request, MAXLINE);
    strcpy(request,method);
    strcat(request," ");
    strcat(request, uri);
    strcat(request," ");
    strcat(request, version);
    strcat(request,"\r\n");
}

/* genheader - generates a HTTP request header */
void genheader(char *host,char *header){
    strcpy(header,"Host: ");
    strcat(header,host);
    strcat(header,"\r\n\r\n");
}


/* parseURL - parses the url for hostname and uri
 * Input: char* url (Contains URL)
 * Output: char* host (Host name ex: www.cmu.edu.), 
 *         char* uri (URI. Contains the directory/file name) 
 */
void getURI(char* url, char* uri)
{
    int len = 0;
    int pos;
    int offset = 0; 
    char nohttp[MAXLINE];

    /* len(http://) = 7 */
    offset = strcspn(url, "http://") + 7;
    len = strlen(url);

    /* Removes the http:// from an url. */
    strcpy(nohttp, url + offset);
    /* Searches for the uri by looking for the first slash. */
    pos = strcspn(nohttp, "/");
    strncpy(uri, nohttp + pos, len - pos);

    dbg_printf("EXTRACTED: uri: %s\n", uri);
}

int isURL(char *buf){   
    if(!buf)
        return -1;
    if(strlen(buf)==strcspn(buf, "http://"))
        return 0;
    return 1;
}

void getHost(char *url, char *host){
    int pos;
    int offset = 0; 
    char nohttp[MAXLINE];

    /* len(http://) = 7 */
    offset = strcspn(url, "http://") + 7;

    /* Removes the http:// from an url. */
    strcpy(nohttp, url + offset);
    /* Searches for the uri by looking for the first slash. */
    pos = strcspn(nohttp, "/");
    bzero(host,MAXLINE);
    strncpy(host, nohttp, pos);
    dbg_printf("EXTRACTED: host: %s\n", host);
}

/* parseHeaderType - given a header line, this will 
 * insert what the header type is into type
 * type must be pre-allocated */
void parseHeaderType(char* header, char* type){
    int pos = 0;

    /* header information type always terminated by ':' */
    pos = strcspn(header, ":");
    /* cpy the portion of header upto the ':' */
    bzero(type,MAXLINE);
    strncpy(type, header, pos); 
}

void* thread(void* vargp)
{
    int* clientfd;
    Pthread_detach(pthread_self());
    while(1){
        clientfd = Malloc(sizeof(int));
        /* Remove connfd from the buffer. */
        *clientfd = sbuf_remove(&sbuf);
        //        printf("client unqueued.\n");
        /* Service client. */
        proc_request((void *)clientfd);
        //Close((void *)clientfd);
        //free(clientfd);
        printf("client disconnected\n");
    }
}

int clientconnected(rio_t *rio_c){
	int len;
	char buf[MAXLINE];
	if((len = rio_readnb(rio_c,buf,MAXLINE))==0){
		//dbg_printf("client end closed socket\n");
        return 0;
	}
	else{
	    //dbg_printf("client still connected\n");*/
		return 1;
	}
}
