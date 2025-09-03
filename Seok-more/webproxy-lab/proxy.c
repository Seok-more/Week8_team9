#include <stdio.h>
#include "csapp.h"
#include <pthread.h>


// MAX_CACHE_SIZE = 1 MiB
// MAX_OBJECT_SIZE = 100 KiB
// where T is the maximum number of active connections: MAX_CACHE_SIZE + T * MAX_OBJECT_SIZE
// approximates a least-recently-used (LRU) eviction policy -> �յ� ���� ����Ʈ �����
// using Pthreads readers-writers locks


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char* user_agent_hdr =
"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
"Firefox/10.0.3\r\n";

// 1. ��Ʈ�� ���ε��ؼ� Ŭ���̾�Ʈ�κ��� HTTP ��û �ޱ�
// 2. Ŭ���̾�Ʈ ��û�� �޾Ƽ� ���� �� ����(tiny)�� ����
// 3. �� ������ ������ �޾Ƽ� �ٽ� Ŭ���̾�Ʈ(������)���� ����
// 4. + ���� ���� ó��
// 5. + ĳ�� �� ������Ʈ

// [Ŭ���̾�Ʈ] ----HTTP��û----> [���Ͻ�] ----HTTP��û----> [tiny]
//            <----HTTP����----         <----HTTP����----    

// ĳ�� ����ü
typedef struct cache_block
{
    char uri[MAXLINE];        // ��û�� URI (key ����)
    char* object;             // ���� ���� ������ (heap�� ���� �Ҵ�)
    int size;                 // object ũ�� (����Ʈ)

    struct cache_block* prev;  // ���� ��� (LRU ����Ʈ)
    struct cache_block* next;  // ���� ��� (LRU ����Ʈ)
} cache_block_t;

typedef struct cache_list
{
    cache_block_t* head;       // LRU ����: ���� ������ ���
    cache_block_t* tail;       // LRU ����: ���� �ֱ� ���� ���
    int total_size;            // ���� ĳ�õ� ��ü ����Ʈ ��

    pthread_rwlock_t lock;     // �б�/���� ��ȣ�� ��
} cache_list_t;


void doit(int fd);
void read_requesthdrs(rio_t* rp, char* header_host, char* header_other);
void parse_uri(char* uri, char* hostname, char* port, char* path);
void reassemble(char* req, char* method, char* path, char* hostname, char* other_header);
void forward_response(int serverfd, int fd, int is_head);

//////////////////////////////
// ĳ�ð���
cache_list_t cache;
void cache_init();
void cache_free_block(cache_block_t* block);
void cache_clear(cache_list_t* cache);
void cahce_node_end(cache_list_t* cache, cache_block_t* node);
void cache_evict(cache_list_t* cache, int size_needed);
void cache_insert(cache_list_t* cache, const char* uri, const char* object, int size);
int cache_find(cache_list_t* cache, const char* uri, char* object_buf, int* size_buf);
//////////////////////////////


void* thread(void* vargp)
{
    int connfd = *((int*)vargp);
    free(vargp); // ���� �Ҵ� ����
    doit(connfd);
    Close(connfd); // Ŭ���̾�Ʈ ���� ����
    return NULL;
}

int main(int argc, char** argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    cache_init();

    // ��� ���� üũ -> ��Ʈ ��ȣ �ֳ� ����
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // ���� ������ ���� ���� (��Ʈ��ȣ��)
    listenfd = Open_listenfd(argv[1]);
    printf("Proxy listening on port %s\n", argv[1]);
    fflush(stdout);

    while (1)
    {
        // Ŭ���̾�Ʈ ������ ��ٸ�
        clientlen = sizeof(clientaddr);
        int* connfdp = malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen); // line:netp:tiny:accept

        // Ŭ���̾�Ʈ�� ȣ��Ʈ��� ��Ʈ ��ȣ ���
        Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // // ���� ��û ó�� �Լ� ȣ��
        // doit(connfd);  // Ŭ���̾�Ʈ ��û ó��
        // // ���� ����
        // Close(connfd); // Ŭ���̾�Ʈ���� ���� �ݱ�

        // ���� ó��
        pthread_t tid;
        pthread_create(&tid, NULL, thread, connfdp);
        pthread_detach(tid);
    }
}

