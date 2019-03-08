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
#include <unistd.h>
#include <fcntl.h>

#include "mistral_fluentbit_tcp.h"
#include "mistral_plugin.h"

#define MISTRAL_EPOL_TIMEOUT_INTERVAL 500 /* Timeout for epol in milliseconds */

/* MISTRAL_TCP_RECONNECT_TRYOUTS indicates how many time the client tries
 * to connect / reconnect before quitting 
 */
#define MISTRAL_TCP_RECONNECT_TRYOUTS 10          
/* MISTRAL_TIMEOUT_BETWEEN_RECONNECTS indicates the time before issueing
 * a new reconnect. The period is in microseconds, i.e.200 milliseconds. 
 */
#define MISTRAL_TIMEOUT_BETWEEN_RECONNECTS 200000 
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

/*
 * mistral_fluentbit_init
 *
 * This function initialises the TCP connection information structure. The structure
 * contains the connection socket, the state of the connection, etc.
 *
 * Parameters:
 *   ctx         - A pointer to the TCP connection information structure.
 * Returns:
 *   void
 */
void mistral_fluentbit_init(mistral_fluentbit_tcp_ctx_s *ctx)
{
    pthread_mutex_init(&ctx->mutex, NULL);
    ctx->fd = -1;
    ctx->state = intialized;
}

/*
 * mistral_fluentbit_connect
 *
 * This function creates a new thread which connects to the Fluent Bit via TCP.
 * It is called in the plug-in initialisation phase.
 *
 * Parameters:
 *   ctx         - A pointer to the TCP connection information structure. The structure
 *                 contains the connection socket, the state of the connection, etc.
 *   server_name - The name or IP address of the Fluent Bit server.
 *   port        - The TCP port of the Fluent Bit server.
 *   func        - A callback function passed to pthread_create(...).
 *   arg         - A pointer to arguments which is used by the callback function.
 *
 * Returns:
 *   A negative integer on failure, a positive integer otherwise
 */
