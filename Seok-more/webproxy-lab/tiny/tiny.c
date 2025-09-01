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


// �������� �����ؼ�, Ŭ���̾�Ʈ(�������� ��)���� ������ �޾Ƶ��̰�
// �� ���Ḷ�� Ŭ���̾�Ʈ�� ��û�� ó���ϴ� �Լ�
int main(int argc, char** argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // ��� ���� üũ -> ��Ʈ ��ȣ �ֳ� ����
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // ���� ������ ���� ���� (��Ʈ��ȣ��)
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        // Ŭ���̾�Ʈ ������ ��ٸ�
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen); // line:netp:tiny:accept

        // Ŭ���̾�Ʈ�� ȣ��Ʈ��� ��Ʈ ��ȣ ���
        Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // ���� ��û ó�� �Լ� ȣ��
        doit(connfd);  // Ŭ���̾�Ʈ ��û ó��

        // ���� ����
        Close(connfd); // Ŭ���̾�Ʈ���� ���� �ݱ�
    }
}

// Ŭ���̾�Ʈ(�������� ��)�� ���� HTTP ��û�� �޾Ƽ�
// ��û�� ���� �˸��� �����̳� ���α׷�(CGI)�� �����ؼ� 
// ����� Ŭ���̾�Ʈ���� �����ִ� �Լ�
void doit(int fd)
{
    int is_static;
    int is_head = 0;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    // Ŭ���̾�Ʈ�� ����� ������ robust I/O ����ü�� ���ε�
    Rio_readinitb(&rio, fd);

    // ��û ����(ù ��° ��)�� �о buf�� ���� (��: "GET /index.html HTTP/1.0")
    if (!Rio_readlineb(&rio, buf, MAXLINE)) return;

    // ��û ���� ��� (������)
    printf("Request line: %s", buf);

    // GET /index.html HTTP/1.1\r\n
    // GET http://localhost:12345/index.html HTTP/1.1\r\n (���Ͻ� ��û)
    // HEAD /image.png HTTP/1.0\r\n
    // ��û ������ method, uri, version���� �и��ؼ� ����
    sscanf(buf, "%s %s %s", method, uri, version);

    // �������� �ʴ� HTTP �޼����� ��� ���� �޽��� ���� �� �Լ� ����
    // -> 11.11 HEAD �߰�
    if (strcasecmp(method, "HEAD") == 0)
    {
        is_head = 1;
    }
    else if (strcasecmp(method, "GET"))
    {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }


    // �߰����� ��û ���(Host, User-Agent ��) �о ���� (��� Ȯ�� ����)
    read_requesthdrs(&rio);

    // URI�� �Ľ��ؼ� ����/���� ������ ���ο� ���� �̸�, CGI ���� ����
    is_static = parse_uri(uri, filename, cgiargs);

    // ��û�� ������ ������ �����ϴ��� Ȯ�� (stat ����ü�� ���� ���� ����)
    if (stat(filename, &sbuf) < 0)
    {
        // ������ ������ 404 ���� �޽��� ����
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    // ��û�� ���� ������(HTML, �̹��� ��)�� ���
    if (is_static)
    {
        // ������ �Ϲ� ������ �ƴϰų� �б� ������ ������ 403 ���� �޽��� ����
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
        }
        // ���� ������ Ŭ���̾�Ʈ���� ����
        serve_static(fd, filename, sbuf.st_size, is_head);
    }
    // ��û�� ���� ������(CGI ���α׷� ����)�� ���
    else
    {
        // ������ �Ϲ� ������ �ƴϰų� ���� ������ ������ 403 ���� �޽��� ����
        if (!S_ISREG(sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        // CGI ���α׷� ���� ����� Ŭ���̾�Ʈ���� ����
        serve_dynamic(fd, filename, cgiargs);
    }
}

// HTTP ��û�� ��� �κ��� �� �پ� �о ����(Ȥ�� ���)�ϴ� �Լ�
void read_requesthdrs(rio_t* rp)
{
    char buf[MAXLINE];

    // ��û ����� ù ���� �о buf�� ����
    Rio_readlineb(rp, buf, MAXLINE);

    // �� ��("\r\n")�� ���� ������(��� ������) �ݺ��ؼ� ��� ���� ����
    while (strcmp(buf, "\r\n"))
    {
        // ���� ��� ���� �о buf�� ����
        Rio_readlineb(rp, buf, MAXLINE);

        // ���� ��� ���� ȭ�鿡 ��� (������)
        printf("%s", buf);
    }

    // �Լ� ���� (���� ����� ����� �о �����ϴ� ����)
    return;
}


// HTTP ��û�� URI�� �м�(�Ľ�)�ؼ�
// ��û�� ���� ������(��: HTML, �̹���)����
// ��û�� ���� ������(��: CGI ���α׷�)���� �����ϰ�
// �ش� �������� �������ų� �����ϱ� ���� ���� ��ο� ����(cgiargs)�� �����ϴ� �Լ�
int parse_uri(char* uri, char* filename, char* cgiargs)
{
    if (strstr(uri, "cgi-bin") == NULL)
    {
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);

        // '/'�� ������ ��츸 home.html ����
        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");

        return 1; // �׻� ���� ������!
    }
    else
    {
        // ���� ������ (cgi-bin)
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

// ���� ������(HTML, �̹���, �ؽ�Ʈ �� �Ϲ� ����)�� 
// Ŭ���̾�Ʈ(��������)���� �����ϴ� �Լ�
void serve_static(int fd, char* filename, int filesize, int is_head)
{
    int srcfd;
    char* srcp, filetype[MAXLINE];

    char buf[MAXBUF];
    char* p = buf;
    int n;
    int remaining = sizeof(buf);

    // ���� �̸��� �������� MIME Ÿ��(��: text/html, image/png ��)�� ����
    get_filetype(filename, filetype);

    // HTTP ���� ��� �ۼ�
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

    // �ۼ��� ���� ����� Ŭ���̾�Ʈ���� ����
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n%s", buf);

    if (!is_head)
    {
        // ��û�� ������ �о Ŭ���̾�Ʈ���� ����
        srcfd = Open(filename, O_RDONLY, 0); // ���� ����

        // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // ������ �޸𸮿� ����
        srcp = (char*)malloc(filesize);
        Rio_readn(srcfd, srcp, filesize);

        Close(srcfd); // ���� ��ũ���� �ݱ�
        Rio_writen(fd, srcp, filesize);

        // Munmap(srcp, filesize); // �޸� ���� ����
        free(srcp);
    }
}


// ���� �̸��� Ȯ���ڸ� �˻��ؼ�
// HTTP ���� ����� �� MIME Ÿ��(Content-Type)�� �����ؼ�
// filetype ������ �����ϴ� �Լ�
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

// CGI(CGI-bin) ����� ���� ������(���α׷� ���� ���)��
// Ŭ���̾�Ʈ���� �������� �����ϴ� �Լ�
void serve_dynamic(int fd, char* filename, char* cgiargs)
{
    char buf[MAXLINE], * emptylist[] = { NULL };

    printf("Execve filename: %s\n", filename);

    // HTTP ������ ���� ���� �ۼ� �� ����
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // ���� ���� ����� �ۼ� �� ����
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // �ڽ� ���μ��� ���� (CGI ���α׷� ������ ����)
    if (Fork() == 0)
    {
        // ȯ�� ���� QUERY_STRING�� CGI ����(cgiargs) ����
        setenv("QUERY_STRING", cgiargs, 1);

        // ǥ�� ����� Ŭ���̾�Ʈ�� ����� ����(fd)�� �����̷���
        Dup2(fd, STDOUT_FILENO);

        // CGI ���α׷� ���� (filename ����� ���α׷� ����)
        Execve(filename, emptylist, environ);
    }

    // �θ� ���μ����� �ڽ� ���μ���(CGI ���α׷�) ������� ���
    Wait(NULL);
}

// Ŭ���̾�Ʈ(��������)�� �߸��� ��û�� ������ ��,
// HTTP ���� ����(��: 404 Not Found, 501 Not Implemented ��)��
// HTML �������� ���� Ŭ���̾�Ʈ���� �����ִ� �Լ�
void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    // ���� ������ HTML ����(body) ����
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body); // ���� ���
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg); // ���� �ڵ� �� ª�� ����
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause); // �� ���� �� ����
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body); // ���� ����

    // HTTP ������ ���� �� ����
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));

    // Content-Type ��� ���� (HTML)
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // Content-Length ��� ���� (���� ����)
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    // HTML ����(body) ����
    Rio_writen(fd, body, strlen(body));
}