// Ŭ���̾�Ʈ�� HTTP ��û�� �а�
// Ÿ�� ����(tiny)�� �°� ��û�� ��ȯ�ؼ� ����
// Ÿ�� ������ ������ �޾�
// �ٽ� Ŭ���̾�Ʈ���� �����ϴ� �Լ�
void doit(int fd)
{
    int is_head = 0;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char header_host[MAXLINE], header_other[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char request[MAXLINE];
    rio_t rio;

    // Ŭ���̾�Ʈ�� ����� ������ robust I/O ����ü�� ���ε�
    Rio_readinitb(&rio, fd);

    // ��û ����(ù ��° ��)�� �о buf�� ���� (��: "GET /index.html HTTP/1.0")
    if (!Rio_readlineb(&rio, buf, MAXLINE)) return;

    // ��û ���� ��� (������)
    printf("Request line: %s", buf);

    // GET /index.html HTTP/1.1\r\n
    // /index.html
    // GET http://localhost:12345/index.html HTTP/1.1\r\n (���Ͻ� ��û)
    // HEAD /image.png HTTP/1.0\r\n
    // ��û ������ method, uri, version���� �и��ؼ� ����
    sscanf(buf, "%s %s %s", method, uri, version);

    // �������� �ʴ� HTTP �޼����� ��� ���� �޽��� ���� �� �Լ� ����
    if (strcasecmp(method, "HEAD") == 0)
    {
        strcpy(method, "GET");
        is_head = 1;
    }
    else if (strcasecmp(method, "GET"))
    {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    // �߰����� ��û ���(Host, User-Agent ��) �о ���� (��� Ȯ�� ����)
    read_requesthdrs(&rio, header_host, header_other);

    // URI�� �Ľ��ؼ� hostname(������ �ּ�), port(������ ��Ʈ), path(��û ���) ����
    parse_uri(uri, hostname, port, path);

    // ĳ�� üũ
    char object_buf[MAX_OBJECT_SIZE];
    int object_size = 0;
    if (cache_find(&cache, uri, object_buf, &object_size))
    {
        // ĳ�ÿ� ������ �ٷ� Ŭ���̾�Ʈ���� ����
        if (!is_head)
        {
            Rio_writen(fd, object_buf, object_size);
        }
        return;
    }

    // ĳ�ð� ������ ������ ��û, ������ hostname�� port�� ������(tiny)�� �����ؼ� ���� ���� ����
    int serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0)
    {
        clienterror(fd, hostname, "502", "Bad Gateway", "Proxy couldn't connect to server");
        return;
    }

    // Ŭ���̾�Ʈ�� ��û�� �������� �´� ����(HTTP/1.0 ��)���� ������
    // request���� ���������� tiny ������ ���� ��û�� ��
    reassemble(request, method, path, hostname, header_other);

    // �������� request�� ������(���� ����)�� ����
    Rio_writen(serverfd, request, strlen(request));

    // �������� ������ �޾Ƽ� Ŭ���̾�Ʈ�� ���� + ĳ��
    rio_t rio_server;                    // ����(tiny) ���Ͽ� robust I/O ����ü ���ε�.
    char cache_object[MAX_OBJECT_SIZE];  // ĳ�ÿ� ������ ������ ���� (�ִ� 100 KiB).
    int cache_size = 0;                  // ������� ���� �������� �� ũ��.
    ssize_t n;                           // �� ���� �о�� �������� ����Ʈ ��.

    Rio_readinitb(&rio_server, serverfd);

    // ����(tiny)�κ��� �����͸� �ݺ��ؼ� ����, ���� �����Ͱ� ������ �ݺ�
    while ((n = Rio_readnb(&rio_server, buf, MAX_OBJECT_SIZE)) > 0)
    {
        // Ŭ���̾�Ʈ�� ����
        if (!is_head)
        {
            Rio_writen(fd, buf, n);
        }

        // ĳ���� ������Ʈ ũ�� ���� �̳���� ĳ�� ���ۿ� �ױ�
        if (cache_size + n <= MAX_OBJECT_SIZE)
        {
            memcpy(cache_object + cache_size, buf, n);
        }
        cache_size += n;
    }

    // �������� ������ �޾Ƽ� Ŭ���̾�Ʈ���� �״�� ���� -> ĳ�� ���ܼ� �н�
    // forward_response(serverfd, fd, is_head);

    // Ÿ�� �������� ���� �ݱ� (�ڿ� ����)
    Close(serverfd);

    // �� �а� ĳ��
    if (cache_size > 0 && cache_size <= MAX_OBJECT_SIZE)
    {
        cache_insert(&cache, uri, cache_object, cache_size);
    }
}

// HTTP ��û�� ��� �κ��� �� �پ� �о Host ����� ��Ÿ ����� �и��ϴ� �Լ�
void read_requesthdrs(rio_t* rp, char* header_host, char* header_other)
{
    char buf[MAXLINE];

    // ��� ���۸� �� ���ڿ��� �ʱ�ȭ
    header_host[0] = '\0';
    header_other[0] = '\0';

    // ����� ���� "\r\n" (�� ��)�̹Ƿ�, �� ���� ���� ������ �ݺ�
    while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
    {
        // Host ����� ������ ���� (���� ���� �� ù ��°�� ����)
        if (!strncasecmp(buf, "Host:", 5))
        {
            if (header_host[0] == '\0')
            {
                strncpy(header_host, buf, MAXLINE - 1);
            }

        }
        // ���Ͻð� ���� ����/������ ����� ����
        else if (!strncasecmp(buf, "User-Agent:", 11) ||
            !strncasecmp(buf, "Connection:", 11) ||
            !strncasecmp(buf, "Proxy-Connection:", 17))
        {
            continue;
        }

        // ������ ����� header_other�� ���� (�����÷ο� ����)
        else
        {
            // ���� ���� ���
            int remain = MAXLINE - strlen(header_other) - 1;
            if (remain > 0)
            {
                strncat(header_other, buf, remain);
            }
        }
    }
}

void parse_uri(char* uri, char* hostname, char* port, char* path)
{
    char* hostbegin, * hostend, * portbegin, * pathbegin;
    char buf[MAXLINE];

    // uri�� buf�� ���� (���� �ı� ����)
    strcpy(buf, uri);

    // "//" ��ġ�� ã�� hostbegin ���� (http:// ���� ����)
    hostbegin = strstr(buf, "//");
    hostbegin = (hostbegin != NULL) ? hostbegin + 2 : buf;

    // ���(path) ���� ��ġ ã��. ������ �⺻ "/"
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin != NULL)
    {
        strcpy(path, pathbegin);      // path�� ��� ����
        *pathbegin = '\0';            // host �κ� ���ڿ� �� ����
    }
    else
    {
        strcpy(path, "/");
    }

    // ��Ʈ ��ȣ(:��Ʈ) ��ġ ã��
    portbegin = strchr(hostbegin, ':');
    if (portbegin != NULL)
    {
        *portbegin = '\0';            // host �κ� ���ڿ� �� ����
        strcpy(hostname, hostbegin);  // hostname ����
        strcpy(port, portbegin + 1);  // ��Ʈ��ȣ ����
    }
    else
    {
        strcpy(hostname, hostbegin);  // ��Ʈ ������ hostname ����
        strcpy(port, "80");           // �⺻ ��Ʈ 80
    }
}

