#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>

#define UDS_PATH "/tmp/dynamic_html_create.sock"
#define MAXLINE 1024
#define WORKER_NUM 3

// 워커 상태를 공유메모리로 관리하기 위한 구조체
// WORKER_IDLE : 작업 대기 중, WORKER_BUSY : 작업 처리 중, WORKER_DEAD : 워커 종료됨
typedef struct {
    int status[WORKER_NUM];  // 0 = IDLE, 1 = BUSY, 2 = DEAD
    sem_t sem;               // 공유메모리 접근용 세마포어
} shared_state_t;

#define WORKER_IDLE 0
#define WORKER_BUSY 1
#define WORKER_DEAD 2

// 시그널 핸들러에서 접근해야 하므로 전역 변수로 선언
int g_listen_sock;
int g_manager_fd[WORKER_NUM];
pid_t g_pids[WORKER_NUM];
shared_state_t *g_shared; // 공유메모리 포인터

// SIGCHLD 시그널 핸들러
// 워커 프로세스가 비정상 종료되면 해당 워커의 상태를 WORKER_DEAD로 변경한다.
void sigchld_handler(int sig){
    int status;
    pid_t pid;
    // waitpid에 WNOHANG : 종료된 자식이 없으면 즉시 리턴 (블로킹 방지)
    // 여러 자식이 동시에 죽을 수 있으므로 while로 반복
    while((pid = waitpid(-1, &status, WNOHANG)) > 0){
        for(int i = 0; i < WORKER_NUM; i++){
            if(g_pids[i] == pid){
                g_shared->status[i] = WORKER_DEAD;
                g_pids[i] = 0;
                break;
            }
        }
    }
}

// SIGINT, SIGTERM 시그널 핸들러
// 매니저가 종료 시그널을 받으면 워커들을 정리하고 소켓 파일을 삭제한다.
void cleanup_handler(int sig){
    // 워커 프로세스들에게 SIGTERM 전송 후 종료 대기
    for(int i = 0; i < WORKER_NUM; i++){
        if(g_pids[i] > 0){
            kill(g_pids[i], SIGTERM);
        }
    }
    for(int i = 0; i < WORKER_NUM; i++){
        if(g_pids[i] > 0){
            waitpid(g_pids[i], NULL, 0);
        }
    }

    // 매니저-워커 간 socketpair 닫기
    for(int i = 0; i < WORKER_NUM; i++){
        close(g_manager_fd[i]);
    }

    // 리슨 소켓 닫기 및 소켓 파일 삭제
    close(g_listen_sock);
    unlink(UDS_PATH);

    // 세마포어 파괴 및 공유메모리 해제
    sem_destroy(&g_shared->sem);
    munmap(g_shared, sizeof(shared_state_t));

    exit(0);
}

