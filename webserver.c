#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define MAXLINE 127

int main(void) {

    // 포트번호는 4000번으로 코드에 하드코딩함.
    u_short port = 4000;
    struct sockaddr_in servaddr, cliaddr;
    int listen_sock, accp_sock, addrlen = sizeof(cliaddr), nbyte;
    char buf[MAXLINE+1];
    
    if((listen_sock = socket(PF_INET, SOCK_STREAM, 0))<0){ // TCP 소켓 생성
        perror("socket fail");
        exit(0);
    }

    // 서버 주소 구조체(sockaddr_in) 초기화
    bzero((char *)&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    // bind : 서버 주소 구조체와 소켓을 연결
    if(bind(listen_sock, (struct sockaddr *)&servaddr, sizeof(servaddr))<0){
        perror("bind fail");
        exit(0);
    }    
    listen(listen_sock, 5);

    //iterative 서버 : 멀티스레드나 멀티프로세스 방식이 아님.
    while(1){
        // accept : 자신에게 오는 클라이언트의 연결 요청을 처리
        accp_sock = accept(listen_sock, (struct sockaddr *)&cliaddr, &addrlen);
        if(accp_sock < 0){
            perror("accept fail");
            exit(0);
        }
        // 어차피 htdocs/index.html만 전달하기에 buf안의 내용을 분석할 필요없음.
        nbyte = read(accp_sock, buf, MAXLINE); // 클라이언트에서 보낸 데이터를 읽음
        
        if(nbyte <= 0){
            close(accp_sock);
            continue;
        }
        buf[nbyte] = '\0';

        // HTTP response 헤더 : 200 상태코드 및 내용 타입(html) 전달
        char header[] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";
        write(accp_sock, header, strlen(header));
        // htdocs 폴더 안의 index.html 파일을 읽기 전용으로 연다.
        FILE *fp = fopen("htdocs/index.html", "r");
        // 파일이 존재한다면
        if(fp) {
            // 512바이트짜리 임시 바구니
            char file_buf[512];
            // fp에서 512바이트만큼 읽은 후 전달 -> 성공하면 주소, 실패하면 NULL
            while(fgets(file_buf, sizeof(file_buf), fp)) {
                write(accp_sock, file_buf, strlen(file_buf));
            }
            // 파일 닫기
            fclose(fp);
        }


        close(accp_sock);
    }
    close(listen_sock);
    return 0;
}