/*
 * ----------------------------------------------------------
 * proxy.c - A simple proxy server
 *
 * by: Benjamin Shih (bshih1) & Rentaro Matsukata (rmatsuka)
 * ----------------------------------------------------------
 */

#define _GNU_SOURCE
#define DEBUG
#define PORT 53392
#define TRUE 1
#define FLASE 0

#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"
#define MAX_VERSION 8

/* FUNCTION PROTOTYPES */
void doit(int fd);
void read_requesthdrs(rio_t *rp, int hostfd, int *persistence);
void clienterror(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg);

void genheader(char *host, char *header); 
void genrequest(char *request, char *method, char *uri, char *version); 
void getHost(char *url, char *host);
void getURI(char *url, char *uri);
void parseHeaderType(char* header, char* type);
/* 
 * MAIN CODE AREA 
 */
int main(int argc, char **argv) 
{
	int listenfd, hostfd, port, clientlen;
	int *clientfd;
	int len;
	struct sockaddr_in clientaddr;

	/* variables that may possibly move */
	int persistence=0;
	char buf[MAXLINE], url[MAXLINE], host[MAXLINE], uri[MAXLINE];
	char method[MAXLINE], version[MAX_VERSION];
	rio_t rio_c, rio_h;
	/* end vars */


	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	port = atoi(argv[1]);
	
	//while(1){
	listenfd = Open_listenfd(port);
	clientlen = sizeof(clientaddr); 
	while(1){
		do{ 
			printf("persistence: %d\n",persistence);
		  
			clientfd = Malloc(sizeof(int));
			*clientfd = Accept(listenfd,(SA *)&clientaddr,(socklen_t *)&clientlen);
			printf("new connection fd: %d\n", *(int *)clientfd);
		
			Rio_readinitb(&rio_c, *clientfd);
			//receive request
			bzero(buf,MAXLINE);
			Rio_readlineb(&rio_c, buf, MAXLINE);
			printf("RECEIVED REQUEST\n");
			bzero(method, MAXLINE);
			bzero(url, MAXLINE);
			bzero(version, MAX_VERSION);
					
			if(!persistence){
				//setup host
				bzero(host,MAXLINE);
				sscanf(buf, "%s %s %s", method, url, version);
				
				getHost(url, host);
				hostfd = Open_clientfd(host, PORT);
				Rio_readinitb(&rio_h, hostfd);
				dbg_printf("CONNECTED TO HOST\n");
				
				getURI(url, uri);
				//parseURL(url, host, uri); 
			}			
			else //host is already connected
				sscanf(buf, "%s %s %s", method, uri, version);
			
			//transmit request
			bzero(buf, MAXLINE);
			genrequest(buf, method, uri, version);
			Rio_writen(hostfd, buf, strlen(buf));
			dbg_printf("REQUEST SENT\n");
			//loop headers
			read_requesthdrs(&rio_c, hostfd, &persistence);
			dbg_printf("CLIENT TRANSMISSION TERMINATED\n");
			
			//parse response and header
			do{
				bzero(buf, MAXLINE);
				Rio_readlineb(&rio_h,buf,MAXLINE);
				dbg_printf("%s", buf);
				Rio_writen(*((int *)clientfd),buf, MAXLINE);
			}while(strcmp(buf,"\r\n"));
			dbg_printf("HOST HEADER TERMINATED\n");
			//loop data
			bzero(buf,MAXLINE);
			while((len = rio_readnb(&rio_h,buf,MAXLINE))>0){
				dbg_printf("READ: %s", buf);
				Rio_writen(*((int *)clientfd), buf, strlen(buf));
			}
			Rio_writen(*((int *)clientfd), buf, strlen(buf));			
			if(rio_readlineb(&rio_c,buf,strlen(buf) == 0)){
				printf("client has closed connection\n");
				Close(*(int *)clientfd);
				free(clientfd);
			}
		}while(persistence==1);
		Close(hostfd);
		//	Close(*clientfd);
	}
}

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
void read_requesthdrs(rio_t *rp, int hostfd, int *persistence) 
{
	char buf[MAXLINE];
	char type[MAXLINE];
	char option[MAXLINE];

	/* loop until the buf is just \r\n */
	do{	
		bzero(buf, MAXLINE);
		Rio_readlineb(rp, buf, MAXLINE);
		//printf("%s",buf);

		bzero(type, MAXLINE);
		bzero(option, MAXLINE);
		parseHeaderType(buf,type);
		if(!strcmp(type,"Proxy-Connection")){
			sscanf(buf, "%*s %s",option);
			if(!strcmp(option,"keep-alive")){
				*persistence=1;
				dbg_printf("keep-alive connection option detected\n");
			}
			/* forward connection option to server */
			bzero(buf, MAXLINE);			
			strcpy(buf, "Connection: ");
			strcat(buf, option);
			strcat(buf, "\r\n");
			Rio_writen(hostfd, buf, strlen(buf));
			printf("%s", buf);
		}
		else if(!strcmp(type,"Cookie"))
			;//	printf("NOTSENT: ");
		else if(!strcmp(type, "User-Agent"))
			;// printf("NOTSENT: ");
		else{ /* just send it */
			Rio_writen(hostfd, buf, strlen(buf));
			printf("%s", buf);
		}
	}while(strcmp(buf, "\r\n"));
	return;
}


/* genrequest - compiles a HTTP request */
void genrequest(char *request, char *method, char *uri, char *version){
	/* create request string */
	bzero(request, MAXLINE);
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

	printf("EXTRACTED: uri: %s\n", uri);
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
	strncpy(host, nohttp, pos);

	printf("EXTRACTED: host: %s\n", host);
}

/* parseHeaderType - given a header line, this will 
 * insert what the header type is into type
 * type must be pre-allocated */
void parseHeaderType(char* header, char* type){
	int pos = 0;

	/* header information type always terminated by ':' */
	pos = strcspn(header, ":");
	/* cpy the portion of header upto the ':' */
	strncpy(type, header, pos); 
}