// ���Ͻð� tiny ������ ������ HTTP ��û�� �������ϴ� �Լ� (GET/HEAD �� �޼��� ����)
void reassemble(char* req, char* method, char* path, char* hostname, char* other_header)
{
    // method ���ڸ� ����Ͽ� ��û ������ �������� ����
    sprintf(req,
        "%s %s HTTP/1.0\r\n"                // ��û ���� (GET/HEAD �� �޼���)
        "Host: %s\r\n"                      // Host ���
        "%s"                                // User-Agent ��� (���� ���)
        "Connection: close\r\n"             // ���� ���� ���
        "Proxy-Connection: close\r\n"       // ���Ͻ� ���� ���� ���
        "%s"                                // ��Ÿ ���
        "\r\n",                             // ��� �� ǥ��
        method,
        path,
        hostname,
        user_agent_hdr,
        other_header
    );
}

// ����(tiny ��)�� ������ �޾� Ŭ���̾�Ʈ���� �״�� �����ϴ� �Լ�
void forward_response(int serverfd, int fd, int is_head)
{
    rio_t rio_server;                   // ���� ���Ͽ� ���� robust I/O ����ü
    char buf[MAX_OBJECT_SIZE];          // �����κ��� �о�� ������ ���� (�ִ� ĳ�� ������Ʈ ũ��)
    ssize_t n;                          // �о�� �������� ����Ʈ ��

    Rio_readinitb(&rio_server, serverfd); // ���� ������ robust I/O ����ü�� ���ε�

    // �����κ��� �����͸� �ݺ������� �о Ŭ���̾�Ʈ�� ����
    while ((n = Rio_readnb(&rio_server, buf, MAX_OBJECT_SIZE)) > 0)
    {
        if (!is_head)
        {                               // HEAD ��û�� �ƴ϶��
            Rio_writen(fd, buf, n);     // Ŭ���̾�Ʈ �������� ���� ������ ����
        }
        // HEAD ��û�̸� �б⸸ �ϰ� Ŭ���̾�Ʈ�� ������ ����
    }
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
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body); // ���� ����

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

///////////////////////////////////////////////////////////////////////////
// ĳ�� ����

// ĳ�� �ʱ�ȭ
void cache_init()
{
    cache.head = NULL; // LRU ����Ʈ�� head(���� ������) ������ �ʱ�ȭ
    cache.tail = NULL; // LRU ����Ʈ�� tail(���� �ֱ�) ������ �ʱ�ȭ
    cache.total_size = 0; // ���� ĳ�õ� ��ü ����Ʈ �� �ʱ�ȭ
    pthread_rwlock_init(&cache.lock, NULL); // �б�/���� �� �ʱ�ȭ (���ü� ����)
}

