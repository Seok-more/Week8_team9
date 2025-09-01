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
void read_requesthdrs(rio_t* rp);
int parse_uri(char* uri, char* filename, char* cgiargs);
void serve_static(int fd, char* filename, int filesize, int is_head);
void get_filetype(char* filename, char* filetype);
void serve_dynamic(int fd, char* filename, char* cgiargs);
void clienterror(int fd, char* cause, char* errnum, char* shortmsg,
    char* longmsg);


// 웹서버를 실행해서, 클라이언트(웹브라우저 등)와의 연결을 받아들이고
// 각 연결마다 클라이언트의 요청을 처리하는 함수
int main(int argc, char** argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // 명령 인자 체크 -> 포트 번호 있냐 없냐
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 서버 리스닝 소켓 생성 (포트번호로)
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        // 클라이언트 연결을 기다림
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen); // line:netp:tiny:accept

        // 클라이언트의 호스트명과 포트 번호 얻기
        Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // 실제 요청 처리 함수 호출
        doit(connfd);  // 클라이언트 요청 처리

        // 연결 종료
        Close(connfd); // 클라이언트와의 연결 닫기
    }
}

// 클라이언트(웹브라우저 등)가 보낸 HTTP 요청을 받아서
// 요청에 따라 알맞은 파일이나 프로그램(CGI)을 실행해서 
// 결과를 클라이언트에게 돌려주는 함수
void doit(int fd)
{
    int is_static;
    int is_head = 0;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    // 클라이언트와 연결된 소켓을 robust I/O 구조체에 바인딩
    Rio_readinitb(&rio, fd);

    // 요청 라인(첫 번째 줄)을 읽어서 buf에 저장 (예: "GET /index.html HTTP/1.0")
    if (!Rio_readlineb(&rio, buf, MAXLINE)) return;

    // 요청 라인 출력 (디버깅용)
    printf("Request line: %s", buf);

    // GET /index.html HTTP/1.1\r\n
    // GET http://localhost:12345/index.html HTTP/1.1\r\n (프록시 요청)
    // HEAD /image.png HTTP/1.0\r\n
    // 요청 라인을 method, uri, version으로 분리해서 저장
    sscanf(buf, "%s %s %s", method, uri, version);

    // 지원하지 않는 HTTP 메서드일 경우 에러 메시지 전송 후 함수 종료
    // -> 11.11 HEAD 추가
    if (strcasecmp(method, "HEAD") == 0)
    {
        is_head = 1;
    }
    else if (strcasecmp(method, "GET"))
    {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }


    // 추가적인 요청 헤더(Host, User-Agent 등) 읽어서 무시 (기능 확장 가능)
    read_requesthdrs(&rio);

    // URI를 파싱해서 정적/동적 컨텐츠 여부와 파일 이름, CGI 인자 추출
    is_static = parse_uri(uri, filename, cgiargs);

    // 요청한 파일이 실제로 존재하는지 확인 (stat 구조체에 파일 정보 저장)
    if (stat(filename, &sbuf) < 0)
    {
        // 파일이 없으면 404 에러 메시지 전송
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    // 요청이 정적 컨텐츠(HTML, 이미지 등)일 경우
    if (is_static)
    {
        // 파일이 일반 파일이 아니거나 읽기 권한이 없으면 403 에러 메시지 전송
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
        }
        // 정적 파일을 클라이언트에게 전송
        serve_static(fd, filename, sbuf.st_size, is_head);
    }
    // 요청이 동적 컨텐츠(CGI 프로그램 실행)일 경우
    else
    {
        // 파일이 일반 파일이 아니거나 실행 권한이 없으면 403 에러 메시지 전송
        if (!S_ISREG(sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        // CGI 프로그램 실행 결과를 클라이언트에게 전송
        serve_dynamic(fd, filename, cgiargs);
    }
}

// HTTP 요청의 헤더 부분을 한 줄씩 읽어서 무시(혹은 출력)하는 함수
void read_requesthdrs(rio_t* rp)
{
    char buf[MAXLINE];

    // 요청 헤더의 첫 줄을 읽어서 buf에 저장
    Rio_readlineb(rp, buf, MAXLINE);

    // 빈 줄("\r\n")이 나올 때까지(헤더 끝까지) 반복해서 헤더 줄을 읽음
    while (strcmp(buf, "\r\n"))
    {
        // 다음 헤더 줄을 읽어서 buf에 저장
        Rio_readlineb(rp, buf, MAXLINE);

        // 읽은 헤더 줄을 화면에 출력 (디버깅용)
        printf("%s", buf);
    }

    // 함수 종료 (실제 기능은 헤더를 읽어서 무시하는 역할)
    return;
}


// HTTP 요청의 URI를 분석(파싱)해서
// 요청이 정적 컨텐츠(예: HTML, 이미지)인지
// 요청이 동적 컨텐츠(예: CGI 프로그램)인지 구분하고
// 해당 컨텐츠를 가져오거나 실행하기 위한 파일 경로와 인자(cgiargs)를 추출하는 함수
int parse_uri(char* uri, char* filename, char* cgiargs)
{
    if (strstr(uri, "cgi-bin") == NULL)
    {
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);

        // '/'로 끝나는 경우만 home.html 붙임
        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");

        return 1; // 항상 정적 컨텐츠!
    }
    else
    {
        // 동적 컨텐츠 (cgi-bin)
        char* ptr = index(uri, '?');
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}

// 정적 컨텐츠(HTML, 이미지, 텍스트 등 일반 파일)를 
// 클라이언트(웹브라우저)에게 전송하는 함수
void serve_static(int fd, char* filename, int filesize, int is_head)
{
    int srcfd;
    char* srcp, filetype[MAXLINE];

    char buf[MAXBUF];
    char* p = buf;
    int n;
    int remaining = sizeof(buf);

    // 파일 이름을 바탕으로 MIME 타입(예: text/html, image/png 등)을 결정
    get_filetype(filename, filetype);

    // HTTP 응답 헤더 작성
    n = snprintf(p, remaining, "HTTP/1.0 200 OK\r\n");
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "Server: Tiny Web Server\r\n");
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "Connection: close\r\n");
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "Content-length: %d\r\n", filesize);
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "Content-type: %s\r\n\r\n", filetype);
    p += n;
    remaining -= n;

    // 작성한 응답 헤더를 클라이언트에게 전송
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n%s", buf);

    if (!is_head)
    {
        // 요청한 파일을 읽어서 클라이언트에게 전송
        srcfd = Open(filename, O_RDONLY, 0); // 파일 열기

        // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일을 메모리에 매핑
        srcp = (char*)malloc(filesize);
        Rio_readn(srcfd, srcp, filesize);

        Close(srcfd); // 파일 디스크립터 닫기
        Rio_writen(fd, srcp, filesize);

        // Munmap(srcp, filesize); // 메모리 매핑 해제
        free(srcp);
    }
}


