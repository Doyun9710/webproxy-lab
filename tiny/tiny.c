/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(char *method, int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(char *method, int fd, char*filename, char *cgiargs)
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void echo(int connfd);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    // 반복적 연결 요청 접수
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // 숙제 11.6 A
    // echo(connfd);

    // 트랜잭션 수행
    doit(connfd);
    // 자신 쪽의 연결 close
    Close(connfd);
  }
}

// 숙제 11.6 A
// telnet 43.200.183.216 5000
// transmit -> hi, receive -> hi
void echo(int connfd) {
  size_t n;
  char buf[MAXLINE];
  rio_t rio;
  Rio_readinitb(&rio, connfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    if (strcmp(buf, "\r\n") == 0)
      break;
    printf("server received %d bytes\n", (int)n);
    Rio_writen(connfd, buf, n);
  }
}

void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  /* 요청 라인을 읽고 분석. Rio_readinitb */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  /*
  Tiny는 GET 메소드만 지원.
  만일 클라이언트가 다른 메소드(POST 같은)를 요청하면, 
  에러 메시지를 보내고,
  main 루틴으로 돌아오고,
  */ 
  // 숙제 11.11
  // strcasecmp() 함수는 대소문자를 구분하지 않고 string1 및 string2를 비교한다.
  // string1 및 string2의 모든 영문자는 비교 전에 소문자로 변환한다.
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  // else if (strcasecmp(method, "HEAD")) {
  //   fd = NULL;
  // }

  // 연결을 닫고 다음 연결 요청을 기다린다.
  // 그렇지 않으면 읽어들이고, 다른 요청헤더들을 무시한다.
  read_requesthdrs(&rio); /* ignore request header */
  
  /* Parse URI from GET request */
  // URI를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고,
  // 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다.
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

// 만약 요청이 정적 컨텐츠를 위한 것이라면,
// 이 파일이 보통 파일인지, 읽기 권한을 가지고 있는지 검사
  if(is_static){ /* serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    // 만일 그렇다면 정적 컨텐츠를 클라이언트에게 제공
    // serve_static(fd, filename, sbuf.st_size);
    serve_static(method, fd, filename, sbuf.st_size);
  }
  else{
    /* serve dynamic content */
    // 동적 컨텐츠인지 검사
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    // 동적 컨텐츠 제공
    serve_dynamic(fd, filename, cgiargs);
  }
}

/*
HTTP 응답을 응답 라인에 적절한 상태 코드와 상태 메시지와 함께 클라이언트에 보내며,
브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML 파일도 함께 보낸다.
HTML 응답은 본체에서 컨텐츠의 크기와 타입을 나타내야 한다는 점을 기억하라.
HTML 컨텐츠를 한 개의 스트링으로 만드는 선택 덕분에 그 크기를 쉽게 결정할 수 있다.
*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor= " "ffffff" ">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/*
Tiny 는 요청 헤더 내의 어떤 정보도 사용하지 않는다.
read_requesthdrs 함수를 호출해서 이들을 읽고 무시한다.
요청 헤더를 종료하는 빈 텍스트 줄이 6번 줄에서 체크하고 있는 
carriage return 과 line feed 쌍으로 구성되어 있다는 점에 주목하라.
*/
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
스트링 cgi-bin 을 포함하는 모든 URI 는 동적 컨텐츠를 오청하는 것을 나타낸다고 가정한다.
기본 파일 이름은 .home.html 이다.
URI 를 파일 이름과 옵션으로 CGI 인자 스트링을 분석한다.
*/
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  // 요청이 정적 컨텐츠를 위한 것이라면 
  if (!strstr(uri, "cgi-bin")) {
    // CGI 인자 스트링을 지우고
    strcpy(cgiargs, "");

    // URI 를 ./index.html 같은 상대 리눅스 경로이름으로 변환한다.
    strcpy(filename, ".");
    strcat(filename, uri);

    // 만일 URI 가 '/' 문자로 끝난다면, 기본 파일 이름을 추가
    if(uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  // 요청이 동적 컨텐츠를 위한 것이라면 
  else {
    /* 모든 CGI 인자들을 추출 (~ else strcpy(cgiargs, "");) */
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");

    /* 나머지 URI 부분을 상대 리눅스 파일 이름으로 변환한다. */
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/*
지역 파일의 내용을 포함하고 있는 본체를 갖는 HTTP 응답을 보낸다.
*/
void serve_static(char *method, int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  // 파일 이름의 접미어 부분을 검사해서 파일 타입을 결정
  get_filetype(filename, filetype);

  /*
  클라이언트에 응답 줄과 응답 헤더를 보낸다.
  빈 줄 한 개가 헤더를 종료하고 있다.
  */
  // 숙제 11.6 C
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  // sprintf(buf, "version : %s 200 OK\r\n", version);
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));

  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "HEAD") == 0)
    return;
  
  /* Send response body to client */
  // 읽기 위해 filename 을 오픈하고 식별자를 얻어온다.
  srcfd = Open(filename, O_RDONLY, 0);
  // 리눅스 mmap 함수는 요청한 파일을 가상메모리 영역으로 매핑한다.
  // mmap : 파일 srcfd 의 첫 번째 filesize 바이트를 
  //        주소 srcp 에서 시작하는 사적 읽기-허용 가상메모리 영역으로 매핑한다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // // 파일을 메모리로 매핑한 후 식별자는 필요 없으므로 close. 메모리 누수 방지.
  // Close(srcfd);
  // // 클라이언트에 파일 전송
  // // rio_writen : 주소 srcp 에서 시작하는 filesize 바이트를 클라이언트의 연결 식별자로 복사한다.
  // Rio_writen(fd, srcp, filesize);
  // // 매핑된 가상메모리 주소를 반환한다. 메모리 누수 방지.
  // Munmap(srcp, filesize);

  // 숙제 11.9
  srcp = malloc(sizeof(char *) * filesize);
  // void rio_readinitb(rio_t, *rp, int fd) 함수는 식별자 fd를 주소 rp에 위치한 rio_t 타입의 읽기 버퍼와 연결한다.
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  free(srcp);
}

/* get_filetype - Derive file type from filename */
void get_filetype(char *filename, char *filetype) {
  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  // 숙제 11.7
  // mpg 파일 미실행. mp4 가능. 왜???
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
 }

/*
Tiny 는 자식 프로세스를 fork 하고,
그 후에 CGI 프로그램을 자식의 컨텍스트에서 실행 하며
모든 종류의 동적 컨텐츠를 제공한다.
*/
/*
클라이언트에 성공을 알려주는 응답 라인을 보내는 것으로 시작
CGI 프로그램은 응답의 나머지 부분을 보내야 한다.
*/
void serve_dynamic(char *method, int fd, char*filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  // 응답의 첫 부분을 보낸다.
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (strcasecmp(method, "HEAD") == 0)
    return;

  // 새로운 자식 프로세스를 fork 한다.
  if(Fork() == 0){
    /* Real server would set all CGI vars here */
    // 자식은 QUERY_STRING환경변수를 요청 URI의 CGI 인자들로 초기화
    setenv("QUERY_STRING", cgiargs, 1);

    // 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정
    Dup2(fd, STDOUT_FILENO);

    // CGI 프로그램을 로드하고 실행
    Execve(filename, emptylist, environ);
  }
  // 부모는 자식이 종료되어 정리되는 것을 기다리기 위해 Wait 함수에서 블록
  Wait(NULL);
}