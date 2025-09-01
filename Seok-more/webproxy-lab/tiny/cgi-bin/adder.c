#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	MAXLINE	 8192

// int main(void)
// {
//     char *buf, *p;            // QUERY_STRING 환경변수를 가리킬 포인터
//     char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
//     int n1 = 0, n2 = 0;

//     // CGI 규약에 따라 웹서버가 QUERY_STRING 환경변수에 쿼리 파라미터를 넣어줌
//     buf = getenv("QUERY_STRING"); // 쿼리 스트링 읽기, 예: "123&456"

//     if (buf != NULL && (p = strchr(buf, '&')) != NULL) 
//     {
//         *p = '\0';                      
//         strcpy(arg1, buf);
//         strcpy(arg2, p + 1);
//         n1 = atoi(arg1);
//         n2 = atoi(arg2);
//     }

//     else if (buf != NULL)
//     {
//         n1 = atoi(buf);
//         n2 = 0;
//     } 

//     // CGI 규약에 따라 Content-type 헤더를 반드시 출력해야 브라우저가 인식한다고함
//     printf("Content-type: text/html\r\n\r\n");

//     // HTML
//     printf("<html><body>\n");
//     printf("QUERY_STRING=%s<br>\n", buf ? buf : "");
//     printf("Welcome to add.com: THE Internet addition portal.<br>\n");
//     printf("The answer is: %d + %d = %d<br>\n", n1, n2, n1 + n2);
//     printf("Thanks for visiting!\n");
//     printf("</body></html>\n");
//     fflush(stdout); // 출력 버퍼를 비움(브라우저로 바로 전송)

//     return 0;
// }

int main(void)
{
    char* buf;
    int n1 = 0, n2 = 0;

    buf = getenv("QUERY_STRING");
    if (buf != NULL)
    {
        if (strchr(buf, '&'))
        {
            //sscanf(buf, "%d&%d", &n1, &n2);
            sscanf(buf, "first=%d&second=%d", &n1, &n2);
        }
        else
        {
            sscanf(buf, "first=%d", &n1);
        }
    }

    printf("Content-type: text/html\r\n\r\n");
    printf("<html><body>\n");
    printf("QUERY_STRING=%s<br>\n", buf ? buf : "");
    printf("Welcome to add.com: THE Internet addition portal.<br>\n");
    printf("The answer is: %d + %d = %d<br>\n", n1, n2, n1 + n2);
    printf("Thanks for visiting!\n");
    printf("</body></html>\n");
    fflush(stdout);
    return 0;
}