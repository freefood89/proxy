/*
 * proxy.c - A simple proxy server
 *
 * by: Benjamin Shih (bshih1) & Rentaro Matsukata (rmatsuka)
 * ---------------------------------------------------------
 */

#define _GNU_SOURCE
#define PORT 80
#define TRUE 1
#define FLASE 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"
#define VERSION " HTTP/1.0\r\n"

/* FUNCTION PROTOTYPES */
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

void genheader(char *host, char *header); 
void genrequest(char *request, char *method, char *uri, char *version);
void parseURL(char* url, char* host, char* uri);
void parseHeaderType(char* header, char* type);
/* 
 * MAIN CODE AREA 
 */
int main(int argc, char **argv) 
{
  int listenfd, connfd, port, clientlen;
  struct sockaddr_in clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  port = atoi(argv[1]);

  listenfd = Open_listenfd(port);
  clientlen = sizeof(clientaddr);  
  connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
  while (1) {
    doit(connfd);

    Close(connfd);
  }
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
  /* variables required for chunking    
  int chunked = 0; 
  int chunksize, accumulator;
  */
  /*
  int is_static;
  struct stat sbuf;
  */
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  //char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio_c, rio_h;

  /* Ren's Local Vars */
  char host[MAXLINE], url[MAXLINE], request[MAXLINE], header[MAXLINE];
  /* char headertype[MAXLINE], option[MAXLINE]; */
  int hostfd;
  /* Ren's Local Vars END */


  /* Read request line and headers */
  Rio_readinitb(&rio_c, fd);

  /* loop while readline did not get EOF or error
   * getting EOF means client closed connection */
  
  while(Rio_readlineb(&rio_c, buf, MAXLINE)!=0){
    sscanf(buf, "%s %s %s", method, url, version);
    /* if (strcasecmp(method, "GET")) { 
      clienterror(fd, method, "501", "Not Implemented",   
		  "Tiny does not implement this method");
      return;
      }*/
    printf("STUFF FROM THE CLIENT:\n");
    printf("%s\n",buf);
    read_requesthdrs(&rio_c);

    /* Ren's code */
    parseURL(url, host, uri); /* parse url for hostname and uri */
    hostfd = Open_clientfd(host, PORT); /* connect to host as client */
    Rio_readinitb(&rio_h, hostfd); /* set up host file discriptor */

    /* generate and send request to host*/  
    genrequest(request, method, uri, version);
    genheader(host, header);
    strcat(request, header);
    printf("STUFF TO THE SERVER:\n%s",request); 
    Rio_writen(hostfd, request, strlen(request));

    /* stream information from server to client */
    printf("STUFF FROM THE SERVER:\n");

    /* go through server response+header */
    do{
      Rio_readlineb(&rio_h,header,MAXLINE);

      /* parse header for useful information 
      parseHeaderType(header,headertype);
      if(!strcmp(headertype,"Transfer-Encoding"))
	sscanf(header, "%*s %s",option);{
	if(!strcmp(option,"chunked")){
	    chunked=1;
	    printf("Chunked TE option detected\n");
	}
      }
      */
      printf("%s",header);
      Rio_writen(fd,header, MAXLINE);
    }while(strcmp(header,"\r\n"));
    printf("end of server response header\n");
    /*
    chunksize = 0;
    accumulator = 0;*/
    /* transfer info until EOF */
    while(Rio_readlineb(&rio_h, buf, MAXLINE)){
      /*  if((chunking==TRUE) && (chunksize<accumulator+strlen(buf))){
	
	  printf("%s",buf);*/
      Rio_writen(fd, buf, MAXLINE);
    }
    printf("\nstream ended\n");
    Close(hostfd); /* disconnect from host */
    printf("closed connection with host\n");
  }
  printf("%s\n",buf);
}

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
void read_requesthdrs(rio_t *rp) 
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}


/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) {  /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else {  /* Dynamic content */
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else 
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}  

/* genrequest - compiles a HTTP request */
void genrequest(char *request, char *method, char *uri, char *version){
  /* create request string */
  strcpy(request,method);
  if (uri[strlen(uri)-1] == '/')
    strcat(uri, "index.html");
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

/*/////////// Added by Ben on 12/2/11*/

/* Given a website, parses the url into its host and argument.

   Input: char* url (Contains the full URL.), char** host (Garbage.), char** uri (Garbage.)
   Output: char* url (Garbage.), char** host (Host name of the URL. ex: www.cmu.edu.), char** uri (Argument of the URL. Contains the remaining directory of the URL.)

   Requires string.h and stdio.h. strchr(s, c) finds the first occurence of char c i
   n string s.
*/
void parseURL(char* url, char* host, char* uri)
{
  int len = 0;
  int pos;
  int offset = 0; /* http:// has a length of 7. We should really be looking for the first space, but this is still guaranteed to work because it will be the first instance of http */
  char nohttp[MAXLINE];
  //  char method[MAXLINE];

  //sscanf(url, "%s %s %s", method, url, version);
  offset = strcspn(url, "http://") + 7;
  len = strlen(url);
  //printf("input -- url: %s\tmethod: %s\tversion: %s\n", url, method, version);

  /* Removes the http:// from an url. */
  strcpy(nohttp, url + offset);

  //printf("string nohttp: %s\n", nohttp);

  /* Searches for the url by looking for the first slash. */
  pos = strcspn(nohttp, "/");
  strncpy(host, nohttp, pos);
  strncpy(uri, nohttp + pos, len - pos);

  //  printf("output: url: %s\thost: %s\t uri: %s\n", url, host, uri);
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
