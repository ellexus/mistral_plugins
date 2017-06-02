/*
 * plugin_control MODULE ALIAS: mistral
 *
 * The module alias defined above is slightly odd. The reason this was chosen is because at some
 * point we intend to release the object file to customers so they can link against it to produce
 * plug-ins. This alias, combined with our naming convention produces nice function names for the
 * external interface.
 */
#include <assert.h>             /* assert */
#include <errno.h>              /* errno */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <limits.h>             /* SSIZE_MAX */
#include <stdarg.h>             /* va_start, va_list, va_end */
#include <stdbool.h>            /* bool */
#include <stdint.h>             /* uint64_t, UINT64_MAX */
#include <stdio.h>              /* fprintf, asprintf, vfprintf */
#include <stdlib.h>             /* calloc, free */
#include <string.h>             /* strerror_r, strdup, strncmp, strcmp, etc. */
#include <sys/types.h>          /* getpid */
#include <time.h>               /* gettimeofday, strptime, mktime, tzset */
#include <unistd.h>             /* getpid, STDOUT_FILENO, STDIN_FILENO */


#include "plugin_control.h"

#define DEBUG_OUTPUT(format, ...)                       \
do {                                                    \
    if (mistral_output_control_debug) {                 \
        mistral_err("DEBUG[control][%d] %s:%d " format, \
                    mistral_plugin_pid, __func__,       \
                    __LINE__, ##__VA_ARGS__);           \
    }                                                   \
} while (0)


static unsigned ver = MISTRAL_API_VERSION;  /* Supported version */
static uint64_t data_count = 0;             /* Number of data blocks received */
static bool in_data = false;                /* True, if mistral is sending data */
static bool shutdown_message = false;       /* True, if mistral sent a shutdown message */
static bool supported_version = false;      /* True, if supported versions were received */
static uint64_t interval = 0;               /* Interval between plug-in calls in seconds */
static mistral_plugin mistral_plugin_info;  /* Used to store plug-in type and calling interval */

/* Global variables available to plug-in developers */
bool mistral_shutdown = false;               /* If set to true plug-in will exit at next line */

/* Define this value here in case the machine used to compile the plug-in functionality module uses
 * different values. This will allow a plug-in author to identify when the upper bound of a size
 * range was seen in a mistral log message.
 */
const int64_t mistral_max_size = SSIZE_MAX;

/*
 * mistral_err
 *
 * Simply print the passed message to the error log.  If this is stderr attempt to append a
 * newline to the format before printing the message.
 *
 * Parameters:
 *    format - a standard printf style format string
 *    ...    - Any parameters required by the format string
 *
 * Returns:
 *   The value returned by a call to vfprintf with the configured stream and the passed parameters.
 */
int mistral_err(const char *format, ...)
{
    int retval = 0;
    va_list ap;
    va_start(ap, format);
    char *file_fmt = NULL;
    char *fmt = (char *)format;

    if (mistral_plugin_info.error_log == stderr && format[strlen(format) - 1] != '\n') {
        if (asprintf(&file_fmt, "%s\n", format) >= 0) {
            fmt = file_fmt;
        }
    } else if (mistral_plugin_info.error_log != stderr) {
        struct timeval tv = {0,0};
        gettimeofday(&tv, NULL);
        if (asprintf(&file_fmt, "%ld.%06ld %s", tv.tv_sec, tv.tv_usec, format) >= 0) {
            fmt = file_fmt;
        }
    }

    retval = vfprintf(mistral_plugin_info.error_log, fmt, ap);
    va_end(ap);
    free(file_fmt);
    fflush(mistral_plugin_info.error_log);
    return retval;
}

/*
 * init_mistral_call_type_names
 *
 * This function initalises an array with all the possible combinations of call types. It is done
 * this way to avoid memory management of the string within the plug-in
 *
 * Parameters:
 *   void
 *
 * Returns:
 *   void
 */
void init_mistral_call_type_names(void)
{
    DEBUG_OUTPUT("Entering function\n");
    size_t max_string = 0;
#define X(name, str) max_string += sizeof(str) + 1;
    CALL_TYPE(X)
#undef X
    char tmp1[max_string];

    /* Loop through all the possible bitmask combinations */
    for (size_t i = 0; i < CALL_TYPE_MASK_MAX; i++) {
        tmp1[0] = '\0';
        /* For each entry in the list loop through the list of call types */
        for (size_t j = 0; j < CALL_TYPE_MAX; j++) {
            if (tmp1[0] == '\0' && (i & BITMASK(j)) == BITMASK(j)) {
                /* This is the first call type we have seen that is represented in this bitmask */
                if (snprintf(tmp1, max_string, "%s", mistral_call_type_name[j]) < 0) {
                    mistral_err("Could not initialise call type name array\n");
                    mistral_shutdown = true;
                }
            } else if ((i & BITMASK(j)) == BITMASK(j)) {
                /* This call type is in the bit mask but we need to append it to the current list */
                char tmp2[max_string];
                tmp2[0] = '\0';
                strncpy(tmp2, tmp1, max_string);
                if (snprintf(tmp1, max_string, "%s+%s", tmp2, mistral_call_type_name[j]) < 0) {
                    mistral_err("Could not initialise call type name array\n");
                    mistral_shutdown = true;
                }
            }
        }
        strncpy((char *)mistral_call_type_names[i], tmp1, sizeof(mistral_call_type_names[i]));
    }
    DEBUG_OUTPUT("Leaving function, success\n");
}

/*
 * send_string_to_mistral
 *
 * Sends a message to mistral
 *
 * Parameters:
 *   message    - Standard null terminated string containing the message to be passed
 *
 * Returns:
 *   False - On error
 *   True  - Otherwise
 */
static bool send_string_to_mistral(const char *message)
{
    DEBUG_OUTPUT("Entering function\n");
    const char *message_start = message;
    size_t message_len = strlen(message);

    fd_set write_set;
    /* Ideally this function would use an immediate timeout but this causes errors on CentOS 4 */
    struct timeval timeout = {
        .tv_sec = 1,
        .tv_usec = 0,
    };

    int result;
    FD_ZERO(&write_set);
    FD_SET(STDOUT_FILENO, &write_set);

    /* Keep trying to send data until all the message has been sent or an error occurs. */
    while (message_len) {
        do {
            result = select(1 + STDOUT_FILENO, NULL, &write_set, NULL, &timeout);
        } while (result == -1 && errno == EINTR);   /* retry if interrupted */

        if (result < 0) {
            char buf[256];
            mistral_err("Error in select() while sending data to Mistral: %s\n",
                        strerror_r(errno, buf, sizeof buf));
            DEBUG_OUTPUT("Leaving function, failure\n");
            return false;
        }

        if (FD_ISSET(STDOUT_FILENO, &write_set)) {
            ssize_t w_res = write(STDOUT_FILENO, message_start, message_len);

            /* If we've successfully written some data, update the message pointer and length
             * values
             */
            if (w_res >= 0) {
                message_start += w_res;
                message_len -= w_res;
            }

            if ((w_res < 0 && errno == EINTR) || (w_res < 0 && errno == EAGAIN) ||
                (w_res >= 0 && message_len != 0)) {
                /* Try and send the remaining portion of the message again if we were
                 * interrupted, encountered a full pipe or only managed a partial write. The
                 * pipe will be retested in case Mistral has exited.
                 */
                continue;
            }
            if (message_len != 0) {
                char buf[256];
                mistral_err("Failed write, unable to send data: (%s)\n",
                            strerror_r(errno, buf, sizeof buf));
                DEBUG_OUTPUT("Leaving function, failure\n");
                return false;
            }
        } else {
            mistral_err("Mistral is not ready to receive data\n");
            DEBUG_OUTPUT("Leaving function, failure\n");
            return false;
        }
    }
    DEBUG_OUTPUT("Leaving function, success\n");
    return true;
}

/*
 * send_message_to_mistral
 *
 * Function sends fixed messages to mistral. Attempting to send a message type that requires a
 * parameter or an invalid message type is considered an error.
 *
 * Parameters:
 *   message - The message type to send
 *
 * Returns:
 *   False - On error
 *   True  - Otherwise
 */
static bool send_message_to_mistral(enum mistral_message message)
{
    DEBUG_OUTPUT("Entering function\n");
    char *message_string = NULL;

    switch (message) {
    case PLUGIN_MESSAGE_SHUTDOWN:
        if (asprintf(&message_string, "%s\n", mistral_log_message[PLUGIN_MESSAGE_SHUTDOWN]) < 0) {
            mistral_err("Unable to construct shutdown message.\n");
            goto fail_asprintf;
        }
        break;
    case PLUGIN_MESSAGE_USED_VERSION:
        if (asprintf(&message_string, "%s%u%s\n",
                     mistral_log_message[PLUGIN_MESSAGE_USED_VERSION],
                     ver, PLUGIN_MESSAGE_END) < 0) {
            mistral_err("Unable to construct used version message.\n");
            goto fail_asprintf;
        }
        break;
    default:
        mistral_err("Invalid message type.\n");
        goto fail_invalid;
    }

    if (!send_string_to_mistral((const char *)message_string)) {
        goto fail_send;
    }

    free(message_string);
    DEBUG_OUTPUT("Leaving function, success\n");
    return true;

fail_send:
    free(message_string);
fail_invalid:
fail_asprintf:
    DEBUG_OUTPUT("Leaving function, failure\n");
    return false;
}

/*
 * str_split
 *
 * Function to split a string into an array of strings. A trailing NULL pointer will be added to the
 * array so it can be used by functions that use this convention rather than taking array length as
 * a parameter.
 *
 * Consecutive separators will not be consolidated , i.e. empty strings will be produced.
 *
 * The value of field_count will be set to the number of strings in the array.
 *
 * Parameters:
 *   s           - Standard null terminated string to be separated
 *   sep         - Character used to delimit array entries
 *   field_count - Integer value to be updated with the number of fields in the array
 *
 * Returns:
 *   A pointer to the start of the array if successful
 *   NULL otherwise
 */
static char **str_split(const char *s, int sep, size_t *field_count)
{
    DEBUG_OUTPUT("Entering function\n");
    size_t n = 1;               /* One more string than separators. */
    size_t len;                 /* Length of 's' */

    assert(s);
    assert(field_count);
    /* Count separators. */
    for (len = 0; s[len]; ++len) {
        n += (sep == s[len]);
    }
    *field_count = n;

    /* Allocate the result array (including space for a NULL at the
     * end), plus space for a copy of 's'.
     */
    void *alloc = calloc(1, len + 1 + (n + 1) * sizeof(char *));
    if (!alloc) {
        DEBUG_OUTPUT("Leaving function, failure\n");
        return NULL;
    }
    char **result = alloc;
    char *copy = alloc;
    copy += (n + 1) * sizeof(char *);
    memcpy(copy, s, len + 1);

    for (size_t i = 0; i < n; ++i) {
        result[i] = copy;
        while (*copy) {
            if (*copy == sep) {
                *copy++ = '\0';
                break;
            }
            ++copy;
        }
    }
    result[n] = NULL;
    DEBUG_OUTPUT("Leaving function, success\n");
    return result;
}

/*
 * find_in_array
 *
 * Search for the passed string in an array of strings that is terminated with a NULL pointer
 *
 * Parameters:
 *   s      - Standard null terminated string containing the string to be found
 *   array  - An array of standard null terminated strings. The final entry must be a NULL pointer
 *
 * Returns:
 *   Index of the matching entry in the array if a match is found
 *   -1 on error
 */
static ssize_t find_in_array(const char *s, const char *const *array)
{
    DEBUG_OUTPUT("Entering function\n");
    assert(array);
    for (size_t i = 0; array[i]; ++i) {
        if (0 == strcmp(s, array[i])) {
            DEBUG_OUTPUT("Leaving function, success\n");
            return i;
        }
    }
    DEBUG_OUTPUT("Leaving function, failure\n");
    return -1;
}

/*
 * parse_size
 *
 * Parse a size followed by a unit in the string starting at s. If successful update *size to be
 * the size in the smallest unit for the type of data, e.g. bytes for size measurements, and set
 * *unit to the UNIT_* enum value representing the display format that was used.
 *
 * Parameters:
 *   s     - Standard null terminated string containing the line to be parsed.
 *   size  - Pointer to the variable to be updated with the calculated size.
 *   unit  - Pointer to the variable to be updated with the reported unit.
 *
 * Returns:
 *   true if the string is successfully parsed
 *   false otherwise
 */
static bool parse_size(const char *s, uint64_t *size, enum mistral_unit *unit)
{
    DEBUG_OUTPUT("Entering function\n");
    assert(s);
    assert(size);
    assert(unit);

    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno || !end || s == end) {
        DEBUG_OUTPUT("Leaving function, failure\n");
        return false;
    }

    ssize_t u = find_in_array(end, mistral_unit_suffix);
    if (u == -1) {
        mistral_err("Invalid unit in value: %s\n", s);
        DEBUG_OUTPUT("Leaving function, failure\n");
        return false;
    } else {
        *unit = u;
    }

    *size = (uint64_t)(value * (double)mistral_unit_scale[*unit]);

    /* Check if the size calculation above overflowed */

    switch (mistral_unit_type[*unit]) {
        /* Because of the differing types each block has to be handled separately */
    case UNIT_CLASS_TIME:
    case UNIT_CLASS_COUNT:
        /* Cast to a double so we shouldn't suffer from integer overflows in the comparison */
        if (value * (double)mistral_unit_scale[*unit] > UINT64_MAX) {
            /* This should not be possible as Mistral is subject to the same limits */
            *size = (uint64_t)UINT64_MAX;
        }
        break;
    case UNIT_CLASS_SIZE:
        /* Cast to a double so we shouldn't suffer from integer overflows in the comparison */
        if (value * (double)mistral_unit_scale[*unit] > SSIZE_MAX) {
            /* Set the size to the maximum value allowed for an ssize_t. This might happen if this
             * file is not compiled on the same machine as the version of Mistral in use.
             */
            *size = (uint64_t)SSIZE_MAX;
        }
        break;
    default:
        /* Getting here is a programming error */
        mistral_err("Unknown unit type: %d\n", mistral_unit_type[*unit]);
        DEBUG_OUTPUT("Leaving function, failure\n");
        return false;
    }

    DEBUG_OUTPUT("Leaving function, success\n");
    return true;
}

