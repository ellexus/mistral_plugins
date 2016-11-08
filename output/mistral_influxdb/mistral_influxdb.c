#include <curl/curl.h>          /* CURL *, CURLoption, etc */
#include <errno.h>              /* errno */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */

#include "mistral_plugin.h"

static FILE *log_file = NULL;
static CURL *easyhandle = NULL;
static char curl_error[CURL_ERROR_SIZE] = "";

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;

/*
 * set_curl_option
 *
 * Function used to set options on a CURL * handle and checking for success.
 * If an error occured log a message and shut down the plug-in.
 *
 * The CURL handle is stored in a global variable as it is shared between all
 * libcurl calls.
 *
 * Parameters:
 *   option    - The curl option to set
 *   parameter - A pointer to the appropriate value to set
 *
 * Returns:
 *   true on success
 *   false otherwise
 */
static bool set_curl_option(CURLoption option, void *parameter)
{

    if (curl_easy_setopt(easyhandle, option, parameter) != CURLE_OK) {
        mistral_err("Could not set curl URL option: %s", curl_error);
        mistral_shutdown = true;
        return false;
    }
    return true;
}

/*
 * influxdb_escape
 *
 * InfluxDB query strings treat commas, spaces and equals signs as delimiters,
 * if these characters occur within the data to be sent they must be escaped
 * with a single backslash character.
 *
 * This function allocates twice as much memory as is required to copy the
 * passed string and then copies the string character by character escaping
 * spaces, commas and equals signs as they are encountered. Once the copy is
 * complete the memory is reallocated to reduce wasteage.
 *
 * Parameters:
 *   string - The string whose content needs to be escaped
 *
 * Returns:
 *   A pointer to newly allocated memory containing the escaped string or
 *   NULL on error
 */
static char *influxdb_escape(const char *string)
{
    size_t len = strlen(string);

    char *escaped = calloc(1, (2 * len + 1) * sizeof(char));
    if (escaped) {
        for (char *p = (char *)string, *q = escaped; *p; p++, q++) {
            if (*p == ' ' || *p == ',' || *p == '=') {
                *q++ = '\\';
            }
            *q = *p;
        }
        char *small_escaped = realloc(escaped, (strlen(escaped) + 1) * sizeof(char));
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
 * In addition this plug-in needs to initialise the InfluxDB connection
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
        {"database", required_argument, NULL, 'd'},
        {"error", required_argument, NULL, 'e'},
        {"host", required_argument, NULL, 'h'},
        {"password", required_argument, NULL, 'p'},
        {"port", required_argument, NULL, 'P'},
        {"https", no_argument, NULL, 's'},
        {"username", required_argument, NULL, 'u'},
        {0, 0, 0, 0},
    };

    const char *database = "mistral";
    const char *error_file = NULL;
    const char *host = "localhost";
    const char *password = NULL;
    uint16_t port = 8086;
    const char *username = NULL;
    const char *protocol = "http";
    int opt;

    while ((opt = getopt_long(argc, argv, "d:e:h:p:P:su:", options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            database = optarg;
            break;
        case 'e':
            error_file = optarg;
            break;
        case 'h':
            host = optarg;
            break;
        case 'p':
            password = optarg;
            break;
        case 'P':{
            char *end = NULL;
            unsigned long tmp_port = strtoul(optarg, &end, 10);
            if (tmp_port <= 0 || tmp_port > UINT16_MAX || !end || *end) {
                mistral_err("Invalid port specified %s", optarg);
                return;
            }
            port = (uint16_t)tmp_port;
            break;
        }
        case 's':
            protocol = "https";
            break;
        case 'u':
            username = optarg;
            break;
        default:
            return;
        }
    }

    if (error_file != NULL) {
        log_file = fopen(error_file, "a");
        if (!log_file) {
            char buf[256];
            mistral_err("Could not open error file %s: %s", error_file,
                        strerror_r(errno, buf, sizeof buf));
        }
    }

    /* If we've opened an error log file use it in preference to stderr */
    if (log_file) {
        plugin->error_log = log_file;
    }

    if (curl_global_init(CURL_GLOBAL_ALL)) {
        mistral_err("Could not initialise curl");
        return;
    }

    /* Initialise curl */
    easyhandle = curl_easy_init();

    if (!easyhandle) {
        mistral_err("Could not initialise curl handle");
        return;
    }

    if (curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, curl_error) != CURLE_OK) {
        mistral_err("Could not set curl error buffer");
        return;
    }

    /* Set curl to treat HTTP errors as failures */
    if (curl_easy_setopt(easyhandle, CURLOPT_FAILONERROR, 1l) != CURLE_OK) {
        mistral_err("Could not set curl to fail on HTTP error");
        return;
    }

    /* Set InfluxDB connection options and set precision to seconds as this is
     * what we see in logs
     */
    char *url = NULL;
    if (asprintf(&url, "%s://%s:%d/write?db=%s&precision=s", protocol, host,
                 port, database) < 0) {
        mistral_err("Could not allocate memory for connection URL");
        return;
    }

    if (!set_curl_option(CURLOPT_URL, url)) {
        return;
    }

    /* Set up authentication */
    char *auth;
    if (asprintf(&auth, "%s:%s", (username)? username : "",
                                 (password)? password : "" ) < 0) {
        mistral_err("Could not allocate memory for authentication");
        return;
    }

    if (strcmp(auth, ":")) {
        if (curl_easy_setopt(easyhandle, CURLOPT_USERPWD, auth) != CURLE_OK) {
            mistral_err("Could not set up authentication");
            return;
        }
    }
    /* Returning after this point indicates success */
    plugin->type = OUTPUT_PLUGIN;

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

    if (log_file) {
        fclose(log_file);
    }

    if (easyhandle) {
        curl_easy_cleanup(easyhandle);
    }
    curl_global_cleanup();
}

