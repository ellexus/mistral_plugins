/* Utilities to read and store log messages received from mistral that do not need to be exposed to
 * someone writing a plug-in outside of Ellexus.
 */

#ifndef MISTRAL_PLUGIN_CONTROL_H
#define MISTRAL_PLUGIN_CONTROL_H

#include <stddef.h>             /* size_t */
#include <stdint.h>             /* uint32_t */
#include <sys/time.h>           /* struct timeval */
#include "mistral_plugin.h"     /* Definitions that need to be available to plug-in developers */

/* Store the lengths of the scopes to avoid using strlen */
size_t mistral_call_type_len[MAX_CALL_TYPE] = {
#define X(P, V) sizeof(V)-1,
    CALL_TYPES(X)
#undef X
};

#define PLUGIN_DATA_ERR -1
#define PLUGIN_FATAL_ERR -2

/* Version of the API used by the plug-in */
#define MISTRAL_API_VERSION 2

/* Define the number of fields in the log string */
#define LOG_FIELDS 13
#define PLUGIN_MESSAGE_FIELDS 3

/* Define the separator as a character that is not valid in a contract label */
#define PLUGIN_MESSAGE_SEP_C ':'
#define PLUGIN_MESSAGE_SEP_S ":"
#define PLUGIN_MESSAGE_END PLUGIN_MESSAGE_SEP_S "\n"

/* Set up message type strings */
#define PLUGIN_MESSAGE(X) \
    X(USED_VERSION  , PLUGIN_MESSAGE_SEP_S "PGNVERSION" PLUGIN_MESSAGE_SEP_S) \
    X(SUP_VERSION   , PLUGIN_MESSAGE_SEP_S "PGNSUPVRSN" PLUGIN_MESSAGE_SEP_S) \
    X(INTERVAL      , PLUGIN_MESSAGE_SEP_S "PGNINTRVAL" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_START    , PLUGIN_MESSAGE_SEP_S "PGNDATASRT" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_LINE     , PLUGIN_MESSAGE_SEP_S "PGNDATALIN" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_END      , PLUGIN_MESSAGE_SEP_S "PGNDATAEND" PLUGIN_MESSAGE_SEP_S) \
    X(SHUTDOWN      , PLUGIN_MESSAGE_SEP_S "PGNSHUTDWN" PLUGIN_MESSAGE_END)

enum {
#define X(P, V) PLUGIN_MESSAGE_ ## P,
    PLUGIN_MESSAGE(X)
#undef X
    PLUGIN_MESSAGE_LIMIT
};

/* Store the lengths of the messages to avoid using strlen */
size_t mistral_log_msg_len[PLUGIN_MESSAGE_LIMIT] = {
#define X(P, V) sizeof(V)-1,
    PLUGIN_MESSAGE(X)
#undef X
};

/* Create various string arrays based off of the mistral_plugin.h header */
const char *const mistral_log_message[PLUGIN_MESSAGE_LIMIT] = {
#define X(P, V) V,
    PLUGIN_MESSAGE(X)
#undef X
};

const char *const mistral_contract_name[] = {
#define X(name, str, header) str,
    CONTRACTS(X)
#undef X
    0x0
};

const char *const mistral_contract_header[] = {
#define X(name, str, header) header,
    CONTRACTS(X)
#undef X
    0x0
};

const char *const mistral_scope_name[] = {
#define X(name, str) str,
    SCOPE(X)
#undef X
    0x0
};

const char *const mistral_measurement_name[] = {
#define X(name, str) str,
    MEASUREMENTS(X)
#undef X
    0x0
};

const char *const mistral_unit_class_name[] = {
#define X(name, str) str,
    UNIT_CLASS(X)
#undef X
    0x0
};

const char *const mistral_unit_suffix[] = {
#define X(name, str, scale, type) str,
    UNITS(X)
#undef X
    0x0
};

const char *const mistral_call_type_name[] = {
#define X(name, str) str,
    CALL_TYPES(X)
#undef X
    0x0
};

const uint32_t mistral_call_type_mask[] = {
#define X(name, str) 1u << (CALL_TYPE_ ## name),
    CALL_TYPES(X)
#undef X
    1u << (MAX_CALL_TYPE)
};

const char mistral_call_type_names[1u << MAX_CALL_TYPE][0
#define X(name, str) + sizeof(str) + 1
    CALL_TYPES(X)
#undef X
];

/* Similarly create some constant integer arrays */
const uint32_t mistral_unit_scale[] = {
#define X(name, str, scale, type) scale,
    UNITS(X)
#undef X
    0x0
};

const uint32_t mistral_unit_type[] = {
#define X(name, str, scale, type) type,
    UNITS(X)
#undef X
    0x0
};

#define STRIP_NEWLINE(line, length)                     \
    do {                                                \
        if (length > 0 && line[length - 1] == '\n') {   \
            line[length - 1] = '\0';                    \
        }                                               \
    } while(0)

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

#endif
