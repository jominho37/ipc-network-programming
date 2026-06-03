#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h> // 멀티 스레드 기능을 위해 추가.

#define MAXLINE 1024

void *read_and_response(void *arg);

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

    // 소켓 옵션 변경
    // 인자 : 소켓의 번호, 프로토콜 레벨 지정이면 SOL_SOCKET, 주소 재사용, 옵션값, 옵션값의 크기
    // SO_REUSEADDR : TIME_WAIT 상태를 없게 만들어준다.
    int set = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));

    // bind : 서버 주소 구조체와 소켓을 연결
    if(bind(listen_sock, (struct sockaddr *)&servaddr, sizeof(servaddr))<0){
        perror("bind fail");
        exit(0);
    }    
    listen(listen_sock, 5);

    // 멀티스레드 서버 : 클라이언트 연결마다 새 스레드를 생성하여 처리.
    while(1){
        // accept : 자신에게 오는 클라이언트의 연결 요청을 처리
        accp_sock = accept(listen_sock, (struct sockaddr *)&cliaddr, &addrlen);
        if(accp_sock < 0){
            perror("accept fail");
            exit(0);
        }

        pthread_t tid;

        pthread_create(&tid, NULL, read_and_response, (void *)(long)accp_sock);

        pthread_detach(tid);


        
    }
    close(listen_sock);
    return 0;
}

void *read_and_response(void *arg){
    int accp_sock = (int)(long)arg;

    // 헤더의 첫 번째 줄을 읽기.
    char read_line[MAXLINE];
    int index = 0;
    char one_char = '\0';

    // read_line : 헤더의 첫 번째 줄을 저장할 장소.
    // index : 몇 번째 문자를 읽고 있는지.
    // one_char : 문자 1바이트만 여기에 저장.
    while(index < sizeof(read_line) - 1){ //마지막에는 NULL을 넣어야 되니까  '-1'.
        int success_flag = read(accp_sock, &one_char, 1); // 1바이트씩 읽기
        if(success_flag<=0) break;
        read_line[index++] = one_char;
        if(one_char == '\n') break;
    }
    read_line[index] = '\0'; //문자열 끝 처리

    // 아무것도 못 읽은 경우, 소켓을 닫고 다시 while 루프를 돈다.
    if(index <= 0) {
        close(accp_sock);
        return NULL;
    }

    // method와 url 분리하기
    // sscanf를 했는데 method와 url에 값이 안 들어가진 상황에서 쓰레기값이 들어있을 수 있기에 {0}을 넣어줌.
    // 한 바이트씩 적고 '\0'을 붙여주는 거 아니면 {0}가 맞다.
    char method[10] = {0};
    char url[256] = {0};
    char path[512] = {0};

    // sscanf는 'GET /index.html HTTP/1.1' 이런 문자열에서 'GET' '/index.html'등을 추출함.
    sscanf(read_line, "%s %s", method, url);

    //strcmp는 문자열 url이 "/"와 같은지 확인하고 같다면 0을 출력함.
    if(strcmp(url, "/") == 0){
        strcpy(url, "/index.html"); // strcpy는 url에 "/index.html" 문자열을 대입함.
    }

    sprintf(path, "htdocs%s", url); // 화면이 아닌 문자열 상자에 출력하겠다. "htdocs/index.html"을 출력.

    while(1) {
        char rest_line[MAXLINE] = {0};
        int index = 0;
        char one_char = '\0';

        // rest_line의 크기를 넘지 않는 선에서 while을 돈다.
        while(index < sizeof(rest_line) - 1) {
            // 문자 하나 읽음.
            int success_flag = read(accp_sock, &one_char, 1);
            // 읽는데 실패한 경우
            if(success_flag <= 0) break;
            // 문자 하나를 rest_line에 넣음.
            rest_line[index++] = one_char;
            // 줄바꿈 문자열인 경우 해당 while 루프 종료
            if(one_char == '\n') break;
        }
        // 문자열의 끝에 NULL을 넣어서 끝을 알림.
        rest_line[index] = '\0';
        if(index <= 0) break;
        // 리눅스 터미널로 접속하면 "\n", 일반 브라우저로 접속하면 "\r\n"
        // 그렇기에 둘 다 확인한 후에 rest_line에 해당 문자열만 있다면 헤더 끝으로 간주하고 종료
        if (strcmp(rest_line, "\n") == 0 || strcmp(rest_line, "\r\n") == 0) {
            break;
        }
    }

    // htdocs 폴더 안의 index.html 파일을 읽기 전용으로 연다.
    FILE *fp = fopen(path, "r");
    // 파일이 존재한다면
    if(fp) {
        // HTTP response 헤더 : 200 상태코드 및 내용 타입(html) 전달
        char header[] =
            "HTTP/1.0 200 OK"
            "\r\nContent-Type: text/html\r\n"
            "\r\n";
        write(accp_sock, header, strlen(header));
        // 512바이트짜리 임시 바구니
        char file_buf[512];
        // fp에서 512바이트만큼 읽은 후 전달 -> 성공하면 주소, 실패하면 NULL
        while(fgets(file_buf, sizeof(file_buf), fp)) {
            write(accp_sock, file_buf, strlen(file_buf));
        }
        // 파일 닫기
        fclose(fp);
    }
    else{
        char header_404[] =
            "HTTP/1.0 404 NOT FOUND\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<html><body><h1>404 Not Found</h1></body></html>";
        write(accp_sock, header_404, strlen(header_404));
    }

    close(accp_sock);
    return NULL;
}