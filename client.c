#include "csapp.h" 

#define VERSION " HTTP/1.0\r\n"

char *allcaps(char *str);
void genheader(char *host, char *header);
void genrequest(char *request, char *method, char *uri);

int main(int argc, char **argv)
{
    int clientfd, port;
    char *host;
    char buffer[1024];

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], requesthdr[MAXLINE];
    char version[MAXLINE];
    //int bufsize;
    rio_t rio;

    FILE *file;

    if(argc!=3){
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    host=argv[1];
    port=atoi(argv[2]);

    /* generate the request header for this host */
    genheader(host,requesthdr);

    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    while(Fgets(buf, MAXLINE, stdin)!=NULL){
        sscanf(buf ,"%s %s %s", method, uri, version);
        if(!strcasecmp(method, "GET")){
            genrequest(buf,"GET",uri); /* generate request into buf */
            strcat(buf, requesthdr); /* concatenate header to buf */	

            file = Fopen(uri+1,"w");  

            /* send request+header to host */
            Rio_writen(clientfd, buf, strlen(buf));

            /* Read the server response header */
            do{
                Rio_readlineb(&rio,buf,MAXLINE);
                printf("%s\n",buf);
            }while(strcmp(buf,"\r\n"));
            printf("end of server response header\n");

            while(Rio_readlineb(&rio,buf,MAXLINE)){
                Fputs(buf,file);
            }

            //printf("wrote to file: %s\n",uri);
            Fclose(file);
        }
    }
    /*
       while(Fgets(buf, MAXLINE, stdin)!=NULL){
       Rio_writen(clientfd, buf, strlen(buf));
       Rio_readlineb(&rio, buf, MAXLINE);
       Fputs(buf, stdout);
       }*/
    Close(clientfd);
    exit(0);
}

/* allcaps - returns str with all letters capitalized */
char *allcaps(char *str)
{
    int i=0;
    while(str[i]){
        putchar(toupper(str[i]));
        i++;
    }
    return str;
}

/* genheader - compiles a HTTP request header from host name */
void genheader(char *host,char *header){
    strcpy(header,"Host: ");
    strcat(header,host);
    strcat(header,"\r\n\r\n");
}

void genrequest(char *request, char *method, char *uri){
    /* create request string */
    strcpy(request,"GET");
    if (uri[strlen(uri)-1] == '/')
        strcat(uri, "index.html");
    strcat(request," ");
    strcat(request, uri);
    strcat(request, VERSION);
    printf("%s",request);
}


