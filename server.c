#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>


#define MAX_HEADERS 64


typedef struct {
  char *method;
  char *uri;
  char *version;
  char *header_names[MAX_HEADERS];
  char *header_values[MAX_HEADERS];
  int header_count;
  char *body;
} http_request_t;

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

int handle_client(int client_socket) {
    ssize_t n = 0;
    char buf[1024];
    const char* hello = "HTTP/1.0 200 OK\r\n\r\n<h1>Hello, World!</h1>";

    printf("\n---\n");
    for (;;) {
        memset(buf, 0, sizeof(buf));

        n = read(client_socket, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read(client)");
            return -1;
        }
        if (n == 0) {
            printf("connection closed gracefully!\n");
            break;
        }

        printf("REQUEST:\n%s", buf);
        (void)write(client_socket, hello, strlen(hello));
        close(client_socket);
        break;
    }
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

    for (;;) {
        printf("waiting for connections...\n");
        client_socket = accept(tcp_socket, NULL, NULL);

        printf("got a connection!\n");
        rc = handle_client(client_socket);
        /* ignore errors, dont care for now */
    }

exit:
    close(tcp_socket);
    return ret;
}