/*
 * parse_rate
 *
 * Parse a rate string in the form <data><unit>/<time><unit>.
 *
 * Parameters:
 *   s          - Standard null terminated string containing the line to be parsed.
 *   size       - Pointer to the variable to be updated with the calculated data size.
 *   unit       - Pointer to the variable to be updated with the reported data unit.
 *   length     - Pointer to the variable to be updated with the calculated time period.
 *   lengthunit - Pointer to the variable to be updated with the reported time period unit.
 *
 * Returns:
 *   true if the string is successfully parsed
 *   false otherwise
 */
static bool parse_rate(const char *s, uint64_t *size, enum mistral_unit *unit, uint64_t *length,
                       enum mistral_unit *lengthunit)
{
    DEBUG_OUTPUT("Entering function\n");
    assert(s);
    assert(size);
    assert(unit);
    assert(length);
    assert(lengthunit);
    size_t field_count;

    char **rate_split = str_split(s, '/', &field_count);
    if (!rate_split) {
        mistral_err("Unable to allocate memory for rate: %s\n", s);
        goto fail_rate_split;
    }

    if (field_count == 2) {
        if (!parse_size(rate_split[0], size, unit)) {
            mistral_err("Unable to parse rate size: %s\n", s);
            goto fail_rate_size;
        }
        /* We don't know the type of unit this will be so we will validate it is consistent later */

        if (!parse_size(rate_split[1], length, lengthunit)) {
            mistral_err("Unable to parse rate time period: %s\n", s);
            goto fail_rate_length;
        }

        if (mistral_unit_type[*lengthunit] != UNIT_CLASS_TIME) {
            mistral_err("Unexpected unit for rate time period: %s\n", s);
            goto fail_rate_lengthunit;
        }
    } else {
        mistral_err("Unable to parse rate: %s\n", s);
        goto fail_rate_split_fields;
    }

