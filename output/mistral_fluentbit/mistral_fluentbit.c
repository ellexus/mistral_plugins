#include <errno.h>              /* errno */
#include <fcntl.h>              /* open */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */
#include <sys/stat.h>           /* open, umask */
#include <sys/types.h>          /* open, umask */
#include <sys/time.h>           /* gettimeofday */

#include "mistral_plugin.h"
#include "mistral_fluentbit_tcp.h"

#define VALID_NAME_CHARS "1234567890abcdefghijklmnopqrstvuwxyzABCDEFGHIJKLMNOPQRSTVUWXYZ-_"
#define MISTRAL_MAX_BUFFER_SIZE 512

static FILE **log_file_ptr = NULL;
static char *url = NULL;
static char *auth = NULL;

static struct timeval mistral_plugin_start;
static struct timeval mistral_plugin_end;

static mistral_fluentbit_tcp_ctx_s fluentbit_tcp_ctx;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;

static char *custom_variables = NULL;

struct saved_resp {
    size_t size;
    char *body;
};

/*
 * usage
 *
 * Output a usage message via mistral_err.
 *
 * Parameters:
 *   name   - A pointer to a string containing arg[0]
 *
 * Returns:
 *   void
 */
static void usage(const char *name)
{
    /* This may be called before options have been processed so errors will go
     * to stderr. While this is designed to be run with Mistral to make the
     * messages understandable on a terminal add an explicit newline to each
     * line.
     */
    mistral_err("Usage:\n"
                "  %s [-h host] [-p port] [-e file] [-m octal-mode]\n",
                name);
    mistral_err("\n"
                "  --error=file\n"
                "  -e file\n"
                "     Specify location for error log. If not specified all errors will\n"
                "     be output on stderr and handled by Mistral error logging.\n"
                "\n"
                "  --host=hostname\n"
                "  -h hostname\n"
                "     The hostname of the Elasticsearch server with which to establish a\n"
                "     connection. If not specified the plug-in will default to \"localhost\".\n"
                "\n"
                "  --mode=octal-mode\n"
                "  -m octal-mode\n"
                "     Permissions used to create the error log file specified by the -e\n"
                "     option.\n"
                "\n"
                "  --port=number\n"
                "  -p number\n"
                "     Specifies the port to connect to on the Elasticsearch server host.\n"
                "     If not specified the plug-in will default to \"9200\".\n"
                "\n");
}

/*
 * fluentbit_escape
 *
 * Fluent Bit uses JSON output which uses double quotes to delimit strings.
 * There are several special characters that must be escaped using a single
 * backslash character inside these strings.
 *
 * This function allocates twice as much memory as is required to copy the
 * passed string and then copies the string character by character escaping
 * the common characters that need special treatment as they are encountered.
 * Once the copy is complete the memory is reallocated to reduce wastage.
 *
 * Parameters:
 *   string - The string whose content needs to be escaped
 *
 * Returns:
 *   A pointer to newly allocated memory containing the escaped string or
 *   NULL on error
 */
static char *fluentbit_escape(const char *string)
{
    if (!string) {
        return NULL;
    }
    size_t len = strlen(string);

    char *escaped = calloc(1, (2 * len + 1) * sizeof(char));
    char *p, *q;
    if (escaped) {
        for (p = (char *)string, q = escaped; *p; p++, q++) {
            switch (*p) {
            case '"':
                *q++ = '\\';
                *q = '"';
                break;
            case '\\':
                *q++ = '\\';
                *q = '\\';
                break;
            case '\b':
                *q++ = '\\';
                *q = 'b';
                break;
            case '\f':
                *q++ = '\\';
                *q = 'f';
                break;
            case '\n':
                *q++ = '\\';
                *q = 'n';
                break;
            case '\r':
                *q++ = '\\';
                *q = 'r';
                break;
            case '\t':
                *q++ = '\\';
                *q = 't';
                break;
            default:
                *q = *p;
                break;
            }
        }
        /* Memory was allocated with calloc so string is already null terminated */
        char *small_escaped = realloc(escaped, q - escaped + 1);
        if (small_escaped) {
            return small_escaped;
        } else {
            return escaped;
        }
    } else {
        return NULL;
    }
}

