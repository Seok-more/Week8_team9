/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void server_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void server_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

//클라이언트의 HTTP 요청을 받아서 적절한 응답을 제공(URI 파싱, 분석, 존재확인, 콘텐츠제공)
void doit (int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // 요청라인과 헤더들을 읽어라.
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // GET요청으로부터 URI를 파싱
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "NOT Found", "Tiny couldn't read the file");
    return;
  }
  // 정적 콘텐츠를 서버에서 제공
  // 정적 타입인가, 아닌가
  if (is_static) {
    // 일반 파일인가? 또는 읽기권한이있나? 둘다 아닌가
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    server_static(fd, filename, sbuf.st_size);
  }
  // 동적 콘텐츠를 서버에서 제공
  else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI progeam");
      return;
    }
    server_dynamic(fd, filename, cgiargs);
  }
}

// 서버에서 client요청으로 처리하다가 error가 났을때 상황을 HTTP응답으로 알려주는 함수.
void clienterror (int fd, char* cause, char* errnum, char* shortmsg, char* longmsg) {
  char buf[MAXLINE], body[MAXLINE];

  // HTTP응답 본문 구성
  // 왜 이렇게 쓸데없이 만들어놨지?
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s : %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  //HTTP응답 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Context type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Context Length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// 클라이언트로부터 요청된 HTTP를 출력함
void read_requesthdrs(rio_t* rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    printf("%s", buf);
    Rio_readlineb(rp, buf, MAXLINE);
  }
  return;
}

// uri를 파싱해서 filename과 cgiargs를 추출, 정적 or 동적 콘텐츠를 구분
int parse_uri (char* uri, char* filename, char* cgiargs) {
  char* ptr;
  // 정적 콘텐츠
  if(!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    // uri의 마지막 인덱스가 / 이면,
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  }
  // 동적 콘텐츠
  else {
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

// 정적 콘텐츠 전송
void server_static (int fd, char *filename, int filesize) {
  int srcfd;
  char* srcp, filetype[MAXLINE], buf[MAXBUF];

  // 응답 헤더를 클라이언트에게 전송
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer : Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("serve_static:\n\n %s", buf);

  // 응답 본문 클라이언트 전송
  printf("filename: %s\n", filename);
  printf("fd: %d\n", fd);
  printf("filesize: %d\n", filesize);
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

// 파일이름에서 파일의 유형을 추출
void get_filetype (char* filename, char* filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  }
  else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  }
  else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  }
  else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  }
  else {
    strcpy(filetype, "text/plain");
  }
}

void server_dynamic (int fd, char* filename, char* cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  // HTTP응답의 첫번째 부분을 반환.
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // 여기서 실제 서버가 모든 CGI 변수를 초기화함.
  if (Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    // 표준 출력을 클라이언트에게 전달한다.
    Dup2(fd, STDOUT_FILENO);
    // CGI프로그램 실행
    Execve(filename, emptylist, environ);
  }
  // 부모가 자식 프로세스를 기다리고 종료함.
  Wait(NULL);
}