    free(rate_split);
    DEBUG_OUTPUT("Leaving function, success\n");
    return true;

fail_rate_split_fields:
fail_rate_lengthunit:
fail_rate_length:
fail_rate_size:
    free(rate_split);
fail_rate_split:
    DEBUG_OUTPUT("Leaving function, failure\n");
    return false;
}

/*
 * parse_log_entry
 *
 * Parse the line received. The line structure is <scope>#<type>#log_message
 *
 * scope will be one of "global" or "local"
 *
 * type will be one of "monitor" or "throttle"
 *
 * log_message is the mistral log message as it would have been written to disk. This is a comma
 * separated format but it is possible that the command or file name may contain a comma.
 *
 * Parameters:
 *   line       - Standard null terminated string containing the line to be parsed
 *
 * Returns:
 *   true on success
 *   false otherwise
 */
static bool parse_log_entry(const char *line)
{
    DEBUG_OUTPUT("Entering function\n");
    size_t field_count;
    mistral_log *log_entry = NULL;

    char **comma_split = str_split(line, ',', &field_count);
    size_t log_field_count = field_count;
    if (!comma_split) {
        mistral_err("Unable to allocate memory for split log line: %s\n", line);
        goto fail_split_commas;
    }

    /* As there might be commas in the command and/or filename we cannot just check the raw count */
    if (log_field_count < FIELD_MAX) {
        mistral_err("Invalid log message: %s\n", line);
        goto fail_split_comma_fields;
    }

    char **hash_split = str_split(comma_split[FIELD_TIMESTAMP], '#', &field_count);
    if (!hash_split) {
        mistral_err("Unable to allocate memory for mistral fields: %s\n", comma_split[FIELD_TIMESTAMP]);
        goto fail_split_hashes;
    }

    if (field_count != PLUGIN_MESSAGE_FIELDS) {
        mistral_err("Invalid log message: %s\n", line);
        goto fail_split_hash_fields;
    }

    /* Allocate memory for the log entry */
    log_entry = calloc(1, sizeof(mistral_log));
    if (log_entry == NULL) {
        mistral_err("Unable to allocate memory for log message: %s\n", line);
        goto fail_log_alloc;
    }

    /* Record the contract scope */
    ssize_t scope = find_in_array(hash_split[0], mistral_scope_name);
    if (scope == -1) {
        mistral_err("Invalid scope in log message: %s\n", hash_split[0]);
        goto fail_log_scope;
    } else {
        log_entry->scope = scope;
    }

    /* Record the contract type */
    ssize_t contract = find_in_array(hash_split[1], mistral_contract_name);
    if (contract == -1) {
        mistral_err("Invalid contract type in log message: %s\n", hash_split[1]);
        goto fail_log_contract;
    } else {
        log_entry->contract_type = contract;
    }

    /* Record the log event time */
    char *p = strptime(hash_split[2], "%FT%T", &log_entry->time);

    if (p == NULL || *p != '\0') {
        mistral_err("Unable to parse date and time in log message: %s\n", hash_split[2]);
        goto fail_log_strptime;
    }

    /* Record the log event time as seconds since epoch mktime will normalise to UTC.
     *
     * Set the daylight savings time value to -1 as log messages will arrive delayed by the update
     * interval so we may have since transitioned between states which will force mktime to use a
     * "best guess".
     */
    log_entry->time.tm_isdst = -1;
    log_entry->epoch.tv_sec = mktime(&log_entry->time);

    if (log_entry->epoch.tv_sec < 0) {
        mistral_err("Unable to convert date and time in log message: %s\n", line);
        goto fail_log_mktime;
    }

    /* Oddly, having parsed this data this way round the timezone information is not accurate in the
     * tm structure. To work around this simply re-populate the structure from the calculated epoch.
     */
    if (localtime_r(&log_entry->epoch.tv_sec, &log_entry->time) == NULL) {
        mistral_err("Unable to calculate timezone in log message: %s\n", hash_split[2]);
        goto fail_log_localtime;
    }

    /* Record the rule label */
    if ((log_entry->label = strdup(comma_split[FIELD_LABEL])) == NULL) {
        mistral_err("Unable to allocate memory for label: %s\n", comma_split[FIELD_LABEL]);
        goto fail_log_label;
    }

    /* Record the rule path */
    if ((log_entry->path = strdup(comma_split[FIELD_PATH])) == NULL) {
        mistral_err("Unable to allocate memory for path: %s\n", comma_split[FIELD_PATH]);
        goto fail_log_label;
    }

    /* Record the rule call types */
    char **call_type_split = str_split(comma_split[FIELD_CALL_TYPE], '+', &field_count);
    if (!call_type_split) {
        mistral_err("Unable to allocate memory for call types: %s\n", comma_split[FIELD_CALL_TYPE]);
        goto fail_log_call_types_split;
    }

    if (!call_type_split[0]) {
        mistral_err("Unable to find call type: %s\n", comma_split[FIELD_CALL_TYPE]);
        goto fail_log_call_types;
    }

    for (char **call_type = call_type_split; call_type && *call_type; ++call_type) {
        ssize_t type = find_in_array(*call_type, mistral_call_type_name);
        if (type == -1) {
            mistral_err("Invalid call type: %s\n", *call_type);
            goto fail_log_call_type;
        } else {
            log_entry->call_type_mask = log_entry->call_type_mask | mistral_call_type_mask[type];
            log_entry->call_types[type] = true;
        }
    }

    /* Record the rule size range, default/missing values are 0 for min, SSIZE_MAX for max */
    char **size_range_split = str_split(comma_split[FIELD_SIZE_RANGE], '-', &field_count);
    if (!size_range_split) {
        mistral_err("Unable to allocate memory for size range: %s\n", comma_split[FIELD_SIZE_RANGE]);
        goto fail_log_size_range_split;
    }

    /* set defaults for both min and max */
    log_entry->size_min = 0;
    log_entry->size_max = SSIZE_MAX;
    log_entry->size_min_unit = UNIT_BYTES;
    log_entry->size_max_unit = UNIT_BYTES;

    if (field_count == 2) {
        uint64_t range;
        /* size range contained a '-' so parse each string that is present (blank => min/max) */
        if (strcmp(size_range_split[0], "")) {
            if (!parse_size(size_range_split[0], &range, &log_entry->size_min_unit)) {
                mistral_err("Unable to parse size range minimum: %s\n", comma_split[FIELD_SIZE_RANGE]);
                goto fail_log_size_range;
            }
            log_entry->size_min = (ssize_t)range;

            if (mistral_unit_type[log_entry->size_min_unit] != UNIT_CLASS_SIZE) {
                mistral_err("Unexpected unit for size range: %s\n", size_range_split[0]);
                goto fail_log_size_range;
            }
        }
        if (strcmp(size_range_split[1], "")) {
            if (!parse_size(size_range_split[1], &range, &log_entry->size_max_unit)) {
                mistral_err("Unable to parse size range maximum: %s\n", comma_split[FIELD_SIZE_RANGE]);
                goto fail_log_size_range;
            }
            log_entry->size_max = (ssize_t)range;

            if (mistral_unit_type[log_entry->size_max_unit] != UNIT_CLASS_SIZE) {
                mistral_err("Unexpected unit for size range: %s\n", size_range_split[1]);
                goto fail_log_size_range;
            }
        }
    } else if (strcmp(size_range_split[0], "all") || field_count > 1) {
        /* Size range was not "all" which is the only other valid value */
        mistral_err("Unable to parse size range: %s\n", comma_split[FIELD_SIZE_RANGE]);
        goto fail_log_size_range;
    }

    /* Finally simply store the raw string */
    if ((log_entry->size_range = strdup(comma_split[FIELD_SIZE_RANGE])) == NULL) {
        mistral_err("Unable to allocate memory for size range: %s\n", comma_split[FIELD_SIZE_RANGE]);
        goto fail_log_size_range;
    }

    /* Record the measurement type */
    ssize_t measurement = find_in_array(comma_split[FIELD_MEASUREMENT], mistral_measurement_name);
    if (measurement == -1) {
        mistral_err("Invalid measurement in log message: %s\n", comma_split[FIELD_MEASUREMENT]);
        goto fail_log_measurement;
    } else {
        log_entry->measurement = measurement;
    }

    /* Record the allowed rate */
    if ((log_entry->threshold_str = strdup(comma_split[FIELD_THRESHOLD])) == NULL) {
        mistral_err("Unable to allocate memory for allowed: %s\n", comma_split[FIELD_THRESHOLD]);
        goto fail_log_allowed;
    }

    /* And also store its constituent parts */
    if (!parse_rate(comma_split[FIELD_THRESHOLD], &log_entry->threshold, &log_entry->threshold_unit,
                    &log_entry->timeframe, &log_entry->timeframe_unit)) {
        goto fail_log_allowed;
    }

    /* Record the observed rate */
    if ((log_entry->measured_str = strdup(comma_split[FIELD_MEASURED])) == NULL) {
        mistral_err("Unable to allocate memory for allowed: %s\n", comma_split[FIELD_MEASURED]);
        goto fail_log_observed;
    }

    /* And also store its constituent parts */
    if (!parse_rate(comma_split[FIELD_MEASURED], &log_entry->measured, &log_entry->measured_unit,
                    &log_entry->measured_time, &log_entry->measured_time_unit)) {
        goto fail_log_observed;
    }

    /* Record the hostname */
    if ((log_entry->full_hostname = strdup(comma_split[FIELD_HOSTNAME])) == NULL) {
        mistral_err("Unable to allocate memory for full hostname: %s\n", comma_split[FIELD_HOSTNAME]);
        goto fail_log_fullhost;

    }

    /* Now we've stored the full hostname save a version trunctated at the first '.' */
    char *dot;
    if ((dot = strchr(comma_split[FIELD_HOSTNAME], '.'))) {
        *dot = '\0';
    }

    if ((log_entry->hostname = strdup(comma_split[FIELD_HOSTNAME])) == NULL) {
        mistral_err("Unable to allocate memory for hostname: %s\n", comma_split[FIELD_HOSTNAME]);
        goto fail_log_host;
    }

    /* Record the pid - because pid_t varies from machine to machine use an int64_t to be safe */
    char *end = NULL;
    errno = 0;
    log_entry->pid = (int64_t)strtoll(comma_split[FIELD_PID], &end, 10);

    if (!end || *end != '\0' || end == comma_split[FIELD_PID] || errno) {
        mistral_err("Invalid PID seen: [%s].\n", comma_split[FIELD_PID]);
        goto fail_log_pid;
    }

    /* Record the CPU ID - this is inlikely to be large but use a uint32_t to future proof */
    end = NULL;
    errno = 0;
    log_entry->cpu = (uint32_t)strtoul(comma_split[FIELD_CPU], &end, 10);

    if (!end || *end != '\0' || end == comma_split[FIELD_CPU] || errno) {
        mistral_err("Invalid CPU ID seen: [%s].\n", comma_split[FIELD_CPU]);
        goto fail_log_cpu;
    }

    /* Record the command
     *
     * Now we need to make an assumption. As it is currently coded we do nothing to escape commas in
     * commands or file names. The command is truncated after roughly 1.5kB though. Assuming we have
     * more than five fields left keep on appending them to the command until either the length
     * limit is reached or we have the right number of fields left.
     */
    size_t field = FIELD_COMMAND;
    char *command = NULL;
    do {
        if (!command) {
            command = strdup(comma_split[field]);
            if (!command) {
                mistral_err("Unable to store command: %s\n", line);
                goto fail_log_command;
            }
        } else {
            char *new_command = NULL;

            if (asprintf(&new_command, "%s,%s", command, comma_split[field]) > 0) {
                free(command);
                command = new_command;
            } else {
                mistral_err("Unable to store command: %s\n", line);
                goto fail_log_command;
            }
        }
        field++;
    } while (field < log_field_count - (FIELD_MAX - FIELD_COMMAND) &&
             strlen(command) + strlen(comma_split[field]) + 2 <= PLUGIN_MESSAGE_CMD_LEN);

    log_entry->command = command;

    /* Record the file name
     *
     * If we break because of field length above we will still have too many fields left so, as
     * above keep appending fields to the filename until we have the right number remaining, this
     * time without the length restriction.
     */
    char *filename = NULL;
    do {
        if (!filename) {
            filename = strdup(comma_split[field]);
            if (!filename) {
                mistral_err("Unable to store filename: %s\n", line);
                goto fail_log_filename;
            }
        } else {
            char *new_filename = NULL;

            if (asprintf(&new_filename, "%s,%s", filename, comma_split[field]) > 0) {
                free(filename);
                filename = new_filename;
            } else {
                mistral_err("Unable to store filename: %s\n", line);
                goto fail_log_filename;
            }
        }
        field++;
    } while (field < log_field_count - (FIELD_MAX - FIELD_FILENAME));

    log_entry->file = filename;

    size_t offset = field - FIELD_FILENAME - 1;

    /* Record the job group id */
    if ((log_entry->job_group_id = (const char *)strdup(comma_split[FIELD_JOB_GROUP_ID + offset])) == NULL) {
        mistral_err("Unable to allocate memory for job group id: %s\n", comma_split[FIELD_JOB_GROUP_ID + offset]);
        goto fail_log_group;
    }

    /* Record the job id */
    if ((log_entry->job_id = (const char *)strdup(comma_split[FIELD_JOB_ID + offset])) == NULL) {
        mistral_err("Unable to allocate memory for job id: %s\n", comma_split[FIELD_JOB_ID + offset]);
        goto fail_log_job;
    }

    /* Record the MPI Rank */
    end = NULL;
    errno = 0;
    log_entry->mpi_rank = (int32_t)strtol(comma_split[FIELD_MPI_RANK + offset], &end, 10);

    if (!end || *end != '\0' || errno) {
        mistral_err("Invalid MPI rank seen: [%s].\n", comma_split[FIELD_MPI_RANK + offset]);
        goto fail_log_mpi_rank;
    }

    CALL_IF_DEFINED(mistral_received_log, log_entry);

    free(size_range_split);
    free(call_type_split);
    free(hash_split);
    free(comma_split);
    DEBUG_OUTPUT("Leaving function, success\n");
    return true;

fail_log_mpi_rank:
fail_log_job:
fail_log_group:
fail_log_filename:
    free(filename);
fail_log_command:
    free(command);
fail_log_cpu:
fail_log_pid:
fail_log_host:
fail_log_fullhost:
fail_log_observed:
fail_log_allowed:
fail_log_measurement:
fail_log_size_range:
    free(size_range_split);
fail_log_size_range_split:
fail_log_call_type:
fail_log_call_types:
    free(call_type_split);
fail_log_call_types_split:
fail_log_label:
fail_log_localtime:
fail_log_mktime:
fail_log_strptime:
fail_log_contract:
fail_log_scope:
fail_log_alloc:
fail_split_hash_fields:
    free(hash_split);
fail_split_hashes:
fail_split_comma_fields:
    free(comma_split);
fail_split_commas:

    CALL_IF_DEFINED(mistral_received_bad_log, line);

    DEBUG_OUTPUT("Leaving function, failure\n");
    return false;
}