int mistral_fluentbit_connect(mistral_fluentbit_tcp_ctx_s *ctx, const char *servername, int port,
                              mistral_fluentbit_tcp_f func,
                              void *args)
{
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

    if ((ret = pthread_create(&thread_id, NULL,
                              mistral_tcp_connection_thread, (void *)ptr_data)) != 0)
    {
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

/*
 * mistral_fluentbit_send
 *
 * This function sends data to the Fluent Bit server.
 *
 * Parameters:
 *   ctx         - A pointer to the TCP connection information structure. The structure
 *                 contains the connection socket, the state of the connection, etc.
 *   buff        - A pointer to the buffer.
 *   len         - The length of the buffer to be sent.
 *
 * Returns:
 *   A negative integer on failure, a positive integer otherwise. The positive interger
 *   specifies how much data is actually send to the remote peer.
 */
int mistral_fluentbit_send(mistral_fluentbit_tcp_ctx_s *ctx, const char *buff, int len)
{
    int fd = -1;
    mistral_fluentbit_tcp_state_t state;

    pthread_mutex_lock(&ctx->mutex);
    fd = ctx->fd;
    state = ctx->state;
    pthread_mutex_unlock(&ctx->mutex);

    if (state != connected) {
        return (-1);
    }

    int ret =  write(fd, buff, len);
    if (ret < 0) {
        mistral_err("Mistral  <%d>!\n", ret);
    }

    return ret;
}

/*
 * mistral_tcp_connect
 *
 * This function implements connection and reconnection policy
 *
 * Parameters:
 *   server_name - The name or IP address of the Fluent Bit server.
 *   port        - The port number where to connected.
 *   retries     - The number of retries before declaring the server unreacheable.
 *   usec        - The time between retries in microseconds.
 *
 * Returns:
 *   A negative integer on failure, a positive integer otherwise. The positive interger
 *   specifies the socket used to communicate with the Fluent Bit
 */
static int mistral_tcp_connect(const char *server_name, int port, int retries, useconds_t usec)
{
    int sockfd = -1;
    char port_buffer[16];
    int gai_retval;
    struct addrinfo *addrs;
    struct addrinfo hints;
    int iteration = 0;

    /* Get addrinfo using the provided parameters */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    sprintf(port_buffer, "%d", port);

    if ((gai_retval = getaddrinfo(server_name, port_buffer, &hints, &addrs)) != 0) {
        mistral_err("Failed to get host info: %s\n", gai_strerror(gai_retval));
        return (-1);
    }

    while (iteration < retries) {
        /* getaddrinfo returns an array of matching possible addresses, try to
         * connect to each until we succeed or run out of addresses.
         */

        for (struct addrinfo *curr = addrs; curr != NULL; curr = curr->ai_next) {
            char buf[256];

            if ((sockfd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1) {
                mistral_err("Unable to create socket: %s%s\n",
                            strerror_r(errno, buf, sizeof buf),
                            curr->ai_next ? " - Trying next address" : "");
                continue;
            }

            if (connect(sockfd, curr->ai_addr, curr->ai_addrlen) == -1) {
                /* Do not log all failed attempts as this can be quite a common occurrence */
                close(sockfd);
                sockfd = -1;
                continue;
            }
            break;
        }

        if (sockfd >= 0) {
            /* This means we have a connected socket */
            break;
        }

        ++iteration;
        usleep(usec);
    } /* End while */

    if (sockfd < 0) {
        mistral_err("Unable to connect to the Fluent Bit TCP endpoint: %s:%s\n", server_name,
                    port_buffer);
    }

    /* We no longer need the array of addresses */
    freeaddrinfo(addrs);

    return sockfd;
}

/*
 * mistral_tcp_connection_thread
 *
 * This is the thread function which maintains the TCP connetion.
 *
 * Parameters:
 *   arg - A pointer to the data structure used by the thread.
 *
 * Returns:
 *   NULL
 */
static void *mistral_tcp_connection_thread(void *arg)
{
    int sockfd = -1;
    char buff[MISTRAL_TCP_BUFFER_SIZE];
    struct epoll_event ev;
    int ret;

    mistral_fluentbit_thread_cb_s *ptr_data = (mistral_fluentbit_thread_cb_s *)arg;
    mistral_fluentbit_tcp_ctx_s *tcp_ctx = ptr_data->ctx;

    /* We connect to the Fluent Bit TCP endpoint */
    sockfd = mistral_tcp_connect(ptr_data->server_name, ptr_data->port,
                                 MISTRAL_TCP_RECONNECT_TRYOUTS, MISTRAL_TIMEOUT_BETWEEN_RECONNECTS);
    if (sockfd < 0) {
        /* We already log this error in the mistral_tcp_connect(). We don't do it here again. */
        goto failed;
    }

    pthread_mutex_lock(&ptr_data->ctx->mutex);
    tcp_ctx->fd = sockfd;
    tcp_ctx->state = connected;
    pthread_mutex_unlock(&ptr_data->ctx->mutex);

    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        mistral_err("Epoll create error: %s\n", strerror(errno));
        close(sockfd);
        goto failed;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;

    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);
    if (ret == -1) {
        mistral_err("Failing to add a socket the main loop - epoll_ctl: <%s>\n", strerror(errno));
        close(sockfd);
        goto failed;
    }

    /*
     * This is our main loop. It uses epoll internally for TCP input from Fluent Bit
     */
    while (true) {
        do {
            ret = epoll_wait(epoll_fd, &ev, 1, MISTRAL_EPOL_TIMEOUT_INTERVAL);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            mistral_err("Client return < 0 %s\n", strerror(errno));
            close(sockfd);
            goto failed;
        }

        /* This means that epoll_wait exited because of the timeout and there is no event */
        if (ret == 0) {
            continue;
        }

        if (ev.data.fd == sockfd) {
            ssize_t len = read(sockfd, buff, MISTRAL_TCP_BUFFER_SIZE);
            if (len == 0) {
                /* "len = 0" means disconnected. We try to reconnect. */

                mistral_err("Mistral plug-in disconnect from Fluent Bit. Trying to reconnect.\n");

                ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, &ev);
                if (ret == -1) {
                    close(sockfd);
                    mistral_err("Epoll_ctl error: %s\n", strerror(errno));
                    goto failed;
                }

                close(sockfd);

                /* We connect to the Fluent Bit TCP endpoint */
                sockfd = mistral_tcp_connect(ptr_data->server_name, ptr_data->port,
                                             MISTRAL_TCP_RECONNECT_TRYOUTS,
                                             MISTRAL_TIMEOUT_BETWEEN_RECONNECTS);
                if (sockfd < 0) {
                    /* We already log this error in the mistral_tcp_connect(). We don't do it again
                     * here. */
                    goto failed;
                }

                ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);
                if (ret == -1) {
                    mistral_err("Failing to add the socket with epoll_ctl: %s\n", strerror(errno));
                    close(sockfd);
                    goto failed;
                }

                /* We have a new socket, so we update the shared data structure */
                pthread_mutex_lock(&tcp_ctx->mutex);
                tcp_ctx->fd = sockfd;
                tcp_ctx->state = connected;
                pthread_mutex_unlock(&tcp_ctx->mutex);
            }
        }
    } /* End while loop */

    close(sockfd);

    /* TODO free arg here
     * pthread_exit(ret);
     */

failed:
    mistral_err("Fluent Bit TCP connectivity thread failed. Terminating the thread.\n");
    pthread_mutex_lock(&tcp_ctx->mutex);
    tcp_ctx->state = failed;
    tcp_ctx->fd = -1;
    pthread_mutex_unlock(&tcp_ctx->mutex);

    return (NULL);
}
