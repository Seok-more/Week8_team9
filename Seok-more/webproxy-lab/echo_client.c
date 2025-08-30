#include "csapp.h"

int main(int argc, char **argv) 
{
    int clientfd;                   // 서버와 연결된 소켓 파일 디스크립터 저장 변수
    char *host, *port, buf[MAXLINE];// 서버 주소, 포트, 입출력 버퍼
    rio_t rio;                      // Robust I/O(버퍼링된 I/O)용 구조체

    // [1] 명령줄 인자 개수 확인
    // 실행 시: ./echo_client <host> <port> 형태로 입력해야 함
    if (argc != 3) 
    {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0); // 인자가 잘못되면 프로그램 종료
    }

    host = argv[1]; // [2] 첫 번째 인자를 서버 주소 문자열로 저장
    port = argv[2]; // [3] 두 번째 인자를 서버 포트 문자열로 저장

    // [4] 서버와 연결되는 TCP 소켓 생성 + connect 호출
    // open_clientfd()는 socket() -> connect()까지 수행하고,
    // 성공 시 연결된 소켓의 FD(정수)를 반환
    clientfd = open_clientfd(host, port);

    // [5] Robust I/O 라이브러리 초기화
    // rio 구조체에 clientfd를 등록해 버퍼링된 I/O 사용 준비
    Rio_readinitb(&rio, clientfd);

    // [6] 표준 입력(stdin)에서 한 줄씩 읽고 서버로 보내는 루프
    while (fgets(buf, MAXLINE, stdin) != NULL) 
    {
        // [6-1] 입력받은 문자열을 서버로 전송
        // clientfd는 서버와 연결된 소켓 FD → 커널 송신 버퍼에 기록
        Rio_writen(clientfd, buf, strlen(buf));

        // [6-2] 서버가 보낸 응답 한 줄을 수신
        // 커널 수신 버퍼에서 데이터 읽기
        Rio_readlineb(&rio, buf, MAXLINE);

        // [6-3] 받은 데이터를 표준 출력(stdout)에 출력
        fputs(buf, stdout);
    }       

    // [7] 서버와 연결된 소켓 FD 닫기
    // 커널이 소켓 객체 참조를 해제하고 연결 종료
    Close(clientfd);

    exit(0); // 프로그램 정상 종료
}