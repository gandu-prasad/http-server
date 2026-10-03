#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>


#define MAX_HEADERS 64
#define MAX_SEGMENTS 16
#define CT_TEXT "text/plain"
#define CT_HTML "text/html"


typedef struct {
  char *method;
  char *uri;
  char *version;
  char *header_names[MAX_HEADERS];
  char *header_values[MAX_HEADERS];
  int header_count;
  char *body;
} http_request_t;

typedef struct {
  char name[MAX_SEGMENTS][16];
  char value[MAX_SEGMENTS][64];
  int count;
} route_params_t;

typedef void (*handler_t)(int fd, const http_request_t *req, const route_params_t *params);

typedef struct {
  char *method;
  char *pattern;
  handler_t handler;
} route_t;

typedef enum {
  HTTP_OK = 200,
  HTTP_BAD_REQUEST = 400,
  HTTP_NOT_FOUND = 404,
  HTTP_METHOD_NOT_ALLOWED = 405,
  HTTP_INTERNAL_ERROR = 500
} http_status_t;

static const char *http_status_text(http_status_t status)
{
  switch (status) {
    case HTTP_OK: return "OK";
    case HTTP_BAD_REQUEST: return "Bad Request";
    case HTTP_NOT_FOUND: return "Not Found";
    case HTTP_METHOD_NOT_ALLOWED: return "Method Not Allowed";
    case HTTP_INTERNAL_ERROR: return "Internal Server Error";
  }
  return "Unknown";
}

static void send_response(int fd, http_status_t status, const char *ctype, const char *body)
{
  char head[256];
  int hn = snprintf(head, sizeof(head),
      "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n",
      (int)status, http_status_text(status), ctype, strlen(body));
  if (hn > 0) (void)write(fd, head, (size_t)hn);
  (void)write(fd, body, strlen(body));
}


int http_parse_request(char *raw, http_request_t *req) {

  memset(req, 0, sizeof(*req));
  char *body_start = strstr(raw, "\r\n\r\n");

  if(body_start) {
    *body_start = '\0';
    req->body = body_start + 4;
  }

  char *saveptr = NULL;
  char *line = strtok_r(raw, "\r\n", &saveptr);
  if(!line) return -1;

  char *lsave = NULL;
  req->method = strtok_r(line, " ", &lsave);
  req->uri = strtok_r(NULL, " ", &lsave);
  req->version = strtok_r(NULL, " ", &lsave);

  if(!req->method || !req->uri || !req->version) return -1;

  if(strncmp(req->version, "HTTP/1.", 7) != 0) return -1;

  while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
    if(req->header_count >= MAX_HEADERS) break;

    char *hsave = NULL;
    char *name = strtok_r(line, ":", &hsave);
    char *value = strtok_r(NULL, "", &hsave);

    if(!name) continue;

    while(value && *value == ' ') value++; // we need to skip mandatory space after ':' per RFC 1945

    req->header_names[req->header_count] = name;
    req->header_values[req->header_count] = value;
    req->header_count++;
  }
  return 0;
}

static route_t *routes = NULL;
static int route_count = 0;
static int route_capacity = 0;

int route_add(const char *method, const char *pattern, handler_t handler)
{
  if(route_count == route_capacity) {
    int new_cap = route_capacity ? route_capacity * 2 : 8;
    route_t *tmp = realloc(routes, new_cap * sizeof(route_t));
    if(!tmp) return -1;
    routes = tmp;
    route_capacity = new_cap;
  }

  routes[route_count].method = strdup(method);
  routes[route_count].pattern = strdup(pattern);
  routes[route_count].handler = handler;

  if(!routes[route_count].method || !routes[route_count].pattern) return -1;

  route_count ++;
  return 0;
}

// :params setup 

static int match_pattern(const char *pattern, const char *path, route_params_t *params)
{
  params->count = 0;
  char pat[256],pth[256];
  strncpy(pat, pattern, sizeof(pat)-1);
  pat[sizeof(pat) - 1] = '\0';
  strncpy(pth, path, sizeof(pth) - 1);
  pth[sizeof(pth) - 1] = '\0';

  char *psave = NULL, *qsave = NULL;
  char *pseg = strtok_r(pat, "/", &psave);
  char *qseg = strtok_r(pth, "/", &qsave);

  while(pseg) {
    if(!qseg) return 0;

    if(pseg[0] == ':') {
      if(params->count >= MAX_SEGMENTS) return 0;
      strncpy(params->name[params->count], pseg+1, sizeof(params->name[0] - 1));
      params->name[params->count][sizeof(params->name[0]) - 1] = '\0';
      strncpy(params->value[params->count], qseg, sizeof(params->value[0]) - 1);
      params->value[params->count][sizeof(params->value[0]) - 1] = '\0';
      params->count++;
    } else if (strcmp(pseg,qseg) != 0) { return 0; }

    pseg = strtok_r(NULL,"/", &psave);
    qseg = strtok_r(NULL, "/", &qsave);
  }

  if(qseg) return 0; //root "/" handled naturally
  
  return 1;
}

