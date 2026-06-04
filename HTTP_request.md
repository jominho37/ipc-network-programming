# HTTP 요청 (Request)

HTTP 요청 메시지는 **요청 라인 + 헤더 + 빈 줄**로 구성된다.

```
[method] [URL] [HTTP 버전]\r\n
[헤더]\r\n
\r\n
```

## 정적 페이지 요청 예시

`http://localhost:4000` 또는 `http://localhost:4000/index.html` 접속 시:

```
GET / HTTP/1.0
Host: localhost:4000
Connection: keep-alive
User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36...
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp...
Accept-Encoding: gzip, deflate, br
Accept-Language: ko-KR,ko;q=0.9,en-US;q=0.8
(빈 줄)
```

## 동적 페이지 요청 예시

`http://localhost:4000/?color=red` 접속 시:

```
GET /?color=red HTTP/1.0
Host: localhost:4000
Connection: keep-alive
User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36...
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp...
Accept-Encoding: gzip, deflate, br
Accept-Language: ko-KR,ko;q=0.9,en-US;q=0.8
(빈 줄)
```

> 정적 요청과의 차이점 : 요청 라인의 URL에 쿼리 스트링(`?color=red`)이 포함된다.

---

# HTTP 응답 (Response)

HTTP 응답 메시지는 **상태 라인 + 헤더 + 빈 줄 + 본문**으로 구성된다.

```
HTTP/1.0 [상태코드] [상태메시지]\r\n
Content-Type: text/html\r\n
\r\n
(본문)
```

## 주요 상태 코드

| 상태코드 | 상태메시지 | 설명 |
|:---:|:---:|:---|
| 200 | OK | 요청 성공. 요청한 리소스를 정상적으로 반환 |
| 404 | Not Found | 요청한 리소스가 서버에 존재하지 않음 |
| 500 | Internal Server Error | 서버 내부 오류 (UDS 연결 실패 등) |

## 정적 페이지 응답 예시 (200 OK)

클라이언트가 존재하는 파일을 요청했을 때:

```
HTTP/1.0 200 OK\r\n
Content-Type: text/html\r\n
\r\n
<!DOCTYPE html>
<html>
...index.html 내용...
</html>
```

## 동적 페이지 응답 예시 (200 OK)

쿼리 스트링이 포함된 요청 시, `web_app_server`가 생성한 동적 HTML을 반환:

```
HTTP/1.0 200 OK\r\n
Content-Type: text/html; charset=UTF-8\r\n
\r\n
<body style="background-color: red; text-align: center;">
<h1>UDS Dynamic Color: red</h1>
</body>
```

## 404 Not Found 응답 예시

클라이언트가 존재하지 않는 파일을 요청했을 때 (예: `GET /about.html`):

```
HTTP/1.0 404 Not Found\r\n
Content-Type: text/html\r\n
\r\n
<html><body><h1>404 Not Found</h1></body></html>
```

## 500 Internal Server Error 응답 예시

UDS 서버(`web_app_server`)에 연결할 수 없을 때:

```
HTTP/1.0 500 Internal Server Error\r\n
Content-Type: text/html; charset=UTF-8\r\n
\r\n
<html><body><h1>500 Internal Server Error</h1></body></html>
```
