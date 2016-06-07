#include <curl/curl.h>
#include <errno.h>              /* errno */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */
#include <search.h>             /* insque, remque */

#include "mistral_plugin.h"

FILE *log_file = NULL;
CURL *easyhandle = NULL;
char curl_error[CURL_ERROR_SIZE] = "";

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;

static bool set_curl_option(CURLoption option, void *parameter)
{

    if (curl_easy_setopt(easyhandle, option, parameter) != CURLE_OK) {
        mistral_err("Could not set curl URL option: %s", curl_error);
        mistral_shutdown = true;
        return false;
    }
    return true;
}

static char *influxdb_escape(const char *string)
{
    size_t len = strlen(string);

    char *escaped = calloc(1, (len + 1) * sizeof(char));
    if (escaped) {
        for (char *p = (char *)string, *q = escaped; *p; p++, q++) {
            if (*p == ' ' || *p == ',') {
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

    if (log_file) {
        plugin->error_log = log_file;
    }

    if (curl_global_init(CURL_GLOBAL_ALL)) {
        mistral_err("Could not initialise curl");
        return;
    }

    easyhandle = curl_easy_init();

    if (!easyhandle) {
        mistral_err("Could not initialise curl handle");
        return;
    }

    if (curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, curl_error) != CURLE_OK) {
        mistral_err("Could not set curl error buffer");
        return;
    }

    char *url = NULL;
    /* Set InfluxDB connetion options and set precision to seconds as this is what we see in logs */
    if (asprintf(&url, "%s://%s:%d/write?db=%s&precision=s", protocol, host, port, database) < 0) {
        mistral_err("Could not allocate memory for connection URL");
        return;
    }

    if (!set_curl_option(CURLOPT_URL, url)) {
        return;
    }

    char *auth = "";
    /* Set up authentication */
    if (username && asprintf(&auth, "%s", username) < 0) {
        mistral_err("Could not allocate memory for username");
        return;
    }
    /* Add the password to the authentication string if one was specified */
    if (password && asprintf(&auth, "%s:%s", auth, password) < 0) {
        mistral_err("Could not allocate memory for password");
        return;
    }
    if (strlen(auth)) {
        if (curl_easy_setopt(easyhandle, CURLOPT_USERPWD, auth) != CURLE_OK) {
            mistral_err("Could not set up authentication");
            return;
        }
    }
    /* Returning after this point indicates success */
    plugin->type = OUTPUT_PLUGIN;

}

void mistral_exit()
{
    if (log_file) {
        fclose(log_file);
    }

    if (easyhandle) {
        curl_easy_cleanup(easyhandle);
    }
    curl_global_cleanup();
}

void mistral_received_log(mistral_log *log_entry)
{
    /* Store log entries in a linked list until we see the end of the data
     * block so we can send all the logs in one request for better performance.
     */
    if (!log_list_head) {
        /* Initialise linked list */
        log_list_head = log_entry;
        log_list_tail = log_entry;
        insque(log_entry, NULL);
    } else {
        insque(log_entry, log_list_tail);
    }

}

void mistral_received_data_end(uint64_t block_num)
{
    UNUSED(block_num);

    char *data = NULL;

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        /* spaces and commas in strings must be escaped in the command and filenames */
        char *command = influxdb_escape(log_entry->command);
        char *file = influxdb_escape(log_entry->file);

        /* We need to write one record for every call type in the rule */
        for (size_t i = 0; i < MAX_CALL_TYPE; i++) {
            if (log_entry->call_types[i]) {
                if (asprintf(&data,
                             "%s%s%s,label=%s,calltype=%s,path=%s,threshold=%"
                             PRIu64 ",timeframe=%" PRIu64 ",size-min=%" PRIu64
                             ",size-max=%" PRIu64 ",file=%s,job-group=%s,"
                             "job-id=%s,pid=%" PRIu64 ",command=%s value=%"
                             PRIu64 " %jd",
                             (data) ? data : "", (data) ? "\n" : "",
                             mistral_measurement_name[log_entry->measurement],
                             log_entry->label,
                             mistral_call_type_name[i],
                             log_entry->path,
                             log_entry->threshold,
                             log_entry->timeframe,
                             log_entry->size_min,
                             log_entry->size_max,
                             file,
                             log_entry->job_group_id,
                             log_entry->job_id,
                             log_entry->pid,
                             command,
                             log_entry->measured,
                             log_entry->epoch.tv_sec) < 0) {
                    mistral_err("Could not allocate memory for log entry");
                    mistral_shutdown = true;
                    return;
                }
            }
        }

        free(file);
        free(command);

        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }

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
