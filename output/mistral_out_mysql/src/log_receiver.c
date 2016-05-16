#include <assert.h>
#include <errno.h>
#include <getopt.h>              /* getopt_long() */
#include <limits.h>
#include <sys/syscall.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log_receiver.h"
#include "log_to_mysql.h"

static unsigned ver = VERSION;          /* Supported version */
static unsigned data_count = 0;         /* Number of data blocks received */
static bool in_data = false;            /* True, if mistral is sending data */
static bool sup_ver = false;            /* True, if supported versions were received */

static FILE *log_fs = NULL;

/* Define how long to wait for reads before shutting down. */
const struct timeval shutdown_delay = {
    .tv_sec = 1,
    .tv_usec = 0,
};

/*
 * send_message_to_mistral()
 *
 * Sends a message to mistral
 *
 * Input:
 *   message    - Standard null terminated string containing the message to be
 *                passed
 *
 * Returns:
 *   False - On error
 *   True  - Otherwise
 */
static bool send_message_to_mistral(const char *message)
{
    const char *message_start = message;
    size_t message_len = strlen(message);

    fd_set writeset;
    struct timeval tv;
    int retval;
    FD_ZERO(&writeset);
    FD_SET(FD_OUTPUT, &writeset);

    /* Wait up to a default number of seconds. */
    tv = shutdown_delay;

    /* Keep trying to send data until all the message has been sent or an error
     * occurs.
     */
    while (message_len) {
        do {
            retval = select(1 + FD_OUTPUT, NULL, &writeset, NULL, &tv);

            if (retval == -1) {
                char buf[256];
                fprintf(log_fs, "Error in select() while writing to mistral: %s\n",
                        strerror_r(errno, buf, sizeof buf));
                return false;
            } else if (retval) {
                ssize_t w_res =
                    write(FD_OUTPUT, message_start, message_len);

                /* If we've successfully written some data, update the message
                 * pointer and length values
                 */
                if (w_res >= 0) {
                    message_start += w_res;
                    message_len -= w_res;
                }

                if ((w_res < 0 && errno == EINTR) ||
                        (w_res < 0 && errno == EAGAIN) ||
                        (w_res >= 0 && message_len != 0)) {
                    /* Try and send the remaining portion of the message again
                     * if we were interrupted, encountered a full pipe or only
                     * managed a partial write. The pipe will be retested in
                     * case the mistral_log has exited.
                     */
                    continue;
                }
                if (message_len != 0) {
                    char buf[256];
                    fprintf(log_fs, "Failed write, unable to send data: (%s)\n",
                            strerror_r(errno, buf, sizeof buf));
                    return false;
                }
            } else {
                fprintf(log_fs, "Not ready to receive data\n");
                return false;
            }
        } while (retval == -1 && errno == EINTR);
    }
    return true;
}

/*
 * send_shutdown_to_mistral()
 *
 * Function sends a shutdown message to mistral
 *
 * Returns:
 *   False - On error
 *   True  - Otherwise
 */
static bool send_shutdown_to_mistral()
{
    char *message_string = NULL;
    bool ret = true;

    if (asprintf(&message_string, "%s",
                 mistral_log_message[PLUGIN_MESSAGE_SHUTDOWN]) < 0) {
        fprintf(log_fs, "Unable to construct shutdown message.\n");
        ret = false;
    } else {
        if (!send_message_to_mistral((const char *)message_string)) {
            ret = false;
        }
        free(message_string);
    }
    return ret;

}

/*
 * send_version_to_mistral()
 *
 * Function sends a start of data message
 *
 * Returns:
 *   False - On error
 *   True  - Otherwise
 */
static bool send_version_to_mistral()
{
    char *message_string = NULL;
    bool ret = true;

    if (asprintf(&message_string, "%s%u%s",
                 mistral_log_message[PLUGIN_MESSAGE_USED_VERSION],
                 ver, PLUGIN_MESSAGE_END) < 0) {
        fprintf(log_fs, "Unable to construct used version message.\n");
        ret = false;
    } else {
        if (!send_message_to_mistral((const char *)message_string)) {
            ret = false;
        }
        free(message_string);
    }
    return ret;
}