/*
 * mistral_destroy_log_entry
 *
 * Used to clean up a log entry structure created by parse_message.
 *
 * Parameters:
 *   log_entry - a pointer to the log entry to destroy
 *
 * Returns:
 *   void
 */
void mistral_destroy_log_entry(mistral_log *log_entry)
{
    DEBUG_OUTPUT("Entering function\n");
    if (log_entry) {
        free((void *)log_entry->label);
        free((void *)log_entry->path);
        free((void *)log_entry->size_range);
        free((void *)log_entry->threshold_str);
        free((void *)log_entry->measured_str);
        free((void *)log_entry->command);
        free((void *)log_entry->file);
        free((void *)log_entry->job_group_id);
        free((void *)log_entry->job_id);
        free((void *)log_entry->hostname);
        free((void *)log_entry->full_hostname);
        free((void *)log_entry);
    }
    DEBUG_OUTPUT("Leaving function, success\n");
}

/*
 * parse_message
 *
 * Check the content of the passed line for valid message data. The function looks at the line in
 * the context of the current state (currently receiving data or not). Trailing new line characters
 * will be removed from the message.
 *
 * Parameters:
 *   line           - Standard null terminated string containing the line to be checked
 *
 * Returns:
 *   PLUGIN_DATA_ERR  - If a control message was recognised but contained invalid data
 *   PLUGIN_FATAL_ERR - If this plug-in does not use a supported API version or was unable to send
 *                      the API version to use to Mistral
 *   value of message - The PLUGIN_MESSAGE_ enum message type seen
 */
