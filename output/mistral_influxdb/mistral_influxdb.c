#include <curl/curl.h>          /* CURL *, CURLoption, etc */
#include <errno.h>              /* errno */
#include <fcntl.h>              /* open */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free, getenv */
#include <string.h>             /* strerror_r */
#include <sys/stat.h>           /* open, umask */
#include <sys/types.h>          /* open, umask */

#include "mistral_plugin.h"

enum debug_states {
    DBG_LOW = 0,
    DBG_MED,
    DBG_HIGH,
    DBG_ENTRY,
    DBG_LIMIT
};

/* Define debug output function as a macro so we can use mistral_err */
#define DEBUG_OUTPUT(level, format, ...)                                                      \
do {                                                                                          \
    if ((1 << level) & debug_level) {                                                         \
        mistral_err("DEBUG[%d] %s:%d " format, level + 1, __func__, __LINE__, ##__VA_ARGS__); \
    }                                                                                         \
} while (0)

#define VALID_NAME_CHARS "1234567890abcdefghijklmnopqrstvuwxyzABCDEFGHIJKLMNOPQRSTVUWXYZ-_"

static unsigned long debug_level = 0;

static FILE **log_file_ptr = NULL;
static CURL *easyhandle = NULL;
static char curl_error[CURL_ERROR_SIZE] = "";
static char *url = NULL;
static char *auth = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;
static char *custom_variables = NULL;

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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %ld, %p\n", (long)option, parameter);

    if (curl_easy_setopt(easyhandle, option, parameter) != CURLE_OK) {
        mistral_err("Could not set curl URL option: %s\n", curl_error);
        mistral_shutdown();
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed\n");
        return false;
    }

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success\n");
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
     * to stderr.
     */
    mistral_err("Usage:\n");
    mistral_err(
        "  %s [-d database] [-h host] [-P port] [-e file] [-m octal-mode] [-u user] [-p password] [-s] [-v var-name ...]\n"
             "[-k] [-c certificate_path] [--cert-dir=certificate_directory]\n", name);
    mistral_err("\n"
                "  --cert-path=certificate_path\n"
                "  -c certificate_path\n"
                "     The full path to a CA certificate used to sign the certificate\n"
                "     of the InfluxDB server. See ``man openssl verify`` for details\n"
                "     of the ``CAfile`` option.\n"
                "\n"
                "  --cert-dir=certificate_directory \n"
                "     The directory that contains the CA certificate(s) used to sign the\n"
                "     certificate of the InfluxDB server. Certificates in this directory\n"
                "     should be named after the hashed certificate subject name, see\n"
                "     ``man openssl verify`` for details of the ``CApath`` option.\n"
                "\n"
                "  --database=db-name\n"
                "  -d db-name\n"
                "     Set the InfluxDB database to be used for storing data.\n"
                "     Defaults to \"mistral\".\n"
                "\n"
                "  --error=file\n"
                "  -e file\n"
                "     Specify location for error log. If not specified all errors will\n"
                "     be output on stderr and handled by Mistral error logging.\n"
                "\n"
                "  --host=hostname\n"
                "  -h hostname\n"
                "     The hostname of the InfluxDB server with which to establish a connection.\n"
                "     If not specified the plug-in will default to \"localhost\".\n"
                "\n"
                "  --mode=octal-mode\n"
                "  -m octal-mode\n"
                "     Permissions used to create the error log file specified by the -e\n"
                "     option.\n"
                "\n"
                "  --password=secret\n"
                "  -p secret\n"
                "     The password required to access the InfluxDB server if needed.\n"
                "\n"
                "  --port=number\n"
                "  -P number\n"
                "     Specifies the port to connect to on the InfluxDB server host.\n"
                "     If not specified the plug-in will default to \"8086\".\n"
                "\n"
                "  --ssl\n"
                "  -s\n"
                "     Connect to the InfluxDB server via secure HTTP.\n"
                "\n"
                "  --skip-ssl-validation\n"
                "  -k\n"
                "     Disable SSL certificate validation when connecting to InfluxDB.\n"
                "\n"
                "  --username=user\n"
                "  -u user\n"
                "     The username required to access the InfluxDB server if needed.\n"
                "\n"
                "  --var=var-name\n"
                "  -v var-name\n"
                "     The name of an environment variable, the value of which should be\n"
                "     stored by the plug-in. This option can be specified multiple times.\n"
                "\n");
}