/*
 * mistral_startup
 *
 * Required function that initialises the type of plug-in we are running. This
 * function is called immediately on plug-in start-up.
 *
 * In addition this plug-in needs to initialise the Fluent Bit connection
 * parameters. The stream used for error messages defaults to stderr but can
 * be overridden here by setting plugin->error_log.
 *
 * Parameters:
 *   plugin - A pointer to the plug-in information structure. This function
 *            must set plugin->type before returning, if it doesn't the plug-in
 *            will immediately shut down.
 *   argc   - The number of entries in the argv array
 *   argv   - A pointer to the argument array passed to main.
 *
 * Returns:
 *   void - but see note about setting plugin->type above.
 */
void mistral_startup(mistral_plugin *plugin, int argc, char *argv[])
{
    /* Returning without setting plug-in type will cause a clean exit */

    static const struct option options[] = {
        {"error", required_argument, NULL, 'e'},
        {"host", required_argument, NULL, 'h'},
        {"port", required_argument, NULL, 'p'},
        {0, 0, 0, 0},
    };

    const char *error_file = NULL;
    const char *host = "localhost";
    uint16_t port = 5170; /* This is the default TCP server port for Fluent Bit TCP connectivity */
    int opt;
    mode_t new_mode = 0;

    /* This is thread safe because the receiving thread hasn't started at this point */
    gettimeofday(&mistral_plugin_start, NULL);
    timerclear(&mistral_plugin_end);

    mistral_fluentbit_init(&fluentbit_tcp_ctx);

    while ((opt = getopt_long(argc, argv, "e:h:p:v:", options, NULL)) != -1) {
        switch (opt) {
        case 'e':
            error_file = optarg;
            break;
        case 'h':
            host = optarg;
            break;
        case 'p': {
            char *end = NULL;
            unsigned long tmp_port = strtoul(optarg, &end, 10);
            if (tmp_port == 0 || tmp_port > UINT16_MAX || !end || *end) {
                mistral_err("Invalid port specified %s\n", optarg);
                return;
            }
            port = (uint16_t)tmp_port;
            break;
        }
        default:
            usage(argv[0]);
            return;
        }
    }

    log_file_ptr = &(plugin->error_log);

    if (error_file != NULL) {
        if (new_mode > 0) {
            mode_t old_mask = umask(00);
            int fd = open(error_file, O_CREAT | O_WRONLY | O_APPEND, new_mode);
            if (fd >= 0) {
                plugin->error_log = fdopen(fd, "a");
            }
            umask(old_mask);
        } else {
            plugin->error_log = fopen(error_file, "a");
        }

        if (!plugin->error_log) {
            plugin->error_log = stderr;
            char buf[256];
            mistral_err("Could not open error file %s: %s\n", error_file,
                        strerror_r(errno, buf, sizeof buf));
        }
    }

    /* Returning after this point indicates success */
    plugin->type = OUTPUT_PLUGIN;

    mistral_fluentbit_connect(&fluentbit_tcp_ctx, host, port, NULL, NULL);
}

/*
 * mistral_exit
 *
 * Function called immediately before the plug-in exits. Check for any unhandled
 * log entries and call mistral_received_data_end to process them if any are
 * found. Clean up any open error log and the libcurl connection.
 *
 * Parameters:
 *   None
 *
 * Returns:
 *   void
 */
void mistral_exit(void)
{
    if (log_list_head) {
        mistral_received_data_end(0, false);
    }

    free(custom_variables);
    free(auth);
    free(url);

    if (log_file_ptr && *log_file_ptr != stderr) {
        fclose(*log_file_ptr);
        *log_file_ptr = stderr;
    }
}

