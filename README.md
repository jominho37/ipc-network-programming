## 🎯 Git Commit Convention

커밋 메시지는 아래의 핵심 이모지와 형식을 준수하여 작성합니다.

| 이모지 | 타입 | 설명 |
|:---:|:---:|:---|
| ✨ | Feat | 새로운 기능 추가 및 구현 |
| 🐛 | Fix | 버그 수정 및 해결 |
| 🎨 | Design | UI 디자인 변경 / CSS, JSP 화면 수정 |
| ♻️ | Refactor | 코드 구조 개선 / 리팩토링 |
| 🔧 | Chore | 빌드 설정, 라이브러리 추가, 환경설정 수정 |
| 📝 | Docs | README 등 문서 수정 및 추가 |

---

# Version 1 : 정적 웹 서버 만들기

C언어 소켓 프로그래밍을 이용하여 간단한 정적 웹 서버를 구현한다.

- Iterative 방식 (멀티스레드/멀티프로세스 없이 순차 처리)
- 클라이언트 요청 시 `htdocs/index.html`을 응답으로 전달
- HTTP 요청 파싱 : method와 URL을 분리하여 경로별 파일 제공
- `SO_REUSEADDR` 옵션으로 서버 재시작 시 포트 즉시 재사용 가능
- 파일이 없으면 `404 Not Found` 응답

---

# Version 2 : 멀티스레드 서버 만들기

Iterative 방식에서 `pthread`를 이용한 멀티스레드 방식으로 전환한다.

- `pthread_create`로 클라이언트 연결마다 새 스레드(`read_and_response`) 생성
- `pthread_detach`로 스레드 자원 자동 회수
- 동시에 여러 클라이언트 요청 처리 가능

---

# Version 3 : UDS를 이용한 동적 HTML 처리가 가능한 서버 만들기

URL에 쿼리 스트링(`?`)이 포함된 경우, UDS(Unix Domain Socket)를 통해 `web_app_server`에 동적 HTML 생성을 요청한다.

- `webserver`는 UDS 클라이언트로서 `web_app_server`에 URL을 전달
- `web_app_server`는 UDS 서버로서 쿼리 스트링에서 `color=` 값을 추출하여 배경색이 적용된 동적 HTML 생성
- UDS 소켓 경로 : `/tmp/dynamic_html_create.sock`
- UDS 연결 실패 시 `500 Internal Server Error` 응답

---

# Version 4 : Prefork 워커 프로세스 풀 방식으로 전환

`web_app_server`를 매니저 + 워커 3개의 prefork 프로세스 풀 구조로 전환한다.

- 매니저 프로세스가 UDS `accept` 후 `socketpair` + `SCM_RIGHTS`로 워커에게 클라이언트 fd를 전달
- 워커 프로세스 3개를 서버 시작 시 미리 `fork` (prefork)
- 라운드 로빈 방식으로 워커를 선택하여 작업 분배
- 워커는 전달받은 fd로 URL을 읽고 동적 HTML을 생성하여 응답
- 시그널 처리 및 cleanup
  - `SIGINT`(Ctrl+C) / `SIGTERM` 수신 시 매니저가 워커에게 `SIGTERM` 전송 후 `waitpid`로 종료 대기
  - socketpair, 리슨 소켓 닫기 및 UDS 소켓 파일(`/tmp/dynamic_html_create.sock`) 삭제
  - 워커는 `SIGINT`를 무시하고 매니저의 `SIGTERM`으로만 종료

---

# Version 5 : 공유메모리 + 세마포어 기반 워커 상태 관리 및 자동 재생성

라운드 로빈 대신 공유메모리(`mmap`) + 세마포어(`sem_t`)로 워커 상태를 관리하고, 죽은 워커를 자동 재생성한다.

- 워커 상태 정의 : `WORKER_IDLE(0)` / `WORKER_BUSY(1)` / `WORKER_DEAD(2)`
- 공유메모리(`mmap`, `MAP_SHARED | MAP_ANONYMOUS`)에 워커 상태 배열 + 세마포어를 저장
- 매니저가 `accept` 후 세마포어로 공유메모리를 보호하면서 `WORKER_IDLE` 워커를 탐색하여 fd 전달
- `SIGCHLD` 핸들러 : 워커 비정상 종료 시 `waitpid(WNOHANG)`로 감지하여 `WORKER_DEAD`로 기록
- 매니저 루프에서 `WORKER_DEAD` 워커를 발견하면 새 `socketpair` + `fork()`로 자동 재생성
- cleanup 시 세마포어 파괴(`sem_destroy`) 및 공유메모리 해제(`munmap`) 추가

## 프로젝트 구조

```
network_assignment/
├── webserver.c          # 멀티스레드 웹 서버 (UDS 클라이언트)
├── web_app_server.c     # UDS 기반 동적 HTML 생성 서버 (매니저 + 워커 3개)
├── Makefile             # 빌드 설정 파일
├── htdocs/
│   └── index.html       # 클라이언트에 전달할 HTML 파일
├── HTTP_request.md      # HTTP 요청/응답 구조 및 예시
└── README.md
```

## HTTP 요청/응답

HTTP 요청 및 응답의 구조, 상태 코드, 정적/동적 예시는 [HTTP_request.md](HTTP_request.md)를 참고한다.

## 컴파일 및 실행

```bash
make
./web_app_server &
./webserver
```

- `make` : `webserver`와 `web_app_server` 모두 컴파일
- `web_app_server`를 먼저 백그라운드로 실행한 뒤 `webserver`를 실행
- 브라우저에서 `http://localhost:4000` 접속 → 정적 HTML 응답
- 브라우저에서 `http://localhost:4000/?color=red` 접속 → 동적 HTML 응답 (배경색 변경)