/* influxdb_escape
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %s\n", string);
    if (!string) {
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, nothing to do\n");
        return NULL;
    }
    size_t len = strlen(string);

    char *escaped = calloc(1, (2 * len + 1) * sizeof(char));
    char *p, *q;
    if (escaped) {
        for (p = (char *)string, q = escaped; *p; p++, q++) {
            if (*p == ' ' || *p == ',' || *p == '=') {
                *q++ = '\\';
            }
            *q = *p;
        }
        /* Memory was allocated with calloc so string is already null terminated */
        char *small_escaped = realloc(escaped, q - escaped + 1);
        if (small_escaped) {
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success\n");
            return small_escaped;
        } else {
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, partial success\n");
            return escaped;
        }
    } else {
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed\n");
        return NULL;
    }
}

/* influxdb_escape_field
 *
 * InfluxDB field strings are double quoted. We need to quote any double quotes
 * that are part of the string value.
 *
 * This function allocates twice as much memory as is required to copy the
 * passed string and then copies the string character by character escaping
 * double quotes as they are encountered. Once the copy is complete the memory
 * is reallocated to reduce wasteage.
 *
 * Parameters:
 *   string - The field string whose content needs to be escaped
 *
 * Returns:
 *   A pointer to newly allocated memory containing the escaped string or
 *   NULL on error
 */
