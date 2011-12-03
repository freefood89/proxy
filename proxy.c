/*
 * proxy.c - A simple proxy server
 *
 * by: Benjamin Shih (bshih1) & Rentaro Matsukata (rmatsuka)
 * ---------------------------------------------------------
 *
 * CHANGE LOG:
 *
 * Dec.03 (00.25) - checked out from git server
 * Nov.30 (19:00) - copy pasted in tiny.c 
 */

#define _GNU_SOURCE
#define PORT 57020

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
void genrequest(char *request, char *method, char *uri);
void parseURL(char* url, char* host, char* uri, char* version);
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
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    doit(connfd);

    Close(connfd);
  }
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio_c, rio_h;

  /* Ren's Local Vars */
  char host[MAXLINE], url[MAXLINE], request[MAXLINE], header[MAXLINE];
  int hostfd;
  /* Ren's Local Vars END */


  /* Read request line and headers */
  Rio_readinitb(&rio_c, fd);
  Rio_readlineb(&rio_c, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) { 
    clienterror(fd, method, "501", "Not Implemented",   
                "Tiny does not implement this method");
    return;
  }
  printf("STUFF FROM THE CLIENT:\n");
  printf("%s\n",buf);
  read_requesthdrs(&rio_c);

  /* Ren's code */
  parseURL(buf, host, uri, version); /* parse url for hostname and uri */
  hostfd = Open_clientfd(host, PORT); /* connect to host as client */
  Rio_readinitb(&rio_h, hostfd); /* set up host file discriptor */

  /* generate and send request to host*/  
  genrequest(request, method, uri);
  genheader(host, header);
  strcat(request, header);
  printf("%s\n",request); 
  Rio_writen(hostfd, request, strlen(request));

  /* stream information from server to client */
  printf("STUFF FROM THE SERVER:\n");
  while(Rio_readlineb(&rio_h, buf, MAXLINE)){
    printf("%s\n",buf);
    Rio_writen(fd, buf, MAXLINE);
  }

  printf("stream ended\n");
  /* Ren's code */
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
 * serve_static - copy a file back to the client 
 */

void serve_static(int fd, char *filename, int filesize) 
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
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

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* child */
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1); 
    Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* genrequest - compiles a HTTP request */
void genrequest(char *request, char *method, char *uri){
  /* create request string */
  strcpy(request,"GET");
  if (uri[strlen(uri)-1] == '/')
    strcat(uri, "index.html");
  strcat(request," ");
  strcat(request, uri);
  strcat(request, VERSION);
  //printf("%s",request);
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
void parseURL(char* url, char* host, char* uri, char* version)
{
  int len = 0;
  int pos;
  int offset = 0; /* http:// has a length of 7. We should really be looking for the first space, but this is still guaranteed to work because it will be the first instance of http */
  char nohttp[MAXLINE];
  char method[MAXLINE];

  sscanf(url, "%s %s %s", method, url, version);
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

  printf("output: url: %s\thost: %s\t uri: %s\n", url, host, uri);
}

