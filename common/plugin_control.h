/* Utilities to read and store log messages received from mistral that do not need to be exposed to
 * someone writing a plug-in outside of Ellexus.
 */

#ifndef MISTRAL_PLUGIN_CONTROL_H
#define MISTRAL_PLUGIN_CONTROL_H

#include <stddef.h>             /* size_t */
#include <stdint.h>             /* uint32_t */
#include <stdio.h>              /* FILE */
#include <sys/time.h>           /* struct timeval */
#include "mistral_plugin.h"     /* Definitions that need to be available to plug-in developers */

/* Store the lengths of the scopes to avoid using strlen */
size_t mistral_call_type_len[CALL_TYPE_MAX] = {
#define X(name, str) sizeof(str)-1,
    CALL_TYPE(X)
#undef X
};

/* Version of the API used by the plug-in */
#define MISTRAL_API_VERSION 2

/* Define the number of fields in the plugin message string */
#define PLUGIN_MESSAGE_FIELDS 3

/* Define the separator as a character that is not valid in a contract label */
#define PLUGIN_MESSAGE_SEP_C ':'
#define PLUGIN_MESSAGE_SEP_S ":"
#define PLUGIN_MESSAGE_END PLUGIN_MESSAGE_SEP_S

/* Set up message type strings */
#define PLUGIN_MESSAGE(X) \
    X(USED_VERSION  , PLUGIN_MESSAGE_SEP_S "PGNVERSION" PLUGIN_MESSAGE_SEP_S) \
    X(SUP_VERSION   , PLUGIN_MESSAGE_SEP_S "PGNSUPVRSN" PLUGIN_MESSAGE_SEP_S) \
    X(INTERVAL      , PLUGIN_MESSAGE_SEP_S "PGNINTRVAL" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_START    , PLUGIN_MESSAGE_SEP_S "PGNDATASRT" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_LINE     , PLUGIN_MESSAGE_SEP_S "PGNDATALIN" PLUGIN_MESSAGE_SEP_S) \
    X(DATA_END      , PLUGIN_MESSAGE_SEP_S "PGNDATAEND" PLUGIN_MESSAGE_SEP_S) \
    X(SHUTDOWN      , PLUGIN_MESSAGE_SEP_S "PGNSHUTDWN" PLUGIN_MESSAGE_END)

enum mistral_message {
    PLUGIN_FATAL_ERR = -2,
    PLUGIN_DATA_ERR = -1,
#define X(P, V) PLUGIN_MESSAGE_ ## P,
    PLUGIN_MESSAGE(X)
#undef X
    PLUGIN_MESSAGE_LIMIT
};

/* Store the lengths of the messages to avoid using strlen */
size_t mistral_log_msg_len[PLUGIN_MESSAGE_LIMIT] = {
#define X(P, V) sizeof(V) - 1,
    PLUGIN_MESSAGE(X)
#undef X
};

const char *const mistral_log_message[PLUGIN_MESSAGE_LIMIT] = {
#define X(P, V) V,
    PLUGIN_MESSAGE(X)
#undef X
};

#define LOG_FIELD(X) \
    X(TIMESTAMP) \
    X(LABEL) \
    X(PATH) \
    X(CALL_TYPE) \
    X(SIZE_RANGE) \
    X(MEASUREMENT) \
    X(MEASURED) \
    X(THRESHOLD) \
    X(PID) \
    X(COMMAND) \
    X(FILENAME) \
    X(JOB_GROUP_ID) \
    X(JOB_ID)

enum mistral_log_fields {
#define X(P) FIELD_ ## P,
    LOG_FIELD(X)
#undef X
    FIELD_MAX
};

/* Create various string arrays based off of the mistral_plugin.h header */
const char *const mistral_contract_name[] = {
#define X(name, str, header) str,
    CONTRACT(X)
#undef X
    NULL
};

const char *const mistral_contract_header[] = {
#define X(name, str, header) header,
    CONTRACT(X)
#undef X
    NULL
};

const char *const mistral_scope_name[] = {
#define X(name, str) str,
    SCOPE(X)
#undef X
    NULL
};

const char *const mistral_measurement_name[] = {
#define X(name, str) str,
    MEASUREMENT(X)
#undef X
    NULL
};

const char *const mistral_unit_class_name[] = {
#define X(name, str) str,
    UNIT_CLASS(X)
#undef X
    NULL
};

const char *const mistral_unit_suffix[] = {
#define X(name, str, scale, type) str,
    UNIT(X)
#undef X
    NULL
};

const char *const mistral_call_type_name[] = {
#define X(name, str) str,
    CALL_TYPE(X)
#undef X
    NULL
};

const uint32_t mistral_call_type_mask[] = {
#define X(name, str) BITMASK(CALL_TYPE_ ## name),
    CALL_TYPE(X)
#undef X
    BITMASK(CALL_TYPE_MAX)
};

const char mistral_call_type_names[CALL_TYPE_MASK_MAX][
#define X(name, str) + sizeof(str) + 1
    CALL_TYPE(X)
#undef X
];

/* Similarly create some constant integer arrays */
const uint32_t mistral_unit_scale[] = {
#define X(name, str, scale, type) scale,
    UNIT(X)
#undef X
};

const uint32_t mistral_unit_type[] = {
#define X(name, str, scale, type) type,
    UNIT(X)
#undef X
};

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

#define CALL_IF_DEFINED(function, ...)  \
    if (function) {                     \
        function(__VA_ARGS__);          \
    }

#endif