static char *influxdb_escape_field(const char *string)
{
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %s\n", string);
    if (!string) {
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, nothing to do\n");
        return NULL;
    }
    size_t len = strlen(string);

    char *escaped = calloc(1, (2 * len + 1) * sizeof(char));
    if (escaped) {
        for (char *p = (char *)string, *q = escaped; *p; p++, q++) {
            if (*p == '"') {
                *q++ = '\\';
            }
            *q = *p;
        }
        char *small_escaped = realloc(escaped, (strlen(escaped) + 1) * sizeof(char));
        if (small_escaped) {
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success\n");
            return small_escaped;
        } else {
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, partial success\n");
            return escaped;
        }
    } else {
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed\n");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function %d, %p, %p\n", argc, argv, plugin);
    /* Returning without setting plug-in type will cause a clean exit */

    #define CERT_DIR_OPTION_CODE 1001

    static const struct option options[] = {
        {"database", required_argument, NULL, 'd'},
        {"debug", required_argument, NULL, 'D'},
        {"error", required_argument, NULL, 'e'},
        {"host", required_argument, NULL, 'h'},
        {"mode", required_argument, NULL, 'm'},
        {"password", required_argument, NULL, 'p'},
        {"port", required_argument, NULL, 'P'},
        {"https", no_argument, NULL, 's'},
        {"skip-ssl-validation", no_argument, NULL, 'k'},
        {"username", required_argument, NULL, 'u'},
        {"var", required_argument, NULL, 'v'},
        {"cert-dir", required_argument, NULL, CERT_DIR_OPTION_CODE},
        {"cert-path", required_argument, NULL, 'c'},        
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
    bool skip_validation = false;
    mode_t new_mode = 0;
    const char *cert_path = NULL;
    const char *cert_dir = NULL;

    while ((opt = getopt_long(argc, argv, "d:D:e:h:m:p:P:sku:v:c:", options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            database = optarg;
            break;
        case 'D': {
            char *end = NULL;
            unsigned long tmp_level = strtoul(optarg, &end, 10);
            if (tmp_level == 0 || !end || *end || tmp_level > DBG_LIMIT) {
                mistral_err("Invalid debug level '%s', using '1'\n", optarg);
                tmp_level = 1;
            }
            /* For now just allow cumulative debug levels rather than selecting messages */
            debug_level = (1 << tmp_level) - 1;
            break;
        }
        case 'e':
            error_file = optarg;
            break;
        case 'h':
            host = optarg;
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
            if (tmp_port <= 0 || tmp_port > UINT16_MAX || !end || *end) {
                mistral_err("Invalid port specified %s\n", optarg);
                DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
                return;
            }
            port = (uint16_t)tmp_port;
            break;
        }
        case 's':
            protocol = "https";
            break;
        case 'k':
            skip_validation = true;
            break;
        case 'u':
            username = optarg;
            break;
        case 'v': {
            char *var_val = NULL;
            char *new_var = NULL;

            if (optarg[0] != '\0' && strspn(optarg, VALID_NAME_CHARS) == strlen(optarg)) {
                var_val = influxdb_escape(getenv(optarg));

                if (var_val == NULL || var_val[0] == '\0') {
                    var_val = strdup("N/A");
                }
                if (var_val == NULL) {
                    mistral_err("Could not allocate memory for environment variable value %s\n",
                                optarg);
                    DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
                    return;
                }
            } else {
                mistral_err("Invalid environment variable name %s\n", optarg);
            }
            if (asprintf(&new_var, "%s,%s=%s",
                         (custom_variables) ? custom_variables : "",
                         optarg,
                         var_val) < 0)
            {
                mistral_err("Could not allocate memory for environment variable %s\n", optarg);
                DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
                free(var_val);
                return;
            }
            free(var_val);
            free(custom_variables);
            custom_variables = new_var;
            break;
        }
        case 'c':
            cert_path = optarg;
            break;
        case CERT_DIR_OPTION_CODE:
            cert_dir = optarg;
            break;
        default:
            usage(argv[0]);
            DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
            return;
        }
    }

    log_file_ptr = &(plugin->error_log);

    /* Error file is opened/created by the first error_msg
     */
    plugin->error_log = stderr;
    plugin->error_log_name = (char *)error_file;
    plugin->error_log_mode = new_mode;
    plugin->flags = 0;

    if (curl_global_init(CURL_GLOBAL_ALL)) {
        mistral_err("Could not initialise curl\n");
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        return;
    }

    /* Initialise curl */
    easyhandle = curl_easy_init();

    if (!easyhandle) {
        mistral_err("Could not initialise curl handle\n");
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        return;
    }

    if (curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, curl_error) != CURLE_OK) {
        mistral_err("Could not set curl error buffer\n");
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        return;
    }

    /* Set curl to treat HTTP errors as failures */
    if (curl_easy_setopt(easyhandle, CURLOPT_FAILONERROR, 1l) != CURLE_OK) {
        mistral_err("Could not set curl to fail on HTTP error\n");
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        return;
    }

    /* If using a self-signed certificate (for example) disable SSL validation */
    if (skip_validation) {
        if (curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYPEER, 0) != CURLE_OK) {
            mistral_err("Could not disable curl peer validation\n");
            return;
        }
    }

    if (cert_path) {
        if (curl_easy_setopt(easyhandle, CURLOPT_CAINFO, cert_path) != CURLE_OK) {
            mistral_err("Could not set curl certificate path (CAINFO) '%s'\n", cert_path);
            return;
        }
    }

    if (cert_dir) {
        if (curl_easy_setopt(easyhandle, CURLOPT_CAPATH, cert_dir) != CURLE_OK) {
            mistral_err("Could not set curl certificate directory (CAPATH) '%s'\n", cert_dir);
            return;
        }
    }    

    /* Set InfluxDB connection options and set precision to microseconds as this
     * is what we see in logs
     *
     * Some versions of libcurl appear to use pointers to the original data for
     * option values, therefore we use global variables for both the URL and
     * authentication strings.
     */
    if (asprintf(&url, "%s://%s:%d/write?db=%s&precision=u", protocol, host,
                 port, database) < 0)
    {
        mistral_err("Could not allocate memory for connection URL\n");
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        return;
    }
    DEBUG_OUTPUT(DBG_MED, "InfluxDB connection URL: %s\n", url);

    if (!set_curl_option(CURLOPT_URL, url)) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        free(url);
        return;
    }

    /* Set up authentication */
    if (asprintf(&auth, "%s:%s", (username) ? username : "",
                 (password) ? password : "") < 0)
    {
        mistral_err("Could not allocate memory for authentication\n");
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
        return;
    }

    if (strcmp(auth, ":")) {
        if (curl_easy_setopt(easyhandle, CURLOPT_USERPWD, auth) != CURLE_OK) {
            mistral_err("Could not set up authentication\n");
            DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed\n");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function\n");
    if (log_list_head) {
        DEBUG_OUTPUT(DBG_LOW, "Log entries existed at exit\n");
        mistral_received_data_end(0, false);
    }

    if (easyhandle) {
        curl_easy_cleanup(easyhandle);
    }

    curl_global_cleanup();

    free(custom_variables);
    free(auth);
    free(url);

    if (log_file_ptr && *log_file_ptr != stderr) {
        DEBUG_OUTPUT(DBG_ENTRY, "Closing log file\n");
        fclose(*log_file_ptr);
        *log_file_ptr = stderr;
    }
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %p\n", log_entry);
    if (!log_list_head) {
        /* Initialise linked list */
        log_list_head = log_entry;
        log_list_tail = log_entry;
        insque(log_entry, NULL);
    } else {
        insque(log_entry, log_list_tail);
        log_list_tail = log_entry;
    }
    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success\n");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entered function, %" PRIu64 ", %d\n", block_num, block_error);
    UNUSED(block_num);
    UNUSED(block_error);

    char *data = NULL;

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        /* Double quotes must must be escaped in the field strings. */
        char *command = influxdb_escape_field(log_entry->command);
        char *file = influxdb_escape_field(log_entry->file);
        char *path = influxdb_escape_field(log_entry->path);
        char *fstype = influxdb_escape_field(log_entry->fstype);
        char *fsname = influxdb_escape_field(log_entry->fsname);
        char *fshost = influxdb_escape_field(log_entry->fshost);
        const char *job_gid = (log_entry->job_group_id[0] == 0) ? "N/A" : log_entry->job_group_id;
        const char *job_id = (log_entry->job_id[0] == 0) ? "N/A" : log_entry->job_id;
        char *new_data = NULL;

        /* InfluxDB tags are always strings. They are indexed and stored in memory. Our current
         * tags are measurement type, call type, job group ID, job ID, label and hostname. None
         * of these tags should contain commas, spaces or equals signs, so we don't need to worry
         * about escaping these characters.
         *
         * InfluxDB fields are not indexed and hence should not be used as query filters. They
         * use different data types, like strings and integers. Strings are double-quoted, so we
         * have to escape double quotes in command and file path. Integers require 'i' suffix,
         * otherwise InfluxDB interprets the value as a float. For example, if you omit 'i' with
         * size-max value 9223372036854775807, InfluxDB stores it as 9.223372036854776e+18 and
         * returns 9223372036854776000.
         */
        if (asprintf(&new_data,
                     "%s%s%s,calltype=%s,jobgroup=%s,jobid=%s,label=%s,path=\"%s\","
                     "fstype=\"%s\",fsname=\"%s\",fshost=\"%s\",host=%s%s"
                     " command=\"%s\",cpu=%" PRIu32 "i,file=\"%s\",logtype=\"%s\""
                     ",mpirank=%" PRId32 "i,pid=%" PRId64 "i,scope=\"%s\""
                     ",sizemin=%" PRIu64 "i,sizemax=%" PRIu64 "i,threshold=%" PRIu64
                     "i,timeframe=%" PRIu64 "i,value=%"
                     PRIu64 " %ld%06" PRIu32,
                     (data) ? data : "", (data) ? "\n" : "",
                     mistral_measurement_name[log_entry->measurement],
                     log_entry->call_type_names,
                     job_gid,
                     job_id,
                     log_entry->label,
                     path,
                     fstype,
                     fsname,
                     fshost,
                     log_entry->hostname,
                     (custom_variables) ? custom_variables : "",
                     command,
                     log_entry->cpu,
                     file,
                     mistral_contract_name[log_entry->contract_type],
                     log_entry->mpi_rank,
                     log_entry->pid,
                     mistral_scope_name[log_entry->scope],
                     log_entry->size_min,
                     log_entry->size_max,
                     log_entry->threshold,
                     log_entry->timeframe,
                     log_entry->measured,
                     log_entry->epoch.tv_sec,
                     log_entry->microseconds) < 0)
        {
            mistral_err("Could not allocate memory for log entry\n");
            free(data);
            free(fshost);
            free(fsname);
            free(fstype);
            free(file);
            free(command);
            mistral_shutdown();
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed\n");
            return;
        }
        free(data);
        free(fshost);
        free(fsname);
        free(fstype);
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
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed\n");
            return;
        }

        CURLcode ret = curl_easy_perform(easyhandle);
        if (ret != CURLE_OK) {
            /* Depending on the version of curl used during compilation
             * curl_error may not be populated. If this is the case, look up
             * the less detailed error based on return code instead.
             */
            mistral_err("Could not run curl query: %s\n",
                        (*curl_error != '\0') ? curl_error : curl_easy_strerror(ret));
            mistral_shutdown();
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed\n");
        }
    }
    free(data);
    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success\n");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function\n");
    if (log_list_head) {
        DEBUG_OUTPUT(DBG_LOW, "Log entries existed when shutdown seen\n");
        mistral_received_data_end(0, false);
    }
}
