#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define UDS_PATH "/tmp/dynamic_html_create.sock"
#define MAXLINE 1024

int main(void){
    int listen_sock, accp_sock;
    struct sockaddr_un addr;
    char buf[MAXLINE];

    // 기존 소켓 삭제.
    unlink(UDS_PATH);

    // 연결될 때까지 대기하는 소켓 생성.
    if((listen_sock = socket(AF_UNIX, SOCK_STREAM, 0))<0){
        perror("UDS socket fail");
        exit(1);
    }

    // 소켓 주소 구조체 초기화
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UDS_PATH, sizeof(addr.sun_path)-1);

    // 소켓과 소켓 주소 구조체 묶기
    if(bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr))<0){
        perror("UDS socket fail");
        exit(1);
    }

    // 5개까지 대기.
    if(listen(listen_sock, 5) < 0){
        perror("UDS listen fail");
        exit(1);
    }

    while(1){
        // 클라이언트와 통신하는 소켓 생성.
        if((accp_sock = accept(listen_sock, NULL, NULL))<0){
            continue;
        }
        int read_bytes_num = read(accp_sock, buf, sizeof(buf) - 1);
        if (read_bytes_num > 0){
            buf[read_bytes_num] = '\0';
            char color[50] = "white"; // 기본 배경색
            // "color="의 위치를 먼저 찾는다.
            char *color_ptr = strstr(buf, "color=");
            if(color_ptr != NULL){
                color_ptr += 6;
                strcpy(color, color_ptr);
            }
            char html_response[2048];
            sprintf(html_response,
                "<body style=\"background-color: %s; text-align: center;\">"
                "<h1>UDS Dynamic Color: %s</h1>"
                "</body>", 
                color, color);
            write(accp_sock, html_response, strlen(html_response));

        }
        close(accp_sock);   
    }
    close(listen_sock);
    return 0;

}