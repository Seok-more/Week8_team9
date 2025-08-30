#include "csapp.h"

// 클라이언트와의 연결에서 데이터를 받아서 그대로 다시 보내는 echo 함수
void echo(int connfd) 
{
    size_t n;                      // 읽은 바이트 수
    char buf[MAXLINE];             // 데이터를 저장할 버퍼
    rio_t rio;                     // robust I/O(버퍼링된 입출력) 구조체

    // robust I/O 구조체 rio를 connfd(클라이언트 소켓)에 연결
    // => 이후 rio 함수로 라인 단위 읽기 가능
    Rio_readinitb(&rio, connfd);   

    // 클라이언트가 보낸 데이터를 한 줄씩 읽어서 다시 전송(echo)
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) // EOF(연결 끊김)까지 반복
    {
        // 서버가 받은 데이터의 길이와 내용을 출력
        printf("server received %d bytes: %s", (int)n, buf);

        // 받은 데이터를 그대로 클라이언트에 전송
        Rio_writen(connfd, buf, n);
    }
}

int main(int argc, char **argv) 
{
    int listenfd, connfd;                               // 서버 리스닝 소켓, 클라이언트 연결 소켓
    socklen_t clientlen;                                // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr;                 // 클라이언트 주소 저장(IPv4/IPv6 호환)
    char client_hostname[MAXLINE], client_port[MAXLINE];// 클라이언트 호스트명/포트 저장 버퍼

    // 명령행 인자 확인 (서버 실행 시 포트 번호 입력 필수)
    if (argc != 2) 
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    // 지정한 포트로 서버 소켓 생성 및 listen 상태로 설정
    listenfd = open_listenfd(argv[1]); 

    // 클라이언트 연결을 계속 받아 처리
    while (1) 
    {
        clientlen = sizeof(struct sockaddr_storage);

        // 클라이언트의 연결 요청을 수락, 새로운 연결 소켓(connfd) 생성
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 연결한 클라이언트의 호스트명과 포트 번호를 알아내서 출력
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);

        // 클라이언트와 에코 통신 수행
        echo(connfd);

        // 연결 소켓 닫기 (다음 클라이언트 연결을 받을 준비)
        Close(connfd);
    }

    exit(0); // 이 프로그램은 무한루프지만, 명시적으로 종료 코드 반환
}