/*
 * mistral_received_log
 *
 * Function called whenever a log message is received. Simply store the log
 * entry received in a linked list until we reach the end of this data block
 * as it is more efficient to send all the entries to Fluent Bit in a single
 * request.
 *
 * Parameters:
 *   log_entry - A Mistral log record data structure containing the received
 *               log information.
 *
 * Returns:
 *   void
 */
void mistral_received_log(mistral_log *log_entry)
{
    if (!log_list_head) {
        /* Initialise linked list */
        log_list_head = log_entry;
        log_list_tail = log_entry;
        insque(log_entry, NULL);
    } else {
        insque(log_entry, log_list_tail);
        log_list_tail = log_entry;
    }
}

/*
 * mistral_simplify_command
 *
 * Function called to simplify the command which is reported to be the highest
 * offender of the rule. For instance '/bin/ls -l -a' would be simpliefied to
 * 'ls'.
 * If the command is badly formed, then NULL is returned.
 *
 *
 * Parameters:
 *   command - A command that the called wants simplified.
 *
 * Returns:
 *   A pointer to a global buffer which stores the the simpliefied command
 *   NULL on error
 */
const char *mistral_simplify_command(const char *command)
{
    static char buffer[MISTRAL_MAX_BUFFER_SIZE];
    char tmp_buffer[MISTRAL_MAX_BUFFER_SIZE];

    if (!command) {
        return NULL;
    }

    int len = strlen(command);

    if (!len) {
        return NULL;
    }

    if (len > MISTRAL_MAX_BUFFER_SIZE - 1) {
        return NULL;
    }

    strcpy(tmp_buffer, command);

    char *start  = tmp_buffer;
    char *end = &tmp_buffer[len];
    char *result = NULL;

    result = strchr(tmp_buffer, ' ');
    if (result) {
        end = result;
        *end = '\0';
    }

    len = strlen(tmp_buffer);

    result = strrchr(tmp_buffer, '/');
    if (result) {
        /* start points to the last '/' character in the command */

        if (result == &tmp_buffer[len - 1]) {
            return NULL;
        }
        start = ++result;
    }

    strcpy(buffer, start);

    return buffer;
}

/*
 * mistral_received_data_end
 *
 * Function called whenever an end of data block message is received. At this
 * point run through the linked list of log entries we have seen and send them
 * to Fluent Bit. Remove each log_entry from the linked list as they are
 * processed and destroy them.
 *
 * No special handling of data block number errors is done beyond the message
 * logged by the main plug-in framework. Instead this function will simply
 * attempt to log any data received as normal.
 *
 * On error the mistral_shutdown flag is set to true which will cause the
 * plug-in to exit cleanly.
 *
 * Parameters:
 *   block_num   - The data block number that was sent in the message. Unused.
 *   block_error - Was an error detected in the block number. May indicate data
 *                 corruption. Unused.
 *
 * Returns:
 *   void
 */
