#include <ctype.h>              /* isalnum */
#include <errno.h>              /* errno */
#include <fcntl.h>              /* open */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <netdb.h>              /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */
#include <sys/socket.h>         /* getaddrinfo, freeaddrinfo, gai_strerror, send */
#include <sys/stat.h>           /* open, umask */
#include <sys/types.h>          /* open, umask, getaddrinfo, freeaddrinfo, gai_strerror, send */
#include <unistd.h>             /* close */

#include "mistral_plugin.h"

static FILE **log_file_ptr = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;
static int graphite_fd = -1;
static char *schema = NULL;

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
    /* This may be called before options have been processed so errors will go to stderr.
     * While this is designed to be run with Mistral to make the messages understandable
     * on a terminal add an explicit newline to each line.
     */
    mistral_err("Usage:\n"
                "  %s [-i metric] [-h host] [-p port] [-e file] [-m octal-mode] [-4|-6]\n", name);
    mistral_err("\n"
                "  -4\n"
                "     Use IPv4 only. This is the default behaviour.\n"
                "\n"
                "  -6\n"
                "     Use IPv6 only.\n"
                "\n"
                "  --error=file\n"
                "  -e file\n"
                "     Specify location for error log. If not specified all errors will\n"
                "     be output on stderr and handled by Mistral error logging.\n"
                "\n"
                "  --host=hostname\n"
                "  -h hostname\n"
                "     The hostname of the Graphite server with which to establish a connection.\n"
                "     If not specified the plug-in will default to \"localhost\".\n"
                "\n"
                "  --instance=metric\n"
                "  -i metric\n"
                "     Set the root metric node name the plug-in should create data under. This\n"
                "     value can contain '.' characters to allow more precise classification\n"
                "     of metrics.  Defaults to \"mistral\".\n"
                "\n"
                "  --mode=octal-mode\n"
                "  -m octal-mode\n"
                "     Permissions used to create the error log file specified by the -o\n"
                "     option.\n"
                "\n"
                "  --port=port\n"
                "  -p port\n"
                "     Specifies the port to connect to on the Graphite server host.\n"
                "     If not specified the plug-in will default to \"2003\".\n"
                "\n");
}

/*
 * graphite_escape
 *
 * Graphite metrics will interpret both '.' and '/' characters as separators so
 * if these characters occur within the data e.g. in the path or job ID they
 * must be replaced. To be paranoid this function will replace '/' with ':' and
 * any other non-alphanumeric, hyphen or underscore characters with hyphens.
 *
 * This function duplicates the passed string and then replaces each character
 * encountered.
 *
 * Parameters:
 *   string - The string whose content needs to be escaped
 *
 * Returns:
 *   A pointer to newly allocated memory containing the escaped string or
 *   NULL on error
 */