/*
 * check_for_message()
 *
 * Check the content for valid message data
 *
 * Function looks at the line in the context of the current state (currently
 * receiving data or not).
 *
 * Input:
 *   line           - Standard null terminated string containing the line to be
 *                    checked
 * Returns:
 *   PLUGIN_DATA_ERR  - If a control message was recognised but contained
 *                      invalid data
 *   value of message - The PLUGIN_MESSAGE_ enum message type seen
 */
static int check_for_message(char *line)
{
    /* number of data blocks received */
    unsigned block_count = data_count;
    int message = PLUGIN_MESSAGE_DATA_LINE;
    size_t line_len = strlen(line);

#define X(P, V) \
    if (strncmp(line, V, sizeof(V)-1) == 0) \
    { \
        message = PLUGIN_MESSAGE_ ##P; \
    } else
    PLUGIN_MESSAGE(X)
#undef X
    {
        /* Final else - do nothing */
    }

    /* Do some generic error checking before we handle the message */
    if (!sup_ver
            && message != PLUGIN_MESSAGE_SUP_VERSION
            && message != PLUGIN_MESSAGE_SHUTDOWN ) {
        /* Strip any trailing newline for readability */
        STRIP_NEWLINE(line, line_len);

        fprintf(log_fs, "Message seen before supported versions received [%s].\n", line);
        return PLUGIN_DATA_ERR;
    } else if (in_data) {/* Already sent data to the mistral_log. */
        switch (message) {
        case PLUGIN_MESSAGE_INTERVAL:
        case PLUGIN_MESSAGE_SUP_VERSION:
        case PLUGIN_MESSAGE_DATA_START: {
            /* Invalid messages */
            /* Strip any trailing newline for readability */
            STRIP_NEWLINE(line, line_len);

            fprintf(log_fs, "Data block incomplete - saw [%s].\nLog data might be corrupted.\n", line);
            return PLUGIN_DATA_ERR;
        }
        }
    } else {
        /* Not in a data block so only control messages are valid */
        if (message == PLUGIN_MESSAGE_DATA_LINE && *line != '\n') {
            /* strip any trailing newline for readability */
            STRIP_NEWLINE(line, line_len);

            fprintf(log_fs, "Invalid data: [%s]. Expected a control message.\n", line);
            return PLUGIN_DATA_ERR;
        }
    }

    /* If we have a valid control message it should end with PLUGIN_MESSAGE_END */
    if (message != PLUGIN_MESSAGE_DATA_LINE
            && strstr(line, PLUGIN_MESSAGE_END) != &line[line_len-sizeof(PLUGIN_MESSAGE_END)+1]) {
        /* strip any trailing newline for readability */
        STRIP_NEWLINE(line, line_len);

        fprintf(log_fs, "Invalid data: [%s]. Expected log message or PLUGIN_MESSAGE_END.\n", line);
        return PLUGIN_DATA_ERR;
    }

    /* now we've validated strip trailing new lines */
    if (message >= 0) {
        line[line_len-1] = '\0';
    }

    switch (message) {

    case PLUGIN_MESSAGE_USED_VERSION:
    case PLUGIN_MESSAGE_INTERVAL: {
        /* We should never receive this message */
        fprintf(log_fs, "Invalid data: [%s]. Don't expect to receive this message.\n", line);
        return PLUGIN_DATA_ERR;
    }
    case PLUGIN_MESSAGE_SUP_VERSION: {
        /* Extract minimun and current version supported */
        unsigned min_ver = 0;
        unsigned cur_ver = 0;

        if (sscanf(line, ":PGNSUPVRSN:%u:%u:\n",&min_ver, &cur_ver) != 2) {
            fprintf(log_fs, "Invalid supported versions format received: [%s].\n", line);
            return PLUGIN_DATA_ERR;
        }

        if (min_ver <= 0 || cur_ver <= 0 || min_ver > cur_ver) {
            fprintf(log_fs, "Invalid supported version numbers received: [%s].\n", line);
            return PLUGIN_DATA_ERR;
        }

        if (ver < min_ver || ver > cur_ver) {
            fprintf(log_fs, "Invalid mistral_log version: [%u] for the supported versions [%s].\n",
                    ver, line);
            return PLUGIN_FATAL_ERR;
        } else {
            sup_ver = true;
            if (!send_version_to_mistral()) {
                return PLUGIN_FATAL_ERR;
            }
        }
        break;
    }
    case PLUGIN_MESSAGE_DATA_START: {
        /* Check if the contract number matches the last version we sent */
        char *p = NULL;
        char *end = NULL;

        p = line + mistral_log_msg_len[PLUGIN_MESSAGE_DATA_START];

        block_count = strtoul(p, &end, 10);

        if (block_count <= 0 || block_count > UINT_MAX || *end != PLUGIN_MESSAGE_SEP_C) {
            fprintf(log_fs, "Invalid contract count: [%s].\n", line);
            return PLUGIN_DATA_ERR;
        }
        in_data = true;
        data_count = block_count;
        break;
    }
    case PLUGIN_MESSAGE_DATA_END: {
        /* Check if the contract number matches the last version we sent */
        unsigned  end_block_count = 0;
        char *p = NULL;
        char *end = NULL;

        p = line + mistral_log_msg_len[PLUGIN_MESSAGE_DATA_END];

        end_block_count = strtoul(p, &end, 10);

        if (end_block_count <= 0 || end_block_count > UINT_MAX ||
                *end != PLUGIN_MESSAGE_SEP_C) {
            fprintf(log_fs, "Invalid contract count: [%s].\n", line);
            return PLUGIN_DATA_ERR;
        }

        if (data_count != end_block_count ) {
            fprintf(log_fs,
                    "Contract instance count mismatch. Saw [%d], expected [%d].\nLog data might be corrupted.\n",
                    end_block_count, data_count);
            return PLUGIN_DATA_ERR;
        }
        in_data = false;
        break;
    }
    default: {
        /* Should only get here with a contract line - do nothing,
         * this should be handled in the calling function.
         */
    }

    } /* End of message types */

    return message;
}

