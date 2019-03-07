/*
 * mistral_fluentbit_tcp.h
 *
 * This file is the header file for the module which provides TCP connectivity
 * towards the Fluent Bit
 *
 */

#ifndef MISTRAL_FLUENTBIT_TCP_H
#define MISTRAL_FLUENTBIT_TCP_H

#include <pthread.h>

typedef enum {
    intialized = 0x5540,
    connecting,
    connected,
    disconnected,
    failed
} mistral_fluentbit_tcp_state_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_t thread_id;
    mistral_fluentbit_tcp_state_t state;
    int fd;
} mistral_fluentbit_tcp_ctx_s;

typedef int (*mistral_fluentbit_tcp_f)(void *);

void mistral_fluentbit_init(mistral_fluentbit_tcp_ctx_s *ctx);
int mistral_fluentbit_connect(mistral_fluentbit_tcp_ctx_s *ctx,
                              const char *servername, int port, mistral_fluentbit_tcp_f func,
                              void *args);
int mistral_fluentbit_send(mistral_fluentbit_tcp_ctx_s *cli, const char *buff, int len);

#endif /* MISTRAL_FLUENTBIT_TCP_H */