static enum mistral_message parse_message(char *line)
{
    DEBUG_OUTPUT("Entering function\n");
    assert(line);

    /* number of data blocks received */
    uint64_t block_count;
    enum mistral_message message = PLUGIN_MESSAGE_DATA_LINE;
    size_t line_len = strlen(line);


    if (line_len > 0 && line[line_len - 1] == '\n') {
        line[line_len - 1] = '\0';
    }
    DEBUG_OUTPUT("Received message \"%s\"\n", line);
#define X(P, V)                               \
    if (strncmp(line, V, sizeof(V) - 1) == 0) \
    {                                         \
        message = PLUGIN_MESSAGE_ ##P;        \
    } else
    PLUGIN_MESSAGE(X)
#undef X
    {
        /* Final else - do nothing */
    }

    /* Do some generic error checking before we handle the message */
    if (!supported_version && message != PLUGIN_MESSAGE_SUP_VERSION &&
        message != PLUGIN_MESSAGE_SHUTDOWN) {
        mistral_err("Message seen before supported versions received [%s].\n", line);
        DEBUG_OUTPUT("Leaving function, failure\n");
        return PLUGIN_DATA_ERR;
    } else if (in_data) {       /* We are currently processing a data block */
        switch (message) {
        case PLUGIN_MESSAGE_INTERVAL:
        case PLUGIN_MESSAGE_SUP_VERSION:
        case PLUGIN_MESSAGE_DATA_START:
            /* Invalid messages */
            mistral_err("Data block incomplete, log data might be corrupted [%s].\n", line);
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        default:
            break;
        }
    } else {
        /* Not in a data block so only control messages are valid */
        if (message == PLUGIN_MESSAGE_DATA_LINE && *line != '\0') {
            mistral_err("Invalid data: [%s]. Expected a control message.\n", line);
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        }
    }

