#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define UDS_PATH "/tmp/dynamic_html_create.sock"
#define MAXLINE 1024
#define WORKER_NUM 3

int main(void){
    int listen_sock;
    struct sockaddr_un addr;

    // manager_fd[i] : 매니저가 워커 i에게 fd를 보낼 때 쓰는 소켓
    // worker_fd[i]  : 워커 i가 매니저로부터 fd를 받을 때 쓰는 소켓
    int manager_fd[WORKER_NUM];
    int worker_fd[WORKER_NUM];
    pid_t pids[WORKER_NUM];

    // 기존 소켓 삭제.
    unlink(UDS_PATH);

    // UDS 리슨 소켓 생성.
    if((listen_sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0){
        perror("UDS socket fail");
        exit(1);
    }

    // 소켓 주소 구조체 초기화
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UDS_PATH, sizeof(addr.sun_path) - 1);

    // 소켓과 소켓 주소 구조체 묶기
    if(bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("UDS bind fail");
        exit(1);
    }

    // 5개까지 대기.
    if(listen(listen_sock, 5) < 0){
        perror("UDS listen fail");
        exit(1);
    }

    // 워커 프로세스 3개를 미리 fork (prefork)
    for(int i = 0; i < WORKER_NUM; i++){
        // socketpair : 매니저-워커 간 fd 전달용 소켓 쌍 생성
        int pair[2];
        if(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0){
            perror("socketpair fail");
            exit(1);
        }
        manager_fd[i] = pair[0]; // 매니저가 사용
        worker_fd[i] = pair[1];  // 워커가 사용

        pid_t pid = fork();
        if(pid < 0){
            perror("fork fail");
            exit(1);
        }

        if(pid == 0){
            // ===== 워커 프로세스 영역 =====

            // 워커는 매니저용 소켓과 리슨 소켓이 필요 없으므로 닫음.
            close(listen_sock);
            close(manager_fd[i]);
            // 다른 워커의 매니저 소켓도 닫음.
            for(int j = 0; j < i; j++){
                close(manager_fd[j]);
            }

            // 워커는 무한 루프를 돌면서 매니저로부터 fd를 받아 처리.
            while(1){
                // ---- SCM_RIGHTS로 fd 수신 ----
                // msghdr : sendmsg/recvmsg에서 사용하는 메시지 구조체
                struct msghdr msg = {0};
                // iovec : 실제 데이터를 담는 버퍼 (더미 1바이트)
                char dummy[1];
                struct iovec iov;
                iov.iov_base = dummy;
                iov.iov_len = sizeof(dummy);
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;

                // cmsg : 제어 메시지 (fd를 담는 보조 데이터)
                // CMSG_SPACE : cmsg 헤더 + int(fd) 크기만큼 공간 확보
                char cmsg_buf[CMSG_SPACE(sizeof(int))];
                msg.msg_control = cmsg_buf;
                msg.msg_controllen = sizeof(cmsg_buf);

                // recvmsg로 매니저가 보낸 fd를 수신
                int n = recvmsg(worker_fd[i], &msg, 0);
                if(n <= 0) break; // 매니저가 종료하면 워커도 종료

                // 제어 메시지에서 fd 꺼내기
                // CMSG_FIRSTHDR : 첫 번째 제어 메시지 헤더를 가져옴
                struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
                int accp_sock;
                // CMSG_DATA : 제어 메시지의 데이터(fd) 위치를 가리킴
                memcpy(&accp_sock, CMSG_DATA(cmsg), sizeof(int));

                // ---- 동적 HTML 처리 ----
                char buf[MAXLINE];
                int read_bytes_num = read(accp_sock, buf, sizeof(buf) - 1);
                if(read_bytes_num > 0){
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
            close(worker_fd[i]);
            exit(0);
        }

        // ===== 매니저 프로세스 영역 =====
        // 매니저는 워커용 소켓이 필요 없으므로 닫음.
        pids[i] = pid;
        close(worker_fd[i]);
    }

    // ===== 매니저 : accept 후 라운드 로빈으로 워커에게 fd 전달 =====
    int current_worker = 0;

    while(1){
        // 클라이언트(webserver)와 통신하는 소켓 생성.
        int accp_sock = accept(listen_sock, NULL, NULL);
        if(accp_sock < 0){
            continue;
        }

        // ---- SCM_RIGHTS로 fd 송신 ----
        struct msghdr msg = {0};
        // 더미 데이터 1바이트 (sendmsg는 최소 1바이트의 실제 데이터가 필요)
        char dummy[1] = {'F'};
        struct iovec iov;
        iov.iov_base = dummy;
        iov.iov_len = sizeof(dummy);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        // 제어 메시지에 accp_sock(fd)를 담아서 전송
        char cmsg_buf[CMSG_SPACE(sizeof(int))];
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;            // fd 전송 타입
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));  // cmsg 헤더 + int(fd) 크기
        memcpy(CMSG_DATA(cmsg), &accp_sock, sizeof(int));

        // 라운드 로빈으로 워커 선택 후 fd 전송
        sendmsg(manager_fd[current_worker], &msg, 0);
        current_worker = (current_worker + 1) % WORKER_NUM;

        // 매니저는 fd를 워커에게 넘겼으므로 자신의 accp_sock은 닫음.
        close(accp_sock);
    }

    close(listen_sock);
    return 0;
}