/*
 * mistral_received_log
 *
 * Function called whenever a log message is received. Simply store the log
 * entry received in a linked list until we reach the end of this data block
 * as it is more efficient to send all the entries to InfluxDB in a single
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
 * mistral_received_data_end
 *
 * Function called whenever an end of data block message is received. At this
 * point run through the linked list of log entries we have seen and send them
 * to InfluxDB. Remove each log_entry from the linked list as they are
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

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        /* spaces, commas and equals signs in strings must be escaped in the
         * command and filenames
         */
        char *command = influxdb_escape(log_entry->command);
        char *file = influxdb_escape(log_entry->file);
        const char *job_gid = (log_entry->job_group_id[0] == 0)? "N/A" : log_entry->job_group_id;
        const char *job_id = (log_entry->job_id[0] == 0)? "N/A" : log_entry->job_id;
        char *old_data = data;

        if (asprintf(&data,
                     "%s%s%s,label=%s,calltype=%s,path=%s,threshold=%"
                     PRIu64 ",timeframe=%" PRIu64 ",size-min=%" PRIu64
                     ",size-max=%" PRIu64 ",file=%s,job-group=%s,"
                     "job-id=%s,pid=%" PRId64 ",command=%s value=%"
                     PRIu64 " %ld",
                     (data) ? data : "", (data) ? "\n" : "",
                     mistral_measurement_name[log_entry->measurement],
                     log_entry->label,
                     mistral_call_type_names[log_entry->call_type_mask],
                     log_entry->path,
                     log_entry->threshold,
                     log_entry->timeframe,
                     log_entry->size_min,
                     log_entry->size_max,
                     file,
                     job_gid,
                     job_id,
                     log_entry->pid,
                     command,
                     log_entry->measured,
                     log_entry->epoch.tv_sec) < 0) {

            mistral_err("Could not allocate memory for log entry");
            free(old_data);
            free(file);
            free(command);
            mistral_shutdown = true;
            return;
        }
        free(old_data);
        free(file);
        free(command);

        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }
    log_list_tail = NULL;

    if (data) {
        if (!set_curl_option(CURLOPT_POSTFIELDS, data)) {
            mistral_shutdown = true;
            return;
        }

        if (curl_easy_perform(easyhandle) != CURLE_OK) {
            mistral_err("Could not run curl query: %s", curl_error);
            mistral_shutdown = true;
        }
    }
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
 * stored and, if so, call mistral_received_data_end to send them to InfluxDB.
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