    /* If we have a valid control message it should end with PLUGIN_MESSAGE_END */
    if (message != PLUGIN_MESSAGE_DATA_LINE
        && strcmp(PLUGIN_MESSAGE_END, &line[line_len - sizeof(PLUGIN_MESSAGE_END)])) {
        mistral_err("Invalid data: [%s]. Expected control message.\n", line);
        DEBUG_OUTPUT("Leaving function, failure\n");
        return PLUGIN_DATA_ERR;
    }

    switch (message) {

    case PLUGIN_MESSAGE_USED_VERSION:
        /* We should only send, never receive this message */
        mistral_err("Invalid data: [%s]. Don't expect to receive this message.\n", line);
        DEBUG_OUTPUT("Leaving function, failure\n");
        return PLUGIN_DATA_ERR;
    case PLUGIN_MESSAGE_INTERVAL:{
        /* Only seen in update plug-ins */
        /* Store the update interval */
        char *p = NULL;
        char *end = NULL;

        p = line + mistral_log_msg_len[PLUGIN_MESSAGE_INTERVAL];

        errno = 0;
        interval = (uint64_t)strtoull(p, &end, 10);

        if (interval == 0 || !end || *end != PLUGIN_MESSAGE_SEP_C || errno) {
            mistral_err("Invalid interval seen: [%s].\n", line);
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        }

        mistral_plugin_info.interval = interval;

        CALL_IF_DEFINED(mistral_received_interval, &mistral_plugin_info);

        break;
    }
    case PLUGIN_MESSAGE_SUP_VERSION:{
        /* Extract minimum and current version supported */
        unsigned min_ver = 0;
        unsigned cur_ver = 0;

        /* Due to compiler warnings about string literals etc it is much simpler to hard code this
         * message.
         */
        if (sscanf(line, ":PGNSUPVRSN:%u:%u:\n", &min_ver, &cur_ver) != 2) {
            /* The assert should identify if we've modified the message literal */
            assert(strncmp(mistral_log_message[PLUGIN_MESSAGE_SUP_VERSION], ":PGNSUPVRSN:", 12));
            mistral_err("Invalid supported versions format received: [%s].\n", line);
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        }

        if (min_ver == 0 || cur_ver == 0 || min_ver > cur_ver) {
            mistral_err("Invalid supported version numbers received: [%s].\n", line);
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        }

        if (ver < min_ver || ver > cur_ver) {
            mistral_err("API version used [%u] is not supported [%s].\n", ver, line);
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_FATAL_ERR;
        } else {
            supported_version = true;
            if (!send_message_to_mistral(PLUGIN_MESSAGE_USED_VERSION)) {
                DEBUG_OUTPUT("Leaving function, failure\n");
                return PLUGIN_FATAL_ERR;
            }
        }
        break;
    }
    case PLUGIN_MESSAGE_DATA_START:{
        /* Check if the data block number is one higher than the last version seen */
        char *p = NULL;
        char *end = NULL;
        bool error_seen = false;

        p = line + mistral_log_msg_len[PLUGIN_MESSAGE_DATA_START];

        errno = 0;
        block_count = (uint64_t)strtoull(p, &end, 10);

        if (block_count == 0 || !end || *end != PLUGIN_MESSAGE_SEP_C || errno) {
            mistral_err("Invalid data block number seen: [%s].\n", line);
            error_seen = true;
        }

        if (block_count != data_count + 1) {
            mistral_err("Unexpected data block number %"PRIu64" seen (expected %"PRIu64").\n",
                        block_count, data_count + 1);
            error_seen = true;
        }

        CALL_IF_DEFINED(mistral_received_data_start, block_count, error_seen);

        in_data = true;
        data_count = block_count;
        if (error_seen) {
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        }
        break;
    }
    case PLUGIN_MESSAGE_DATA_END:{
        /* Check if the data block number matches the last value seen */
        uint64_t end_block_count = 0;
        char *p = NULL;
        char *end = NULL;
        bool error_seen = false;

        p = line + mistral_log_msg_len[PLUGIN_MESSAGE_DATA_END];

        errno = 0;
        end_block_count = (uint64_t)strtoull(p, &end, 10);

        if (end_block_count == 0 || !end || *end != PLUGIN_MESSAGE_SEP_C || errno) {
            mistral_err("Invalid data block number seen: [%s].\n", line);
            error_seen = true;
        }

        if (data_count != end_block_count) {
            mistral_err("Unexpected data block number %"PRIu64" seen (expected %"PRIu64"), data may be corrupt.\n",
                        end_block_count, data_count);
            error_seen = true;
        }
        in_data = false;

        CALL_IF_DEFINED(mistral_received_data_end, end_block_count, error_seen);

        if (error_seen) {
            DEBUG_OUTPUT("Leaving function, failure\n");
            return PLUGIN_DATA_ERR;
        }
        break;
    }
    case PLUGIN_MESSAGE_SHUTDOWN:
        /* Handle a clean shut down */
        shutdown_message = true;
        CALL_IF_DEFINED(mistral_received_shutdown);
        break;
    default:
        /* Should only get here with a contract line */
        /* This is output plug-in specific */
        if (!parse_log_entry((const char *)line)) {
            mistral_err("Invalid log message received: %s.\n", line);
        }
        break;

    } /* End of message types */