static char *graphite_escape(const char *string)
{
    char *escaped = strdup(string);
    if (escaped) {
        for (char *p = escaped; *p; p++) {
            if (*p == '/') {
                *p = ':';
            } else if (!isalnum((unsigned char)*p) && *p != '_') {
                *p = '-';
            }
        }
        return escaped;
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
 * In addition this plug-in needs to initialise the Graphite connection
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
        {"4", no_argument, NULL, '4'},
        {"6", no_argument, NULL, '6'},
        {"error", required_argument, NULL, 'e'},
        {"host", required_argument, NULL, 'h'},
        {"instance", required_argument, NULL, 'i'},
        {"mode", required_argument, NULL, 'm'},
        {"port", required_argument, NULL, 'p'},
        {0, 0, 0, 0},
    };

    const char *error_file = NULL;
    const char *host = "localhost";
    const char *port = "2003";
    int family = AF_UNSPEC;
    int gai_retval;
    int opt;
    mode_t new_mode = 0;
    struct addrinfo *addrs;
    struct addrinfo hints;

    while ((opt = getopt_long(argc, argv, "e:h:i:m:p:", options, NULL)) != -1) {
        switch (opt) {
        case '4':
            family = AF_INET;
            break;
        case '6':
            family = AF_INET6;
            break;
        case 'e':
            error_file = optarg;
            break;
        case 'h':
            host = optarg;
            break;
        case 'i':
            schema = optarg;
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
        case 'p': {
            char *end = NULL;
            unsigned long tmp_port = strtoul(optarg, &end, 10);
            if (tmp_port > UINT16_MAX || !end || *end) {
                mistral_err("Invalid port specified %s\n", optarg);
                return;
            }
            port = optarg;
            break;
        }
        default:
            usage(argv[0]);
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

    if (!schema) {
        schema = "mistral";
        if (!schema) {
            char buf[256];
            mistral_err("Could not set schema instance \"mistral\": %s\n",
                        strerror_r(errno, buf, sizeof buf));
        }
    }
    /* Get addrinfo using the provided parameters */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    if ((gai_retval = getaddrinfo(host, port, &hints, &addrs)) != 0) {
        mistral_err("Failed to get host info: %s\n", gai_strerror(gai_retval));
        return;
    }

    /* getaddrinfo returns an array of matching possible addresses, try to
     * connect to each until we succeed or run out of addresses.
     */

    for (struct addrinfo *curr = addrs; curr != NULL; curr = curr->ai_next) {
        char buf[256];
        if ((graphite_fd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1) {
            mistral_err("Unable to create socket: %s%s\n",
                        strerror_r(errno, buf, sizeof buf),
                        curr->ai_next ? " - Trying next address" : "");
            continue;
        }

        if (connect(graphite_fd, curr->ai_addr, curr->ai_addrlen) == -1) {
            /* Do not log all failed attempts as this can be quite a common occurrence */
            close(graphite_fd);
            continue;
        }
        break;
    }

    if (graphite_fd < 0) {
        mistral_err("Unable to connect to: %s:%s\n", host, port);
    }

    /* We no longer need the array of addresses */
    freeaddrinfo(addrs);

    /* Returning after this point indicates success */
    plugin->type = OUTPUT_PLUGIN;
}

/*
 * mistral_exit
 *
 * Function called immediately before the plug-in exits. Check for any unhandled
 * log entries and call mistral_received_data_end to process them if any are
 * found. Clean up any open error log and the socket connection.
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

    if (graphite_fd >= 0) {
        close(graphite_fd);
    }

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
 * as it is more efficient to send all the entries to Graphite in a single
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
 * to Graphite. Remove each log_entry from the linked list as they are
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

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        char *data = NULL;

        char *job_gid = graphite_escape(log_entry->job_group_id);
        char *job_id = graphite_escape(log_entry->job_id);
        char *path = graphite_escape(log_entry->path);
        char *fstype = graphite_escape(log_entry->fstype);
        char *fsname = graphite_escape(log_entry->fsname);
        char *fshost = graphite_escape(log_entry->fshost);
        if (job_gid[0] == 0) {
            free(job_gid);
            job_gid = strdup("None");
        }
        if (job_id[0] == 0) {
            free(job_id);
            job_id = strdup("None");
        }

        /* Graphite doesn't support sub-second precision, as explained in:
         * http://graphite.readthedocs.io/en/latest/terminology.html#term-series
         *
         * timestamp
         * A point in time in which values can be associated. Time in Graphite is represented
         * as epoch time with a maximum resolution of 1-second.
         */
        if (asprintf(&data,
                     "%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%" PRIu32 ".%" PRId32 " %" PRIu64
                     " %ld\n",
                     schema,
                     mistral_scope_name[log_entry->scope],
                     mistral_contract_name[log_entry->contract_type],
                     mistral_measurement_name[log_entry->measurement],
                     log_entry->label,
                     path,
                     fstype,
                     fsname,
                     fshost,
                     log_entry->call_type_names,
                     log_entry->size_range,
                     job_gid,
                     job_id,
                     log_entry->hostname,
                     log_entry->cpu,
                     log_entry->mpi_rank,
                     log_entry->measured,
                     log_entry->epoch.tv_sec) < 0)
        {
            mistral_err("Could not allocate memory for log entry\n");
            mistral_shutdown();
            free(fshost);
            free(fsname);
            free(fstype);
            free(path);
            free(job_id);
            free(job_gid);
            return;
        }
        free(fshost);
        free(fsname);
        free(fstype);
        free(path);
        free(job_id);
        free(job_gid);

        /* As send needs to send data atomically send each log message separately */
        if (send(graphite_fd, data, strlen(data), 0) == -1) {
            char buf[256];
            mistral_err("Could not send data to Graphite %s\n",
                        strerror_r(errno, buf, sizeof buf));
        }

        free(data);

        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }
    log_list_tail = NULL;
}

/*
 * mistral_received_shutdown
 *
 * Function called whenever a shutdown message is received. Under normal
 * circumstances this message will be sent by Mistral outside of a data block
 * but in certain failure cases this may not be true.
 *
 * On receipt of a shutdown message check to see if there are any log entries
 * stored and, if so, call mistral_received_data_end to send them to Graphite.
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