//dispatcher

int route_dispatch(int fd, const http_request_t *req) {

  //strip query string first
  char path[512];
  strncpy(path, req->uri, sizeof(path) - 1);
  path[sizeof(path) - 1] = '\0';
  char *q = strchr(path, '?');
  if (q) { *q = '\0'; }  

  route_params_t params;
  memset(&params, 0, sizeof(params));
  
  for (int i = 0; i < route_count; i++) {
    if(strcmp(routes[i].method, req->method) != 0) { continue; }
    if(match_pattern(routes[i].pattern, path, &params)) {
      routes[i].handler(fd, req, &params);
      return 0;
    }
  }
  return -1; // 404 error
}

// catching params on the handler side

static void handle_user(int fd, const http_request_t *req, const route_params_t *params)
{
  (void)req;
  // look up the ":id" capture
  for (int i=0; i < params->count; i++) {
    if(strcmp(params->name[i], "id") == 0) {
      char body[128];
      snprintf(body, sizeof(body), "user id = %s\n", params->value[i]);
      // text/plain on purpose: echoing URL data to HMTL unescaped is an XSS risk
      send_response(fd, HTTP_OK, CT_TEXT,body);
      return;
    }
  }
  send_response(fd, HTTP_BAD_REQUEST, CT_TEXT, "user id not captured\n");
}

static void handle_users(int fd, const http_request_t *req, const route_params_t *params)
{
  (void)req;
  (void) params;
  send_response(fd, HTTP_OK, CT_TEXT, "users list\n");
}

static void handle_root(int fd, const http_request_t *req, const route_params_t *params)
{
  (void)req;
  (void)params;
  send_response(fd, HTTP_OK, CT_HTML, "<h1>Hello, World! </h1>");
}

int handle_client(int client_socket) {
  char buf[4096];

  printf("\n---\n");
  
  ssize_t n = read(client_socket, buf, sizeof(buf) - 1);

  if (n < 0) {
    perror("read(client");
    return -1;
  }
  if(n == 0) {
    printf("connection closed gracefully\n");
    return 0;
  }

  buf[n] = '\0';

  //print first: parsing modifies buf in place
  printf("Request:\n%s", buf);

  // req's pointers point into buf, so buf must outlive req (it does here)
  http_request_t req;
  if(http_parse_request(buf, &req) < 0) {
    send_response(client_socket, HTTP_BAD_REQUEST, CT_TEXT, "bad request\n");
    printf("\n---\n");
    return 0;
  }

  if (route_dispatch(client_socket, &req) < 0)
    send_response(client_socket, HTTP_NOT_FOUND, CT_TEXT, "not found\n");

  printf("\n---\n");
  return 0;
}

int main(void) {

    /* declare */
    int rc = 0;
    struct sockaddr_in bind_addr;
    int tcp_socket = 0;
    int ret = 0;
    int client_socket = 0;
    int enabled = true;

    // saves the server from killing if client hangs up mid-write
    signal(SIGPIPE, SIG_IGN);

    /* initialize */
    memset(&bind_addr, 0, sizeof(bind_addr));
    tcp_socket = socket(
        AF_INET, /* IPv4 */
        SOCK_STREAM, /* TCP */
        0 /* dont care */
    );

    if (tcp_socket < 0) {
        perror("socket()");
        return 1;
    }
    printf("socket creation succeeded\n");

    /* we dont really care if this fails */
    (void)setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    bind_addr.sin_port = htons(6969);
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    rc = bind(tcp_socket, (const struct sockaddr*)&bind_addr, sizeof(bind_addr));
    if (rc < 0) {
        perror("bind()");
        ret = 1;
        goto exit;
    }
    printf("bind succeeded\n");

    rc = listen(tcp_socket, SOMAXCONN);
    if (rc < 0) {
        perror("listen()");
        ret = 1;
        goto exit;
    }
    printf("listen succeeded\n");

    route_add("GET", "/", handle_root);
    route_add("GET", "/users", handle_users);
    route_add("GET", "/users/:id", handle_user);

    for (;;) {
        printf("waiting for connections...\n");
        client_socket = accept(tcp_socket, NULL, NULL);
        if (client_socket < 0) {
          perror("accept()");
          continue;
        }
        printf("got a connection!\n");
        handle_client(client_socket);
        close(client_socket);
    }

exit:
    close(tcp_socket);
    return ret;
}
