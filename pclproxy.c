#include <stdio.h>
#include <sys/types.h>
#inlcude <sys/socket.h>
#include <netinet/in.h>

struct sockinfo {
  char buffer[1024];
  int buf_pos;
  int buf_len;
  struct sockaddr_in addr;
  int fd;
};

struct connection {
  int client_fd;
  struct sockbuf client
}

void
run_proxy(int client_fd, const char* connect_host, int connect_port)
{
  struct sockbuf client, server;
  struct sockaddr_in addr;
  int server_fd=-1;
  fd_set read_set, write_set;
  int nfds = client_fd;

  snd.pos = rcv.pos = snd.len = rcv.len = 0;

  if((server_fd=socket(AF_INET, SOCK_STREAM, 0))<0) {
    perror("socket");
    goto done;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(connect_host);
  addr.sin_port = htons(connect_port);

  while(true) {
    FD_ZERO(&read_set);
    FD_SET( client_fd, &read_set );
    if(server_fd>=0) FD_SET( server_fd, &read_set );

    FD_ZERO(&write_set);
    if(client.len>0) FD_SET( client_fd, &write_set );
    if(server.len>0) FD_SET( server_fd, &write_set );

    if (select(nfds+1, &read_fd_set, &write_fd_set, NULL, NULL)<0) {
      perror("select");
      goto done;
    }

    if(FD_ISSET())


  }


  if(connect(server_fd, (struct sockaddr *)&addr, sizeof(addr))<0) {
    perror("connect");
    goto done;
  }



 done:
  close(client_fd);
  if(server_fd>0) close(server_fd);
}

void
run_server(const char* path_to_self, int listen_port, const char* connect_host, int connect_port)
{
  int listen_fd=-1;
  struct sockaddr_in listen_addr;

  if((listen_fd=socket(AF_INET, SOCK_STREAM, 0))<0) {
    perror("socket");
    goto done;
  }

  listen_addr.sin_family = AF_INET;
  listen_addr.sin_port = htons(port);
  listen_addr.sin_addr.s_addr = INADDR_ANY;

  if(bind(listen_fd, (struct sockaddr *) &listen_addr, sizeof(listen_addr)) < 0) {
    perror("bind");
    goto done;
  }

  listen(listen_fd, 5);

  while(1) {
    struct sockaddr_in accept_addr;
    int len, accept_fd;
    if((accept_fd=accept(listen_fd, (struct sockaddr *)&accept_addr, &len))<0) {
      perror("accept");
      goto done;
    }

    switch(vfork()) {
    case -1:
      perror("vfork");
      goto done;
    case 0: // child
      close(listen_fd);
      {
        char fd_buf[16], port_buf[16];
        char* args[5] = { "child", fd_buf, connect_host, port_buf };
        sprintf( fd_buf, "%d", accept_fd );
        sprintf( port_buf, "%d", connect_port );
        execve( path_to_self, &args, NULL );
      }
      abort();
    default:
      close(accept_fd);
    }
  }

 done:
  if(listen_fd>=0)
    close(listen_fd);
}

int
main(int argc, char *argv[])
{
  if (strcmp(argv[0], "child")) { // child socketfd connecthost connectport
    run_proxy(atoi(argv[1]), argv[2], atoi(argv[3]));
  } else if (argc == 5) { // pclproxy listenport connecthost connectport
    while(true) {
      run_server(argv[0], atoi(argv[1]), argv[2], atoi(argv[3]));
      sleep(5);
    }
  }
}
