#include <curl/curl.h>          /* CURL *, CURLoption, etc */
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

#include "mistral_plugin.h"

#define VALID_NAME_CHARS "1234567890abcdefghijklmnopqrstvuwxyzABCDEFGHIJKLMNOPQRSTVUWXYZ-_"

static FILE **log_file_ptr = NULL;
static CURL *easyhandle = NULL;
static char curl_error[CURL_ERROR_SIZE] = "";
static char *url = NULL;
static char *auth = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;
static const char *es_index = "mistral";
static char *custom_variables = NULL;
static unsigned long es_version = 5;

struct saved_resp {
    size_t size;
    char *body;
};

static struct curl_slist *headers = NULL;
/*
 * write_callback
 *
 * Function to be called by CURL to save the response from a request. All this
 * function does is save the response for later processing. If a memory
 * allocation error occurs, the entire response is discarded.
 *
 * Parameters:
 *   data   - A pointer to the returned data
 *   size   - Size of a data element
 *   nmemb  - Number of elements to "write"
 *   saved  - A pointer to the full response, this is our custom structure
 *
 * Returns:
 *   Number of bytes successfully processed
 */
static size_t write_callback(void *data, size_t size, size_t nmemb, void *saved)
{
    size_t data_len = size * nmemb;
    struct saved_resp *response = (struct saved_resp *)saved;

    char *new_body = realloc(response->body, response->size + data_len + 1);
    if (new_body == NULL) {
        free(response->body);
        response->body = NULL;
        response->size = 0;
        mistral_err("Could not allocate memory for response body\n");
        return 0;
    }

    response->body = new_body;
    memcpy(response->body + response->size, data, data_len);
    response->size += data_len;
    *(response->body + response->size) = '\0';

    return data_len;
}