int main(void){
    struct sockaddr_un addr;

    // worker_fd[i] : 워커 i가 매니저로부터 fd를 받을 때 쓰는 소켓
    int worker_fd[WORKER_NUM];

    // 시그널 핸들러 등록
    signal(SIGINT, cleanup_handler);   // Ctrl+C
    signal(SIGTERM, cleanup_handler);  // kill 명령
    signal(SIGCHLD, sigchld_handler);  // 자식 프로세스 종료 감지

    // 공유메모리 생성 (mmap, MAP_SHARED로 부모-자식 간 공유)
    g_shared = mmap(NULL, sizeof(shared_state_t),
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(g_shared == MAP_FAILED){
        perror("mmap fail");
        exit(1);
    }

    // 공유메모리 초기화 : 모든 워커를 IDLE 상태로 설정
    for(int i = 0; i < WORKER_NUM; i++){
        g_shared->status[i] = WORKER_IDLE;
    }

    // 세마포어 초기화 (pshared=1 : 프로세스 간 공유, 초기값=1)
    if(sem_init(&g_shared->sem, 1, 1) < 0){
        perror("sem_init fail");
        exit(1);
    }

    // 기존 소켓 삭제.
    unlink(UDS_PATH);

    // UDS 리슨 소켓 생성.
    if((g_listen_sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0){
        perror("UDS socket fail");
        exit(1);
    }

    // 소켓 주소 구조체 초기화
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UDS_PATH, sizeof(addr.sun_path) - 1);

    // 소켓과 소켓 주소 구조체 묶기
    if(bind(g_listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("UDS bind fail");
        exit(1);
    }

    // 5개까지 대기.
    if(listen(g_listen_sock, 5) < 0){
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
        g_manager_fd[i] = pair[0]; // 매니저가 사용
        worker_fd[i] = pair[1];   // 워커가 사용

        pid_t pid = fork();
        if(pid < 0){
            perror("fork fail");
            exit(1);
        }

        if(pid == 0){
            // ===== 워커 프로세스 영역 =====

            // 워커는 매니저용 소켓과 리슨 소켓이 필요 없으므로 닫음.
            close(g_listen_sock);
            close(g_manager_fd[i]);
            // 다른 워커의 매니저 소켓도 닫음.
            for(int j = 0; j < i; j++){
                close(g_manager_fd[j]);
            }

            // 워커는 SIGINT를 무시 (매니저가 cleanup에서 SIGTERM을 보내므로)
            signal(SIGINT, SIG_IGN);

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

                // 워커 상태를 BUSY로 변경
                sem_wait(&g_shared->sem);
                g_shared->status[i] = WORKER_BUSY;
                sem_post(&g_shared->sem);

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

                // 워커 상태를 IDLE로 변경
                sem_wait(&g_shared->sem);
                g_shared->status[i] = WORKER_IDLE;
                sem_post(&g_shared->sem);
            }
            close(worker_fd[i]);
            exit(0);
        }

        // ===== 매니저 프로세스 영역 =====
        // 매니저는 워커용 소켓이 필요 없으므로 닫음.
        g_pids[i] = pid;
        close(worker_fd[i]);
    }

    // ===== 매니저 : DEAD 워커 재생성 + accept 후 IDLE 워커를 찾아 fd 전달 =====
    while(1){
        // DEAD 상태인 워커가 있으면 재생성
        for(int i = 0; i < WORKER_NUM; i++){
            if(g_shared->status[i] != WORKER_DEAD) continue;

            // 기존 socketpair 닫기
            close(g_manager_fd[i]);

            // 새 socketpair 생성
            int pair[2];
            if(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0){
                perror("socketpair fail (respawn)");
                continue;
            }
            g_manager_fd[i] = pair[0];

            pid_t pid = fork();
            if(pid < 0){
                perror("fork fail (respawn)");
                close(pair[0]);
                close(pair[1]);
                continue;
            }

            if(pid == 0){
                // ===== 재생성된 워커 프로세스 영역 =====
                close(g_listen_sock);
                // 모든 매니저 소켓 닫기
                for(int j = 0; j < WORKER_NUM; j++){
                    close(g_manager_fd[j]);
                }

                signal(SIGINT, SIG_IGN);

                int my_fd = pair[1];
                while(1){
                    struct msghdr msg = {0};
                    char dummy[1];
                    struct iovec iov;
                    iov.iov_base = dummy;
                    iov.iov_len = sizeof(dummy);
                    msg.msg_iov = &iov;
                    msg.msg_iovlen = 1;

                    char cmsg_buf[CMSG_SPACE(sizeof(int))];
                    msg.msg_control = cmsg_buf;
                    msg.msg_controllen = sizeof(cmsg_buf);

                    int n = recvmsg(my_fd, &msg, 0);
                    if(n <= 0) break;

                    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
                    int accp_sock;
                    memcpy(&accp_sock, CMSG_DATA(cmsg), sizeof(int));

                    sem_wait(&g_shared->sem);
                    g_shared->status[i] = WORKER_BUSY;
                    sem_post(&g_shared->sem);

                    char buf[MAXLINE];
                    int read_bytes_num = read(accp_sock, buf, sizeof(buf) - 1);
                    if(read_bytes_num > 0){
                        buf[read_bytes_num] = '\0';
                        char color[50] = "white";
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

                    sem_wait(&g_shared->sem);
                    g_shared->status[i] = WORKER_IDLE;
                    sem_post(&g_shared->sem);
                }
                close(my_fd);
                exit(0);
            }

            // 매니저 : 재생성 완료
            close(pair[1]);
            g_pids[i] = pid;

            sem_wait(&g_shared->sem);
            g_shared->status[i] = WORKER_IDLE;
            sem_post(&g_shared->sem);
        }

        // 클라이언트(webserver)와 통신하는 소켓 생성.
        int accp_sock = accept(g_listen_sock, NULL, NULL);
        if(accp_sock < 0){
            continue;
        }

        // 공유메모리에서 IDLE 상태인 워커를 찾는다.
        int target = -1;
        while(target == -1){
            sem_wait(&g_shared->sem);
            for(int i = 0; i < WORKER_NUM; i++){
                if(g_shared->status[i] == WORKER_IDLE){
                    target = i;
                    g_shared->status[i] = WORKER_BUSY; // 선택한 워커를 미리 BUSY로 변경
                    break;
                }
            }
            sem_post(&g_shared->sem);
            // IDLE 워커가 없으면 잠시 대기 후 재탐색
            if(target == -1) usleep(1000);
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

        // IDLE 워커에게 fd 전송
        sendmsg(g_manager_fd[target], &msg, 0);

        // 매니저는 fd를 워커에게 넘겼으므로 자신의 accp_sock은 닫음.
        close(accp_sock);
    }

    close(g_listen_sock);
    return 0;
}
