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

## 개요

- 클라이언트(브라우저)의 HTTP 요청을 받으면 `htdocs/index.html` 파일을 응답으로 전달하는 정적 웹 서버
- Iterative 방식 (멀티스레드/멀티프로세스 없이 순차 처리)
- 포트번호 : `4000`

## 프로젝트 구조

```
network_assignment/
├── webserver.c          # 웹 서버 소스 코드
├── htdocs/
│   └── index.html       # 클라이언트에 전달할 HTML 파일
├── HTTP_request.md      # HTTP 요청 메시지 예시
└── README.md
```

## 동작 흐름

1. **소켓 생성** : `socket(PF_INET, SOCK_STREAM, 0)`으로 TCP 소켓 생성
2. **bind** : 서버 주소 구조체(`sockaddr_in`)와 소켓을 바인딩 (포트 4000)
3. **listen** : 연결 요청 대기 (backlog = 5)
4. **accept** : 클라이언트 연결 수락
5. **read** : 클라이언트가 보낸 HTTP 요청 메시지 수신
6. **write** : HTTP 응답 헤더(`200 OK`) + `htdocs/index.html` 내용을 전송
7. **close** : 연결 종료 후 다시 accept 대기

## HTTP 요청 예시

```
GET / HTTP/1.0
Host: localhost:4000
Connection: keep-alive
User-Agent: Mozilla/5.0 ...
Accept: text/html,application/xhtml+xml,...
Accept-Encoding: gzip, deflate, br
Accept-Language: ko-KR,ko;q=0.9,en-US;q=0.8
(빈 줄)
```

## HTTP 응답

```
HTTP/1.0 200 OK
Content-Type: text/html

(index.html 내용)
```

## 컴파일 및 실행

```bash
gcc -o webserver webserver.c
./webserver
```

실행 후 브라우저에서 `http://localhost:4000` 으로 접속하면 `index.html` 페이지가 표시된다.