// 파일 이름의 확장자를 검사해서
// HTTP 응답 헤더에 들어갈 MIME 타입(Content-Type)을 결정해서
// filetype 변수에 저장하는 함수
void get_filetype(char* filename, char* filetype)
{
    if (strstr(filename, ".html"))
    {
        strcpy(filetype, "text/html");
    }
    else if (strstr(filename, ".gif"))
    {
        strcpy(filetype, "image/gif");
    }
    else if (strstr(filename, ".png"))
    {
        strcpy(filetype, "image/png");
    }
    else if (strstr(filename, "jpg"))
    {
        strcpy(filetype, "image/jpeg");
    }
    // 11.7 Expand to MPG
    else if (strstr(filename, ".mpg"))
    {
        strcpy(filetype, "video/mpeg");
    }
    else if (strstr(filename, ".mp4"))
    {
        strcpy(filetype, "video/mp4");
    }
    //
    else
    {
        strcpy(filetype, "text/plain");
    }
}

// CGI(CGI-bin) 방식의 동적 컨텐츠(프로그램 실행 결과)를
// 클라이언트에게 응답으로 전송하는 함수
void serve_dynamic(int fd, char* filename, char* cgiargs)
{
    char buf[MAXLINE], * emptylist[] = { NULL };

    printf("Execve filename: %s\n", filename);

    // HTTP 응답의 상태 줄을 작성 및 전송
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // 서버 정보 헤더를 작성 및 전송
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // 자식 프로세스 생성 (CGI 프로그램 실행을 위해)
    if (Fork() == 0)
    {
        // 환경 변수 QUERY_STRING에 CGI 인자(cgiargs) 저장
        setenv("QUERY_STRING", cgiargs, 1);

        // 표준 출력을 클라이언트와 연결된 소켓(fd)로 리다이렉션
        Dup2(fd, STDOUT_FILENO);

        // CGI 프로그램 실행 (filename 경로의 프로그램 실행)
        Execve(filename, emptylist, environ);
    }

    // 부모 프로세스는 자식 프로세스(CGI 프로그램) 종료까지 대기
    Wait(NULL);
}

// 클라이언트(웹브라우저)가 잘못된 요청을 보냈을 때,
// HTTP 에러 응답(예: 404 Not Found, 501 Not Implemented 등)을
// HTML 형식으로 만들어서 클라이언트에게 보내주는 함수
void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    // 에러 응답의 HTML 본문(body) 생성
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body); // 배경색 흰색
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg); // 에러 코드 및 짧은 설명
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause); // 긴 설명 및 원인
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body); // 서버 정보

    // HTTP 상태줄 생성 및 전송
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));

    // Content-Type 헤더 전송 (HTML)
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // Content-Length 헤더 전송 (본문 길이)
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    // HTML 본문(body) 전송
    Rio_writen(fd, body, strlen(body));
}