/*
 * set_curl_option
 *
 * Function used to set options on a CURL * handle and checking for success.
 * If an error occurred log a message and shut down the plug-in.
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
        mistral_err("Could not set curl option: %s\n", curl_error);
        mistral_shutdown();
        return false;
    }
    return true;
}

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
                "  %s [-i index] [-h host] [-P port] [-e file] [-m octal-mode] [-u user] [-p password] [-s] [-v var-name ...]\n",
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
                "  --index=index_name\n"
                "  -i index_name\n"
                "     Set the index to be used for storing data. Defaults to \"mistral\".\n"
                "\n"
                "  --mode=octal-mode\n"
                "  -m octal-mode\n"
                "     Permissions used to create the error log file specified by the -e\n"
                "     option.\n"
                "\n"
                "  --password=secret\n"
                "  -p secret\n"
                "     The password required to access the Elasticsearch server if needed.\n"
                "\n"
                "  --port=number\n"
                "  -P number\n"
                "     Specifies the port to connect to on the Elasticsearch server host.\n"
                "     If not specified the plug-in will default to \"9200\".\n"
                "\n"
                "  --ssl\n"
                "  -s\n"
                "     Connect to the Elasticsearch server via secure HTTP.\n"
                "\n"
                "  --username=user\n"
                "  -u user\n"
                "     The username required to access the Elasticsearch server if needed.\n"
                "\n"
                "  --var=var-name\n"
                "  -v var-name\n"
                "     The name of an environment variable, the value of which should be\n"
                "     stored by the plug-in. This option can be specified multiple times.\n"
                "\n"
                "  --es-version=num\n"
                "  -V num\n"
                "     The major version of the Elasticsearch server to connect to.\n"
                "     If not specified the plug-in will default to \"5\".\n"
                "\n");
}

/*
 * elasticsearch_escape
 *
 * Elasticsearch uses JSON output which uses double quotes to delimit strings.
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
static char *elasticsearch_escape(const char *string)
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
 * In addition this plug-in needs to initialise the Elasticsearch connection
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
        {"index", required_argument, NULL, 'i'},
        {"error", required_argument, NULL, 'e'},
        {"host", required_argument, NULL, 'h'},
        {"mode", required_argument, NULL, 'm'},
        {"password", required_argument, NULL, 'p'},
        {"port", required_argument, NULL, 'P'},
        {"ssl", no_argument, NULL, 's'},
        {"username", required_argument, NULL, 'u'},
        {"var", required_argument, NULL, 'v'},
        {"es-version", required_argument, NULL, 'V'},
        {0, 0, 0, 0},
    };

    const char *error_file = NULL;
    const char *host = "localhost";
    const char *password = NULL;
    uint16_t port = 9200;
    const char *username = NULL;
    const char *protocol = "http";
    int opt;
    mode_t new_mode = 0;

    while ((opt = getopt_long(argc, argv, "e:h:i:m:p:P:su:v:V:", options, NULL)) != -1) {
        switch (opt) {
        case 'e':
            error_file = optarg;
            break;
        case 'h':
            host = optarg;
            break;
        case 'i':
            es_index = optarg;
            break;
        case 'm': {
            char *end = NULL;
            unsigned long tmp_mode = strtoul(optarg, &end, 8);
            if (!end || *end) {
                tmp_mode = 0;
            }
            new_mode = (mode_t)tmp_mode;

            if (new_mode <= 0 || new_mode > 0777) {
                mistral_err("Invalid mode '%s' specified, using default\n", optarg);
                new_mode = 0;
            }

            if ((new_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0) {
                mistral_err(
                    "Invalid mode '%s' specified, plug-in will not be able to write to log. Using default\n",
                    optarg);
                new_mode = 0;
            }
            break;
        }
        case 'p':
            password = optarg;
            break;
        case 'P': {
            char *end = NULL;
            unsigned long tmp_port = strtoul(optarg, &end, 10);
            if (tmp_port == 0 || tmp_port > UINT16_MAX || !end || *end) {
                mistral_err("Invalid port specified %s\n", optarg);
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
        case 'v': {
            char *var_val = NULL;
            char *new_var = NULL;

            if (optarg[0] != '\0' && strspn(optarg, VALID_NAME_CHARS) == strlen(optarg)) {
                var_val = elasticsearch_escape(getenv(optarg));
                if (var_val == NULL || var_val[0] == '\0') {
                    var_val = strdup("N/A");
                }
                if (var_val == NULL) {
                    mistral_err("Could not allocate memory for environment variable value %s\n",
                                optarg);
                    return;
                }
            } else {
                mistral_err("Invalid environment variable name %s\n", optarg);
            }
            if (asprintf(&new_var, "%s%s\"%s\":\"%s\"",
                         (custom_variables) ? custom_variables : "",
                         (custom_variables) ? "," : "",
                         optarg,
                         var_val) < 0)
            {
                mistral_err("Could not allocate memory for environment variable %s\n", optarg);
                free(var_val);
                return;
            }
            free(var_val);
            free(custom_variables);
            custom_variables = new_var;
            break;
        }
        case 'V': {
            char *end = NULL;
            es_version = strtoul(optarg, &end, 10);
            if (es_version < 2 || es_version > 6 || !end || *end) {
                mistral_err("Unsupported Elasticsearch version \"%s\" specified\n", optarg);
                return;
            }
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

    if (curl_global_init(CURL_GLOBAL_ALL)) {
        mistral_err("Could not initialise curl\n");
        return;
    }

    /* Initialise curl */
    easyhandle = curl_easy_init();

    if (!easyhandle) {
        mistral_err("Could not initialise curl handle\n");
        return;
    }

    if (curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, curl_error) != CURLE_OK) {
        mistral_err("Could not set curl error buffer\n");
        return;
    }

    /* Set curl to treat HTTP errors as failures */
    if (curl_easy_setopt(easyhandle, CURLOPT_FAILONERROR, 1l) != CURLE_OK) {
        mistral_err("Could not set curl to fail on HTTP error\n");
        return;
    }

    /* Use a custom write function to save any response from Elasticsearch */
    if (!set_curl_option(CURLOPT_WRITEFUNCTION, write_callback)) {
        mistral_shutdown();
        return;
    }

    /* All our queries are going to be using JSON so save a custom header */
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!set_curl_option(CURLOPT_HTTPHEADER, headers)) {
        mistral_shutdown();
        return;
    }

    /* Set Elasticsearch connection options
     *
     * Some versions of libcurl appear to use pointers to the original data for
     * option values, therefore we use global variables for both the URL and
     * authentication strings.
     */
    if (asprintf(&url, "%s://%s:%d/_bulk", protocol, host, port) < 0) {
        mistral_err("Could not allocate memory for connection URL\n");
        return;
    }

    if (!set_curl_option(CURLOPT_URL, url)) {
        free(url);
        return;
    }

    /* Set up authentication */
    if (asprintf(&auth, "%s:%s", username ? username : "",
                 password ? password : "") < 0)
    {
        mistral_err("Could not allocate memory for authentication\n");
        return;
    }

    if (strcmp(auth, ":")) {
        if (curl_easy_setopt(easyhandle, CURLOPT_USERPWD, auth) != CURLE_OK) {
            mistral_err("Could not set up authentication\n");
            free(auth);
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

    if (easyhandle) {
        curl_easy_cleanup(easyhandle);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }

    curl_global_cleanup();

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
 * as it is more efficient to send all the entries to Elasticsearch in a single
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
 * to Elasticsearch. Remove each log_entry from the linked list as they are
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

    /* Elasticsearch 5.3.0 appears to treat dates inserted into date fields
     * as seconds since epoch as long ints rather than dates even with a
     * date mapping defined. Therefore we must use a string representation
     * for the date. Again while Elasticsearch should cope with time zones
     * in practice it seems that every timestamp must be in the same time
     * zone for searches to work as expected. Logstash uses zulu time format
     * (UTC) into a field named "@timestamp" therefore we will do the same
     * as this should maximise compatibility.
     */
    size_t date_len = sizeof("YYYY-MM-DD"); /* strftime format = %F */
    size_t ts_len = sizeof("YYYY-MM-DDTHH:MI:SS"); /* = %FT%T */
    size_t type_len = sizeof(",\"_type\":\"throttle\""); /* Max possible len */
    char strdate[date_len];
    char strts[ts_len];
    char doc_type[type_len];
    struct tm utc_time;

    if (es_version == 6) {
        strcpy(doc_type, ",\"_type\":\"_doc\"");
    }

    while (log_entry) {
        /* Calculate UTC time */
        if (gmtime_r(&log_entry->epoch.tv_sec, &utc_time) == NULL) {
            mistral_err("Unable to calculate UTC time for log message: %ld\n",
                        log_entry->epoch.tv_sec);
            free(data);
            mistral_shutdown();
            return;
        }

        strftime(strdate, date_len, "%F", &utc_time);
        strftime(strts, ts_len, "%FT%T", &utc_time);

        /* Command and filename must be JSON escaped */
        char *command = elasticsearch_escape(log_entry->command);
        char *file = elasticsearch_escape(log_entry->file);
        const char *job_gid = (log_entry->job_group_id[0] == 0) ? "N/A" : log_entry->job_group_id;
        const char *job_id = (log_entry->job_id[0] == 0) ? "N/A" : log_entry->job_id;
        char *new_data = NULL;

        if (es_version < 6) {
            sprintf(doc_type, ",\"_type\":\"%s\"",
                    mistral_contract_name[log_entry->contract_type]);
        }

        if (asprintf(&new_data,
                     "%s"
                     "{\"index\":{\"_index\":\"%s-%s\"%s}}\n"
                     "{\"@timestamp\": \"%s.%03" PRIu32 "Z\","
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
                     "\"job-id\":\"%s\""
                     "},"
                     "\"process\":{"
                     "\"pid\":%" PRId64 ","
                     "\"command\":\"%s\","
                     "\"file\":\"%s\","
                     "\"cpu-id\":%" PRIu32 ","
                     "\"mpi-world-rank\":%" PRId32
                     "},"
                     "%s"
                     "%s"
                     "%s"
                     "\"value\":%" PRIu64
                     "}\n",
                     (data) ? data : "",
                     es_index, strdate, doc_type,
                     strts,
                     (uint32_t)((log_entry->microseconds / 1000.0f) + 0.5f),
                     mistral_scope_name[log_entry->scope],
                     mistral_contract_name[log_entry->contract_type],
                     log_entry->label,
                     mistral_measurement_name[log_entry->measurement],
                     log_entry->call_type_names,
                     log_entry->path,
                     log_entry->threshold,
                     log_entry->timeframe,
                     log_entry->size_min,
                     log_entry->size_max,
                     log_entry->hostname,
                     job_gid,
                     job_id,
                     log_entry->pid,
                     command,
                     file,
                     log_entry->cpu,
                     log_entry->mpi_rank,
                     (custom_variables) ? "\"environment\":{" : "",
                     (custom_variables) ? custom_variables : "",
                     (custom_variables) ? "}," : "",
                     log_entry->measured) < 0)
        {
            mistral_err("Could not allocate memory for log entry\n");
            free(data);
            free(file);
            free(command);
            mistral_shutdown();
            return;
        }
        free(data);
        free(file);
        free(command);
        data = new_data;

        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }
    log_list_tail = NULL;

    if (data) {
        if (!set_curl_option(CURLOPT_POSTFIELDS, data)) {
            mistral_shutdown();
            return;
        }

        struct saved_resp full_response = {0, NULL};
        if (!set_curl_option(CURLOPT_WRITEDATA, &full_response)) {
            mistral_shutdown();
            return;
        }

        CURLcode ret = curl_easy_perform(easyhandle);
        if (ret != CURLE_OK) {
            /* Depending on the version of curl used during compilation
             * curl_error may not be populated. If this is the case, look up
             * the less detailed error based on return code instead.
             */
            mistral_err("Could not run curl query: %s\n",
                        (curl_error[0] != '\0') ? curl_error : curl_easy_strerror(ret));
            mistral_shutdown();
        }

        if (full_response.body) {
            char *success = strstr(full_response.body, "\"errors\":false");
            if (!success) {
                mistral_shutdown();
                mistral_err("Could not index data\n");
                mistral_err("Data sent:\n%s\n", data);
                mistral_err("Response received:\n%s\n", full_response.body);
            }
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
 * stored and, if so, call mistral_received_data_end to send them to
 * Elasticsearch.
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
