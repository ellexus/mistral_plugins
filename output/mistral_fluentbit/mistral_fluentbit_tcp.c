

#include <sys/epoll.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <pthread.h>

#include "mistral_fluentbit_tcp.h"
#include "mistral_plugin.h"

#define MISTRAL_EPOL_TIMEOUT_INTERVAL 500 /* This 500 milliseconds */

#define MISTRAL_TCP_RECONNECT_TRYOUTS 3 /**/
#define MISTRAL_TIMEOUT_BETWEEN_RECONNECTS 500000 /*ms*/
#define MISTRAL_TCP_BUFFER_SIZE 1024
#define MISTRAL_FLUENT_BIT_SERVER_NAME_SIZE 128

static void *mistral_tcp_connection_thread(void *arg);


typedef struct {
    char server_name[MISTRAL_FLUENT_BIT_SERVER_NAME_SIZE];
    int port;
    mistral_fluentbit_tcp_f cb;
    mistral_fluentbit_tcp_ctx_s *ctx;
    void *data;
        
} mistral_fluentbit_thread_cb_s;

void mistral_fluentbit_init(mistral_fluentbit_tcp_ctx_s *ctx) {

    pthread_mutex_init(&ctx->mutex, NULL);
    ctx->fd = -1;
    ctx->state = intialized;
    return;
}

int mistral_fluentbit_connect(mistral_fluentbit_tcp_ctx_s *ctx, const char *servername, int port, mistral_fluentbit_tcp_f func,
                      void *args) {

    mistral_err("mistral_fluentbit_connect\n");
    int ret = -1;
    pthread_t thread_id;

    if (strlen(servername) >= MISTRAL_FLUENT_BIT_SERVER_NAME_SIZE - 1) {
        mistral_err("Server name too long!\n");
        
        /* This is thread safe because the connectivity thread hasn#t been created yet*/
        ctx->state = failed;
        return (-1);
    }

    mistral_fluentbit_thread_cb_s *ptr_data = (mistral_fluentbit_thread_cb_s *)malloc(
        sizeof(mistral_fluentbit_thread_cb_s));
    if (ptr_data == NULL) {

        /* This is thread safe because the connectivity thread hasn't been created yet*/
        ctx->state = failed;
        return (-1);
    }

    strcpy(ptr_data->server_name, servername);
    ptr_data->port = port;

    ptr_data->cb = func;
    ptr_data->data = args;
    ptr_data->ctx = ctx;

    if ((ret = pthread_create(&thread_id, NULL, mistral_tcp_connection_thread, (void *)ptr_data)) != 0) {
        mistral_err("pthread_create() error %d\n", ret);
        ctx->state = failed;
        return (-1);
    }

    if (pthread_detach(thread_id) != 0) {
        mistral_err("pthread_detach() error\n");
        return (-1);
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->thread_id = thread_id;
    pthread_mutex_unlock(&ctx->mutex);

    return ret;
}


int mistral_fluentbit_send(mistral_fluentbit_tcp_ctx_s *ctx, const char *buff, int len) {

    int fd = -1;
    mistral_fluentbit_tcp_state_t state;

    pthread_mutex_lock(&ctx->mutex);
    fd = ctx->fd;
    state = ctx->state;
    pthread_mutex_unlock(&ctx->mutex);

    if (state != connected) return (-1);
  
    int ret =  write(fd, buff, len);

    if (ret < 0) 
    {
        mistral_err("mistral_fluentbit_send <%d>!\n", ret);
    }

    return ret;
}


static int mistral_tcp_connect(int sockfd, struct sockaddr_in *serveraddr, int retries, useconds_t usec) 
{

    int iterations = 0;
    int ret = -1;

    while (iterations < retries) {

        ret = connect(sockfd, (const struct sockaddr *)serveraddr, sizeof(struct sockaddr_in));
        /* Connect: create a connection to the Fluet Bit server */
        if (ret < 0) {
            mistral_err("Error connecting socket %d\n", sockfd);
            iterations++;
            usleep(iterations * usec);
        } else goto done;

    }

done:
    return ret;
}


static void *mistral_tcp_connection_thread(void *arg)
{
    int sockfd = -1;
    struct sockaddr_in serveraddr;
    struct hostent *server = NULL;
    char buff[MISTRAL_TCP_BUFFER_SIZE];

    mistral_fluentbit_thread_cb_s *ptr_data = (mistral_fluentbit_thread_cb_s *)arg;
    mistral_fluentbit_tcp_ctx_s *tcp_ctx = ptr_data->ctx;

    /* Socket: create the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        mistral_err("Error opening socket %d\n", sockfd);
        goto failed;
    }

    mistral_err("Get DNS name for %s\n", ptr_data->server_name);
    /* Gethostbyname: get the server's DNS entry */
    server = gethostbyname(ptr_data->server_name);
    if (server == NULL) {
        mistral_err("Error, no such host as %s\n", ptr_data->server_name);

                goto failed;
    }

    /* Build the server's Internet address */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(ptr_data->port);

    mistral_err("Connect socket %d\n", sockfd);

       /* Connect: create a connection with the server */
    if (mistral_tcp_connect(sockfd, &serveraddr, MISTRAL_TCP_RECONNECT_TRYOUTS, MISTRAL_TIMEOUT_BETWEEN_RECONNECTS) < 0) {
            mistral_err("Error connecting to the Fluent Bit - socket %d\n", sockfd);

                goto failed;
        } 


    pthread_mutex_lock(&ptr_data->ctx->mutex);
    
    ptr_data->ctx->fd = sockfd;
    ptr_data->ctx->state = connected;
    pthread_mutex_unlock(&ptr_data->ctx->mutex);

    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        mistral_err("Epoll create error: %s\n", strerror(errno));

        goto failed;

    }

    struct epoll_event ev;
    

    int rc;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;

    mistral_err("Adding fd <%d> to epoll\n", sockfd);

    rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);
    if (rc == -1) {
        mistral_err("Failing to add a socket the main loop - epoll_ctl: <%s>\n", strerror(errno));

        goto failed;
    }

    /*
    * This is our main loop. It uses epoll internally for input.
    */
    while (true) {
        int ret = 0;
        do {
            ret = epoll_wait(epoll_fd, &ev, 1, MISTRAL_EPOL_TIMEOUT_INTERVAL);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            mistral_err("Client return < 0 %s\n", strerror(errno));

             goto failed;

        }

        /* This means that epoll_wait exit with timeout and there is not event */
        if (ret == 0) continue;

        if (ev.data.fd == sockfd) {

            ssize_t len = read(sockfd, buff, 1024);
            mistral_err("XXXClient receives: %zu\n", len);
            if (len == 0) {
                mistral_err("Client receives: %zu\n", len);
                goto failed;

            }
        }
    
    } /* End while loop */

    close(sockfd);


    /* TODO free arg here
     * pthread_exit(ret); // TODO
     */

failed: 

    pthread_mutex_lock(&tcp_ctx->mutex);
    tcp_ctx->state = failed;
    if (tcp_ctx->fd >= 0) close(tcp_ctx->fd);
    tcp_ctx->fd = -1;  
    pthread_mutex_unlock(&tcp_ctx->mutex);

    return (NULL);
}


