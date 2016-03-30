#include "log_to_mysql.h"
/* Utilities to read and store log messages received from mistral. */

#ifndef MISTRAL_MYSQL_LOG_H
#define MISTRAL_MYSQL_LOG_H

/* Define file descriptors for the comunication pipe */
#define LOG_INPUT 0
#define LOG_OUTPUT 1


#define PLUGIN_DATA_ERR -1
#define PLUGIN_FATAL_ERR -2

/* Version of the plugin */
#define VERSION 1

/* Define the path to the error log file when there is no -o options*/
#define TMP_ERR_LOG /tmp/err_log_

/* Define supported plugin interface version range */
#define PLUGIN_MIN_VER "1"
#define PLUGIN_CUR_VER "1"

/* Define the number of separators in the log string */
#define COMMA_SEP_NUM 12
#define COLON_SEP_NUM 5

/* Define the separator as a character that is not valid at in a contract label */
#define PLUGIN_MESSAGE_SEP_C ':'
#define PLUGIN_MESSAGE_SEP_S ":"
#define PLUGIN_MESSAGE_END PLUGIN_MESSAGE_SEP_S "\n"

/* Set up message type strings */
#define PLUGIN_MESSAGE(X) \
    X(USED_VERSION  , PLUGIN_MESSAGE_SEP_S "PGNVERSION" PLUGIN_MESSAGE_SEP_S) \
    X(SUP_VERSION   , PLUGIN_MESSAGE_SEP_S "PGNSUPVRSN" PLUGIN_MESSAGE_SEP_S  \
                      PLUGIN_MIN_VER PLUGIN_MESSAGE_SEP_S          \
                      PLUGIN_CUR_VER PLUGIN_MESSAGE_END)           \
    X(INTERVAL      , PLUGIN_MESSAGE_SEP_S "PGNINTRVAL" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_START,     PLUGIN_MESSAGE_SEP_S "PGNDATASRT" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_LINE ,     PLUGIN_MESSAGE_SEP_S "PGNDATALIN" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_END  ,     PLUGIN_MESSAGE_SEP_S "PGNDATAEND" PLUGIN_MESSAGE_SEP_S) \
    X(SHUTDOWN      , PLUGIN_MESSAGE_SEP_S "PGNSHUTDWN" PLUGIN_MESSAGE_END)

enum {
#define X(P, V) PLUGIN_MESSAGE_ ## P,
    PLUGIN_MESSAGE(X)
#undef X
    PLUGIN_MESSAGE_LIMIT
};

/*Store the lengths of the messages to avoid using strlen */
size_t mistral_log_msg_len[PLUGIN_MESSAGE_LIMIT] = {
#define X(P, V) sizeof(V)-1,
    PLUGIN_MESSAGE(X)
#undef X
};

/* Store the message strings in an easily accessed form */
const char *const mistral_log_message[PLUGIN_MESSAGE_LIMIT] = {
#define X(P, V) V,
    PLUGIN_MESSAGE(X)
#undef X
};

#define STRIP_NEWLINE(line, length)                     \
    do {                                                \
        if (length > 0 && line[length - 1] == '\n') {   \
            line[length - 1] = '\0';                    \
        }                                               \
    } while(0)

#endif