static char **str_split(const char *s, int sep, size_t *sep_count)
{
    size_t n = 1;               /* One more string than separators. */
    size_t len;                 /* Length of 's' */

    assert(sep_count);
    /* Count separators. */
    for (len = 0; s[len]; ++len) {
        n += (sep == s[len]);
    }
    *sep_count = n;

    /* Allocate the result array (including space for a NULL at the
     * end), plus space for a copy of 's'.
     */
    void *alloc = calloc(1, len + 1 + (n + 1) * sizeof(char *));
    if (!alloc) {
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
    return result;
}

/*
 * parse_log_entry()
 *
 * Parse the line received. The line structure is <scope>:<type>:log_message
 * log_message contains comma separated strings.
 */
static bool parse_log_entry(char *line)
{
    bool ret = true;
    size_t sep_count;
    char **comma_splits = str_split(line, ',', &sep_count);
    if (sep_count != COMMA_SEP_NUM) {
        fprintf(log_fs, "Invalid log message: %s\n", line);
        ret = false;
        goto err_comma_nr;
    }
    char **semicolon_splits = str_split(comma_splits[0], ':', &sep_count);
    if (sep_count != COLON_SEP_NUM) {
        fprintf(log_fs, "Invalid log message: %s\n", line);
        ret = false;
        goto err_sc_nr;
    }

    mistral_log_entry_s log_entry = {0};
    mistral_log_msg_s msg = {0};
    log_entry.log_msg = &msg;


    log_entry.scope = semicolon_splits[0];
    log_entry.type = semicolon_splits[1];

    int i;
    char tmp_timestamp[30];
    strcpy(tmp_timestamp, semicolon_splits[2]);
    /* Combining again timestamp because it contained ':' which was used as separator
     * for the split. The timestamp string was splitted in three parts.
     */
    for (i = 3; semicolon_splits[i]; i++) {
        strcat(tmp_timestamp, ":");
        strcat(tmp_timestamp, semicolon_splits[i]);
    }
    log_entry.log_msg->timestamp = tmp_timestamp;

    log_entry.log_msg->label = comma_splits[1];
    log_entry.log_msg->path = comma_splits[2];
    log_entry.log_msg->measurement = comma_splits[3];
    log_entry.log_msg->call_type_set = comma_splits[4];
    log_entry.log_msg->observed = comma_splits[5];
    log_entry.log_msg->limit = comma_splits[6];
    log_entry.log_msg->pid = comma_splits[7];
    log_entry.log_msg->command = comma_splits[8];
    log_entry.log_msg->file_name = comma_splits[9];
    log_entry.log_msg->gid = comma_splits[10];
    log_entry.log_msg->jid = comma_splits[11];

    ret = write_log_to_db(&log_entry);
err_sc_nr:
    free(semicolon_splits);
err_comma_nr:
    free(comma_splits);
    return ret;
}

/*
 * read_data_from_mistral()
 *
 * Function that reads from FD_INPUT file descriptor.
 * Calls parse_log_entry() to parse the received message.
 */
static bool read_data_from_mistral()
{
    int retval;
    struct timeval tv;

    /* waiting time set to 0 */
    tv = shutdown_delay;
    fd_set readset;

    /* check stdin (fd 0) to see when it has input. */
    FD_ZERO(&readset);
    FD_SET(FD_INPUT, &readset);
    char *line = NULL;
    size_t line_length = 0;

    bool ret = false;

    do {
        retval = select(1 + FD_INPUT, &readset, NULL, NULL, &tv);

        if (retval == -1) {
            char buf[256];
            fprintf(log_fs, "Error in select() while reading from mistral: %s.\n",
                    strerror_r(errno, buf, sizeof buf));
            goto read_fail_select;
        } else if (retval) {
            /*Data is available now. */
            /* FD_ISSET(0, &readset) will be true. */
            while (getline(&line, &line_length, stdin) > 0) {
                int message = check_for_message(line);
                if (message == PLUGIN_DATA_ERR) {
                    /* ignore bad data */
                    continue;
                } else if (message == PLUGIN_FATAL_ERR) {
                    /* Not ignored. */
                    goto read_err;
                }

                /* The contract should start here. */
                if (in_data) {
                    if (message == PLUGIN_MESSAGE_SHUTDOWN) {
                        ret = true;
                        goto read_err;
                    }
                }

                if (message == PLUGIN_MESSAGE_DATA_LINE) {
                    line[line_length-1] = '\0';
                    line_length = 0;
                    if (!parse_log_entry(line)) {
                        fprintf(log_fs, "Invalid log message received: %s.\n",line);
                    }
                } else if (message == PLUGIN_MESSAGE_DATA_END) {
                    /* Entire contract data block received */
                }

                if (message == PLUGIN_MESSAGE_SHUTDOWN) {
                    ret = true;
                    goto read_err;
                }
            }
        }
    } while (retval == -1 && errno == EINTR);

    ret = true;
read_err:
    free(line);
read_fail_select:
    return ret;
}

static bool create_tmp_err_log()
{
    int ppid = syscall(SYS_getppid);
    bool ret = false;


    if (ppid == -1) {
        fprintf(log_fs, "Couldn't get the parent pid.\n");
    } else {
        char *path_err_log_file = NULL;
        if (asprintf(&path_err_log_file, "/tmp/err_log_%lu", ppid) < 0) {
            fprintf(log_fs, "Unable to construct error log file path.\n");
        } else {
            log_fs = fopen(path_err_log_file,"a");
            char buf[256];
            if (log_fs < 0) {
                fprintf(log_fs, "Unable to create error log file \"%s\"\n",
                        strerror_r(errno, buf, sizeof buf));
            } else {
                ret = true;
            }
        }
    }
    return ret;
}

static int main(int argc, char **argv)
{

    static const struct option options[] = {
        {"config", required_argument, NULL, 'c'},
        {"output", required_argument, NULL, 'o'},
        {0, 0, 0, 0},
    };

    const char *config_file = NULL;
    const char *output_file = NULL;
    int opt;

    while ((opt = getopt_long(argc, argv, "c:o:", options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
            break;
        case 'o':
            output_file = optarg;
            break;
        default:
            exit(EXIT_FAILURE);
            break;
        }
    }

    if (output_file != NULL) {
        log_fs = fopen(output_file, "a");
        if (!log_fs) {
            char buf[256];
            fprintf(stderr, "Could not open output file %s: %s", output_file,
                    strerror_r(errno, buf, sizeof buf));
        }
    } else {
        if (!create_tmp_err_log()) {
            return EXIT_FAILURE;
        }
    }
    if (config_file == NULL) {
        fprintf(stderr, "Missing option -c. Impossible to connect to the database without specifying a configuration file.\n");
        exit(EXIT_FAILURE);
    }

    if (!connect_to_db(config_file)) {
        fprintf(stderr, "Failed to connect to the db.\n");
        return EXIT_FAILURE;
    }
    dup2(fileno(log_fs), fileno(stderr));

    read_data_from_mistral();
    send_shutdown_to_mistral();
    disconnect_from_db();
    fclose(log_fs);
    return EXIT_SUCCESS;
}