void mistral_received_data_end(uint64_t block_num, bool block_error)
{
    UNUSED(block_num);
    UNUSED(block_error);

    char *data = NULL;
    struct timeval time_elapsed;
    mistral_log *log_entry = log_list_head;
    uint64_t calculated_timeframe = 0;

    size_t date_len = sizeof("YYYY-MM-DD"); /* strftime format = %F */
    size_t ts_len = sizeof("YYYY-MM-DDTHH:MI:SS"); /* = %FT%T */
    /*size_t type_len = sizeof(",\"_type\":\"throttle\""); / * Max possible len * / */
    char strdate[date_len];
    char strts[ts_len];
    struct tm utc_time;

    while (log_entry) {
        /* Calculate local time */
        if (localtime_r(&log_entry->epoch.tv_sec, &utc_time) == NULL) {
            mistral_err("Unable to calculate UTC time for log message: %ld\n",
                        log_entry->epoch.tv_sec);
            free(data);
            mistral_shutdown();
            return;
        }

        /* This is thread-safe because we access and modify mistral_plugin_start and
         * mistral_plugin_end
         * only in this thread. */

        if (!timerisset(&mistral_plugin_end)) {
            gettimeofday(&mistral_plugin_end, NULL);
        }

        timersub(&mistral_plugin_end, &mistral_plugin_start, &time_elapsed);
        calculated_timeframe = 1000000 * time_elapsed.tv_sec + time_elapsed.tv_usec;

        strftime(strdate, date_len, "%F", &utc_time);
        strftime(strts, ts_len, "%FT%T", &utc_time);

        /* Command and filename must be JSON escaped */
        char *command = fluentbit_escape(log_entry->command);
        char *file = fluentbit_escape(log_entry->file);
        char *path = fluentbit_escape(log_entry->path);
        const char *job_gid = (log_entry->job_group_id[0] == 0) ? "N/A" : log_entry->job_group_id;
        const char *job_id = (log_entry->job_id[0] == 0) ? "N/A" : log_entry->job_id;
        char generic_id[MISTRAL_MAX_BUFFER_SIZE];
        char *new_data = NULL;

        /* We create the content for the genereric_id. The generic_id is used for queries when the
         * there is no job id present. The requirement for the generic_id came from Arm.
         */
        char *user_name = getenv("USER");
        if (!user_name) {
            user_name = "";
        }
        const char *simplified_command = mistral_simplify_command(command);
        if (!simplified_command) {
            simplified_command = "unknown";
        }

        if (snprintf(generic_id, MISTRAL_MAX_BUFFER_SIZE, "%s@%s_%s", getenv("USER"),
                     log_entry->hostname, simplified_command) > MISTRAL_MAX_BUFFER_SIZE)
        {
            mistral_err("The generic_id has been truncated\n");
        }

        if (asprintf(&new_data,
                     "{\"timestamp\": \"%s.%03" PRIu32 "Z\","
                     "\"rule\":{"
                     "\"scope\":\"%s\","
                     "\"type\":\"%s\","
                     "\"label\":\"%s\","
                     "\"measurement\":\"%s\","
                     "\"calltype\":\"%s\","
                     "\"path\":\"%s\","
                     "\"threshold\":%" PRIu64 ","
                     "\"timeframe\":%" PRIu64 ","
                     "\"size-min\":%" PRIu64 ","
                     "\"size-max\":%" PRIu64
                     "},"
                     "\"job\":{"
                     "\"host\":\"%s\","
                     "\"job-group-id\":\"%s\","
                     "\"job-id\":\"%s\","
                     "\"generic-id\":\"%s\""
                     "}}\n",
                     strts,
                     (uint32_t)((log_entry->microseconds / 1000.0f) + 0.5f),
                     mistral_scope_name[log_entry->scope],
                     mistral_contract_name[log_entry->contract_type],
                     log_entry->label,
                     mistral_measurement_name[log_entry->measurement],
                     log_entry->call_type_names,
                     path,
                     log_entry->threshold,
                     calculated_timeframe,
                     log_entry->size_min,
                     log_entry->size_max,
                     log_entry->hostname,
                     job_gid,
                     job_id,
                     generic_id) < 0)

        {
            mistral_err("Could not allocate memory for log entry\n");
            free(data);
            free(path);
            free(file);
            free(command);
            mistral_shutdown();
            return;
        }
        free(data);
        free(path);
        free(file);
        free(command);
        data = new_data;

        mistral_fluentbit_send(&fluentbit_tcp_ctx, new_data, strlen(new_data));

        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }
    log_list_tail = NULL;

    free(data);
}

/*
 * mistral_received_shutdown
 *
 * Function called whenever a shutdown message is received. Under normal
 * circumstances this message will be sent by Mistral outside of a data block
 * but in certain failure cases this may not be true.
 *
 * On receipt of a shutdown message check to see if there are any log entries
 * stored and, if so, call mistral_received_data_end to send them to
 * Fluent Bit.
 *
 * Parameters:
 *   void
 *
 * Returns:
 *   void
 */
void mistral_received_shutdown(void)
{
    if (log_list_head) {
        mistral_received_data_end(0, false);
    }
}