    DEBUG_OUTPUT("Leaving function, success\n");
    return message;
}

/*
 * read_data_from_mistral
 *
 * Function that reads from STDIN_FILENO file descriptor.
 * Calls parse_message() to parse the received message.
 *
 * Parameters:
 *   None
 *
 * Returns:
 *  true on success
 *  false otherwise
 */
static bool read_data_from_mistral(void)
{
    DEBUG_OUTPUT("Entering function\n");
    int result;
    /* waiting time set to 10 seconds, this is used to wait for the first message to be seen */
    struct timeval timeout = {
        .tv_sec = 10,
        .tv_usec = 0,
    };

    fd_set read_set;

    /* check stdin to see when it has input. */
    FD_ZERO(&read_set);
    FD_SET(STDIN_FILENO, &read_set);
    char *line = NULL;
    size_t line_length = 0;

    bool retval = true;

    do {
        result = select(1 + STDIN_FILENO, &read_set, NULL, NULL, &timeout);
    } while (result == -1 && errno == EINTR);   /* retry if interrupted */

    if (result < 0) {
        char buf[256];
        mistral_err("Error in select() while reading from mistral: %s.\n",
                    strerror_r(errno, buf, sizeof buf));
        goto read_fail_select;
    }

    if (FD_ISSET(STDIN_FILENO, &read_set)) {
        /* Data is available now. */
        while (!mistral_shutdown && getline(&line, &line_length, stdin) > 0) {
            enum mistral_message message = parse_message(line);

            if (message == PLUGIN_MESSAGE_SHUTDOWN) {
                /* Stop processing */
                goto read_shutdown;

            } else if (message == PLUGIN_DATA_ERR) {
                /* Ignore bad data */
                continue;

            } else if (message == PLUGIN_FATAL_ERR) {
                /* But do not continue if a serious error was seen. */
                retval = false;
                goto read_error;
            }
        }
    }

read_error:
read_shutdown:
    free(line);

read_fail_select:
    DEBUG_OUTPUT("Leaving function, success\n");
    return retval;
}

/*
 * main
 *
 * Call the mistral_startup function to get required initialisation for the plug-in then start the
 * main read and processing loop.
 *
 * Parameters:
 *   argc - The number of arguments in the argv array
 *   argv - An array of standard null terminated character arrays containing the command line
 *          parameters
 *
 * Returns:
 *   EXIT_SUCCESS
 */
int main(int argc, char **argv)
{
    mistral_plugin_pid = getpid();
    DEBUG_OUTPUT("Entering function\n");
    mistral_plugin_info.type = MAX_PLUGIN;
    mistral_plugin_info.error_log = stderr;
    init_mistral_call_type_names();

    /* used to set the type of plug-in we should run as */
    mistral_startup(&mistral_plugin_info, argc, argv);

    if (mistral_plugin_info.type != MAX_PLUGIN) {
        read_data_from_mistral();
    }

    if (!shutdown_message) {
        /* Mistral did not tell us to exit */
        send_message_to_mistral(PLUGIN_MESSAGE_SHUTDOWN);
    }

    DEBUG_OUTPUT("Leaving function, success\n");
    CALL_IF_DEFINED(mistral_exit);
    return EXIT_SUCCESS;
}