// ĳ�� �� ����
void cache_free_block(cache_block_t* block)
{
    if (block)
    {
        free(block->object);
        free(block);
    }
}

// ĳ�� ���� 
void cache_clear(cache_list_t* cache)
{
    pthread_rwlock_wrlock(&cache->lock); // ĳ�� �����̹Ƿ� ���� ��

    cache_block_t* node = cache->head;
    while (node)
    {
        cache_block_t* next = node->next;
        cache_free_block(node);
        node = next;
    }

    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;

    pthread_rwlock_unlock(&cache->lock);

}


// LRU ����Ʈ���� node�� ���� ��(�ֱ� ���)�� �̵���Ŵ
void cahce_node_end(cache_list_t* cache, cache_block_t* node)
{
    if (node == cache->tail) return;

    // ����Ʈ���� node ����
    if (node->prev)
    {
        node->prev->next = node->next;
    }
    else // node�� head
    {
        cache->head = node->next;
    }

    if (node->next)
    {
        node->next->prev = node->prev;
    }
    else // node�� tail
    {
        cache->tail = node->prev;
    }

    // ���� ����
    node->prev = cache->tail;
    node->next = NULL;

    if (cache->tail)
    {
        cache->tail->next = node;
    }
    else // ĳ�ð� �����
    {
        cache->head = node;
    }

    // tail ����
    cache->tail = node;
}

// ĳ�ð� ��ĥ ���, ������ �ͺ��� ����(LRU)�ϴ� �Լ�
void cache_evict(cache_list_t* cache, int size_needed)
{
    // ĳ�ÿ� �� ������(size_needed)�� �־��� �� �뷮 �ʰ��� �ݺ������� evict
    while (cache->total_size + size_needed > MAX_CACHE_SIZE)
    {
        if (cache->head == NULL) return; // ĳ�ð� ������� ����

        cache_block_t* oldest = cache->head; // ���� ������ ��� ����

        cache->head = oldest->next; // head�� ���� ������� �̵�
        if (cache->head)
        {
            cache->head->prev = NULL; // head�� prev�� NULL��
        }
        else
        {
            cache->tail = NULL; // ����Ʈ�� ����� ��� tail�� NULL
        }

        cache->total_size -= oldest->size; // ĳ�� ��뷮 ����

        cache_free_block(oldest);
    }
}

// ���ο� ������Ʈ�� ĳ�ÿ� �����ϴ� �Լ�
void cache_insert(cache_list_t* cache, const char* uri, const char* object, int size)
{
    if (size > MAX_OBJECT_SIZE) return; // ������Ʈ�� �ʹ� ũ�� ĳ������ ����

    pthread_rwlock_wrlock(&cache->lock); // ���� �� ȹ��

    cache_evict(cache, size); // ���� �� �뷮 �ʰ��� �κ� ����

    cache_block_t* new_block = malloc(sizeof(cache_block_t)); // �� ��� ����
    if (!new_block)
    {
        pthread_rwlock_unlock(&cache->lock);
        return;
    }

    strncpy(new_block->uri, uri, MAXLINE - 1); // URI ���� (key ����)
    new_block->uri[MAXLINE - 1] = '\0';

    new_block->object = malloc(size); // ������Ʈ ������ ������ ���� �޸� �Ҵ�
    if (!new_block->object)
    {
        cache_free_block(new_block);
        pthread_rwlock_unlock(&cache->lock);
        return;
    }

    memcpy(new_block->object, object, size); // ���� ���� ������ ����
    new_block->size = size;

    new_block->prev = cache->tail; // �� ����� tail �ڿ� ����
    new_block->next = NULL;

    if (cache->tail)
    {
        cache->tail->next = new_block;
    }
    else
    {
        cache->head = new_block; // ĳ�ð� ������� head�� �� ���
    }

    cache->tail = new_block; // tail�� �� ������� ����
    cache->total_size += size; // ĳ�� ��뷮 ����

    pthread_rwlock_unlock(&cache->lock); // �� ����
}

// ĳ�ÿ��� �ش� URI�� �´� ������Ʈ�� ã�� object_buf�� �������ִ� �Լ�
// ��ȯ��: 1(����) / 0(����)
int cache_find(cache_list_t* cache, const char* uri, char* object_buf, int* size_buf)
{
    pthread_rwlock_wrlock(&cache->lock); // ó������ ���� ��

    cache_block_t* node = cache->head;
    while (node)
    {
        if (strcmp(node->uri, uri) == 0)
        {
            cahce_node_end(cache, node); // �ֱ� ������� tail�� �̵�
            memcpy(object_buf, node->object, node->size); // ������ ����
            *size_buf = node->size;

            pthread_rwlock_unlock(&cache->lock); // �� ����
            return 1;
        }
        node = node->next;
    }

    pthread_rwlock_unlock(&cache->lock); // �� ã�� ��� �� ����
    return 0;
}