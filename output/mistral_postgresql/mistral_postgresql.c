#include <assert.h>             /* assert */
#include <errno.h>              /* errno */
#include <fcntl.h>              /* open */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <libpq-fe.h>           /* PQconnectdb, etc */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */
#include <sys/stat.h>           /* open, umask */
#include <sys/types.h>          /* open, umask */
#include <uuid/uuid.h>          /* uuid_generate, uuid_parse */

#include "mistral_plugin.h"

/* Define some database column sizes */
#define RATE_SIZE 64
#define STRING_SIZE 256
#define LONG_STRING_SIZE 1405
#define MEASUREMENT_SIZE 13
#define UUID_SIZE 36

/* Arbritrary string buffer size limit - copied from mysql */
#define BUFFER_SIZE 1000000
#define DATE_FORMAT "YYYY-MM-DD"
#define DATETIME_FORMAT "YYYY-MM-DD HH-mm-SS"
#define DATE_LENGTH sizeof(DATE_FORMAT)
#define DATETIME_LENGTH sizeof(DATETIME_FORMAT)

#define VALID_NAME_CHARS "1234567890abcdefghijklmnopqrstvuwxyzABCDEFGHIJKLMNOPQRSTVUWXYZ-_"

static char run_id[UUID_SIZE + 1];

static FILE **log_file_ptr = NULL;
static PGconn *con = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;
static void *rule_root = NULL;
static void *table_root = NULL;

static char *log_insert = NULL;
static size_t log_insert_len = 0;

typedef struct rule_param {
    char label[STRING_SIZE + 1];
    char path[STRING_SIZE + 1];
    uint32_t call_types;
    enum mistral_measurement measurement;
    char size_range[RATE_SIZE + 1];
    char threshold[RATE_SIZE + 1];
    long rule_id;
} rule_param;

typedef struct env_table {
    int table_number;
    char date[DATE_LENGTH + 1];
} env_table;

typedef struct env_var {
    struct env_var *forward;
    struct env_var *backward;
    char name[STRING_SIZE + 1];
    char value[STRING_SIZE + 1];
} env_var;

static env_var *env_head = NULL;
static env_var *env_tail = NULL;

static void usage(const char *name)
{
    mistral_err("Usage:\n"
                "  %s -c config [-o file [-m octal-mode]] [-v var-name ...]\n", name);
    mistral_err("\n"
                "  --defaults-file=config\n"
                "  -c config\n"
                "     Location of a PostgreSQL formatted options file \"config\" that\n"
                "     contains database connection configuration.\n"
                "\n"
                "  --mode=octal-mode\n"
                "  -m octal-mode\n"
                "     Permissions used to create the error log file specified by the -o\n"
                "     option.\n"
                "\n"
                "  --output=file\n"
                "  -o file\n"
                "     Specify location for error log. If not specified all errors will\n"
                "     be output on stderr and handled by Mistral error logging.\n"
                "\n"
                "  --var=var-name\n"
                "  -v var-name\n"
                "     The name of an environment variable, the value of which should be\n"
                "     stored by the plug-in. This option can be specified multiple times.\n"
                "\n");
}

static bool statements_prepared = false;
const char *get_rule_stmt_name = "GET_RULE_ID_FROM_PARAMS";
const char *insert_rule_details_stmt_name = "PUT_RULE_DETAILS";
const char *insert_measure_stmt_name = "PUT_MEASURE";
const char *insert_env_stmt_name = "PUT_ENV";

static bool setup_prepared_statements()
{
    if (!statements_prepared) {
        char *get_rule_params_id_sql = "SELECT rule_id FROM rule_details "              \
                                       "WHERE rule_label = $1 AND violation_path = $2 " \
                                       "AND call_type = $3 AND measurement = $4 AND "   \
                                       "size_range = $5 AND threshold = $6";
        PGresult *res = PQprepare(con, get_rule_stmt_name, get_rule_params_id_sql, 6, NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Get rule prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_rule_details_sql = "INSERT INTO rule_details"
                                        "(rule_label, violation_path, call_type, measurement, " \
                                        "size_range, threshold)"                                \
                                        "VALUES ($1,$2,$3,$4,$5,$6) RETURNING rule_id";
        res = PQprepare(con, insert_rule_details_stmt_name, insert_rule_details_sql, 6,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert rule prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_rec_sql = "INSERT INTO mistral_log (scope, type, time_stamp, host," \
                               "rule_id, observed, pid, cpu, command,"                   \
                               "file_name, group_id, id, mpi_rank, plugin_run_id"        \
                               ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14) RETURNING log_id";
        res = PQprepare(con, insert_measure_stmt_name, insert_rec_sql, 14,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert measurement prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_env_sql =
            "INSERT INTO env (plugin_run_id, env_name, env_value) VALUES ($1,$2,$3)";
        res = PQprepare(con, insert_env_stmt_name, insert_env_sql, 6, NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Get rule prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            fprintf(stderr, "%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        statements_prepared = true;
    }
    return statements_prepared;
fail_prepared_statements:
    mistral_err("setup_prepared_statements failed!\n");
    return false;
}

/*
 * insert_rule_details
 *
 * Inserts a record into the rule_details table and retrieves the related ID
 * for the inserted record
 *
 * Parameters:
 *   log_entry   - A Mistral log record data structure containing the received
 *                 log information.
 *   ptr_rule_id - Pointer to the variable to be populated with the related
 *                 record ID
 *
 * Returns:
 *   true if the record was inserted successfully
 *   false otherwise
 */
static bool insert_rule_details(mistral_log *log_entry, long *ptr_rule_id)
{
    PGresult *res;
    const char *values[6];

    values[0] = log_entry->label;
    values[1] = log_entry->path;
    values[2] = log_entry->call_type_names;
    values[3] = mistral_measurement_name[log_entry->measurement];
    values[4] = log_entry->size_range;
    values[5] = log_entry->threshold_str;

    res = PQexecPrepared(con, insert_rule_details_stmt_name, 6, values, NULL, NULL, 0);

    /* Has the query returned results */
    if (NULL == res || PGRES_TUPLES_OK != PQresultStatus(res)) {
        mistral_err("PQexecParams(insert rule) failed\n");
        mistral_err("%s\n", PQresultErrorMessage(res));
        fprintf(stderr, "%s\n", PQresultErrorMessage(res));
        if (res != NULL) {
            PQclear(res);
        }
        goto fail_insert_rule_details;
    }

    int received = PQntuples(res);
    if (received == 1) {
        /* Sets the rule_id to this new inserted value */
        assert(ptr_rule_id);
        *ptr_rule_id = atoi(PQgetvalue(res, 0, 0));
    } else {
        mistral_err("Expected to get 1 record with rule-id - but got %d\n", received);
        PQclear(res);
        goto fail_insert_rule_details;
    }

    PQclear(res);

    return true;

fail_insert_rule_details:
    mistral_err("insert_rule_details failed\n");
    return false;
}

/*
 * insert_env_records
 *
 * This function checks to see if any additional environment varaibles that were
 * specified at the command line have been stored for this job in the env table.
 *
 * We do not expect a large number of these environment variables to be
 * specified and, even so, will only need to do this once so
 * there is no need to construct a large bulk insert.
 *
 * Returns:
 *   true if environment variables are saved successfully
 *   false otherwise
 */
static bool insert_env_records()
{
    /* Do we have any environment variables to store? */
    if (!env_head) {
        /* Nothing to do */
        return true;
    }

    const char *input_bind[3];

    env_var *variable = env_head;
    while (variable) {
        input_bind[0] = run_id;
        input_bind[1] = variable->name;
        input_bind[2] = variable->value;

        PGresult *res = PQexecPrepared(con, insert_env_stmt_name, 3, input_bind, NULL, NULL, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Failed to insert env record\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_insert_env_records;
        }
        PQclear(res);

        variable = variable->forward;
    }

    return true;
fail_insert_env_records:
    return false;
}

/*
 * rule_compare
 *
 * Comparison function for use with tsearch to compare two rules.
 *
 * Parameters:
 *   p  - pointer to the first rule stored in a rule_param structure
 *   q  - pointer to the second rule stored in a rule_param structure
 *
 * Returns:
 *   < 0 if first rule is "less than" the second rule
 *   0   if first rule is "equal to" the second rule
 *   > 0 if first rule is "greater than" the second rule
 */
static int rule_compare(const void *p, const void *q)
{
    const rule_param *rule1 = p;
    const rule_param *rule2 = q;
    int retval = 0;
    int64_t tmpval = 0;

    retval = strcmp(rule1->label, rule2->label);
    if (retval) {
        return retval;
    }

    retval = strcmp(rule1->path, rule2->path);
    if (retval) {
        return retval;
    }

    tmpval = (int64_t)rule1->call_types - (int64_t)rule2->call_types;
    if (tmpval < 0) {
        retval = -1;
    } else if (tmpval > 0) {
        retval = 1;
    }
    if (retval) {
        return retval;
    }

    retval = rule1->measurement - rule2->measurement;
    if (retval) {
        return retval;
    }

    retval = strcmp(rule1->size_range, rule2->size_range);
    if (retval) {
        return retval;
    }

    retval = strcmp(rule1->threshold, rule2->threshold);
    return retval;
}

/*
 * set_rule_id
 *
 * This function checks to see if the violated rule details are already present
 * in the rule_details table and, if so, selects the related record ID. If
 * the record does not already exist insert_rule_details is called to create
 * it.
 *
 * While this schema creates a nicely normalised dataset it is not particularly
 * efficient for inserts. For write efficiency it may actually be better to
 * include the raw data in the log row.
 *
 * Parameters:
 *   log_entry   - A Mistral log record data structure containing the received
 *                 log information.
 *   ptr_rule_id - Pointer to the variable to be populated with the related
 *                 record ID
 *
 * Returns:
 *   true if the record was found or inserted successfully
 *   false otherwise
 *
 */
static bool set_rule_id(mistral_log *log_entry, long *ptr_rule_id)
{
    const char    *input_bind[6];
    rule_param    *this_rule;
    void          *found;
    int received;

    /* First let's check if we have seen the rule before */
    this_rule = calloc(1, sizeof(rule_param));
    if (this_rule) {
        strncpy(this_rule->label, log_entry->label, STRING_SIZE);
        this_rule->label[STRING_SIZE] = '\0';
        strncpy(this_rule->path, log_entry->path, STRING_SIZE);
        this_rule->path[STRING_SIZE] = '\0';
        this_rule->call_types = log_entry->call_type_mask;
        this_rule->measurement = log_entry->measurement;
        strncpy(this_rule->size_range, log_entry->size_range, RATE_SIZE);
        this_rule->size_range[RATE_SIZE] = '\0';
        strncpy(this_rule->threshold, log_entry->threshold_str, RATE_SIZE);
        this_rule->threshold[RATE_SIZE] = '\0';

        found = tsearch((void *)this_rule, &rule_root, rule_compare);
        if (found == NULL) {
            mistral_err("Out of memory in tsearch\n");
            free(this_rule);
            this_rule = NULL;
            goto fail_set_rule_id;
        } else if (*(rule_param **)found != this_rule) {
            *ptr_rule_id = (*(rule_param **)found)->rule_id;
            free(this_rule);
            this_rule = NULL;
            return true;
        }
    } else {
        mistral_err("Unable to allocate memory for rule to be used in tsearch\n");
        goto fail_set_rule_id;
    }
    /* If we have got here this is the first time we have seen this rule - see
     * if it is already in the database.
     */

    /* Prepares the statement for use */
    input_bind[0] = log_entry->label;
    input_bind[1] = log_entry->path;
    input_bind[2] = log_entry->call_type_names;
    input_bind[3] = mistral_measurement_name[log_entry->measurement];
    input_bind[4] = log_entry->size_range;
    input_bind[5] = log_entry->threshold_str;

    /** Get matching rules from the database */
    PGresult *res = PQexecPrepared(con, get_rule_stmt_name, 6, input_bind, NULL, NULL, 0);
    /* Has the query returned results */
    if (NULL == res || PGRES_TUPLES_OK != PQresultStatus(res)) {
        mistral_err("PQexecPrepared(get_rule_stmt_name) failed\n");
        mistral_err("%s\n", PQresultErrorMessage(res));
        goto fail_set_rule_id;
    }

    received = PQntuples(res);
    if (received == 1) {
        /* We found the rule in the DB, store this result in the tsearch tree */
        this_rule->rule_id = atoi(PQgetvalue(res, 0, 0)); /* TODO: This previously did something
                                                           * with pointers - is this
                                                           * now correct? */
    } else if (received == 0) {
        PQclear(res);
        if (!insert_rule_details(log_entry, ptr_rule_id)) {
            tdelete((void *)this_rule, &rule_root, rule_compare);
            goto fail_set_rule_id;
        }
        /* Store the freshly inserted ID in the tsearch tree */
        this_rule->rule_id = *ptr_rule_id;
    } else {
        mistral_err("Expected 1 returned row but received %u\n", received);
        PQclear(res);
        goto fail_set_rule_id;
    }
    PQclear(res);

    return true;

fail_set_rule_id:
    mistral_err("Set_rule_ID failed!\n");
    return false;
}

/*
 * build_log_values_string
 *
 * Create a values string for use with the main log insert statement.
 *
 * The log insert values raw performance over absolute data integrity so uses
 * a custom insert query containing multiple insert VALUES strings which must
 * be built correctly in order to be safe to insert. We can insert as many rows
 * as we like up to the limit allowed by the communication buffer size We do the
 * insert this way rather than using bind variables as this would limit us to
 * inserting a single row at a time.
 *
 * Parameters:
 *   log_entry   - A Mistral log record data structure containing the received
 *                 log information.
 *   rule_id     - The rule ID related to this log message.
 *
 * Returns:
 *   A pointer to the string containing the VALUES string or
 *   NULL on error
 */
static char *build_log_values_string(mistral_log *log_entry, long rule_id)
{
    /* Set up variables to hold the mysql escaped version of potentially
     * unsafe strings
     */
    char escaped_command[LONG_STRING_SIZE * 2 + 1];
    char escaped_filename[LONG_STRING_SIZE * 2 + 1];
    char escaped_hostname[STRING_SIZE * 2 + 1] = "";
    char escaped_groupid[STRING_SIZE * 2 + 1];
    char escaped_id[STRING_SIZE * 2 + 1];
    char timestamp[DATETIME_LENGTH];
    char *values_string = NULL;

    int *error_num = 0;

    PQescapeStringConn(con, escaped_command, log_entry->command, strlen(log_entry->command),
                       error_num);
    PQescapeStringConn(con, escaped_command, log_entry->command, strlen(log_entry->command),
                       error_num);
    PQescapeStringConn(con, escaped_filename, log_entry->file, strlen(log_entry->file),
                       error_num);
    PQescapeStringConn(con, escaped_hostname, log_entry->hostname, strlen(log_entry->hostname),
                       error_num);
    PQescapeStringConn(con, escaped_groupid, log_entry->job_group_id,
                       strlen(log_entry->job_group_id), error_num);
    PQescapeStringConn(con, escaped_id, log_entry->job_id, strlen(log_entry->job_id), error_num);

    /* Converts the timestamp to a formatted string */
    strftime(timestamp, sizeof(timestamp), "%F %T", &log_entry->time);

    #define LOG_VALUES "('%s', '%s', '%s.%06" PRIu32 "', '%s', %lu, '%s', %" PRIu64 \
    ", %" PRId32 ", '%s', '%s', '%s', '%s', %" PRId32                               \
    ",'%s')"

    if (asprintf(&values_string,
                 LOG_VALUES,
                 mistral_scope_name[log_entry->scope],
                 mistral_contract_name[log_entry->contract_type],
                 timestamp,
                 log_entry->microseconds,
                 escaped_hostname,
                 rule_id,
                 log_entry->measured_str,
                 log_entry->pid,
                 log_entry->cpu,
                 escaped_command,
                 escaped_filename,
                 escaped_groupid,
                 escaped_id,
                 log_entry->mpi_rank,
                 run_id) < 0)
    {
        mistral_err("build_log_values_string failed to allocate memory in asprintf\n");
        goto fail_build_log_values_string;
    }

    return values_string;

fail_build_log_values_string:
    mistral_err("build_log_values_string failed!\n");
    return NULL;
}

/*
 * insert_log_to_db
 *
 * Performs the saved insert statement then frees the memory and resets the
 * related global variables.
 *
 * Parameters:
 *   void
 *
 * Returns:
 *   true if the records were inserted successfully
 *   false otherwise
 */
static bool insert_log_to_db(void)
{
    /* Execute the statement */
    PGresult *res = PQexec(con, log_insert);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        mistral_err("Failed while inserting log entry\n");
        mistral_err("%s\n",  PQresultErrorMessage(res));
        mistral_err("%s\n", log_insert);
        mistral_err("Insert_log_to_db failed!\n");
        return false;
    }
    PQclear(res);

    free(log_insert);
    log_insert = NULL;
    log_insert_len = 0;

    return true;
}

/*
 * mistral_startup
 *
 * Required function that initialises the type of plug-in we are running. This
 * function is called immediately on plug-in start-up.
 *
 * In addition this plug-in needs to initialise the PostgreSQL connection
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

    const struct option options[] = {
        {"error", required_argument, NULL, 'e'},
        {"mode", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'e'},
        {"var", required_argument, NULL, 'v'},
        {0, 0, 0, 0},
    };

    const char *error_file = NULL;
    int opt;
    mode_t new_mode = 0;

    while ((opt = getopt_long(argc, argv, "m:e:v:", options, NULL)) != -1) {
        switch (opt) {
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
        case 'e':
            error_file = optarg;
            break;
        case 'v': {
            env_var *this_env = NULL;

            if (optarg[0] != '\0' && strspn(optarg, VALID_NAME_CHARS) == strlen(optarg)) {
                this_env = calloc(1, sizeof(env_var));
                if (this_env) {
                    char *temp_var = getenv(optarg);
                    if (temp_var) {
                        /* calloc ensures NULL termination */
                        strncpy(this_env->value, temp_var, STRING_SIZE);
                    } else {
                        this_env->value[0] = '\0';
                    }
                    strncpy(this_env->name, optarg, STRING_SIZE);

                    if (!env_head) {
                        /* Initialise linked list */
                        env_head = this_env;
                        env_tail = this_env;
                        insque(this_env, NULL);
                    } else {
                        insque(this_env, env_tail);
                        env_tail = this_env;
                    }
                } else {
                    mistral_err("Could not allocate memory for environment variable\n");
                    return;
                }
            } else {
                mistral_err("Invalid environment variable name %s\n", optarg);
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

    /* Setup the PostgreSQL database connection
     * TODO: Stop using default username/password etc.
     */
    con = PQconnectdb("user=mistral password=ellexus host=localhost port=5432 dbname=mistral_log");

    if (PQstatus(con) == CONNECTION_BAD) {
        mistral_err("Unable to connect to PostgreSQL: %s\n", PQerrorMessage(con));
        PQfinish(con);
        return;
    }

    /* Generate a unique run_id to link records in the database */
    uuid_t urun_id;
    uuid_generate(urun_id);
    uuid_unparse(urun_id, run_id);

    /* Setup the prepared statements used by the plug-in */
    if (!setup_prepared_statements()) {
        mistral_shutdown();
        return;
    }

    /* Insert the environment records - we only get these at the start of the run, so might as well
     * record them here too */
    if (!insert_env_records()) {
        mistral_shutdown();
        return;
    }

    /* Returning after this point indicates success */
    plugin->type = OUTPUT_PLUGIN;
}

/*
 * mistral_exit
 *
 * Function called immediately before the plug-in exits. Check for any unhandled
 * log entries and call mistral_received_data_end to process them if any are
 * found. Clean up linked lists, search trees, any open error log and the MySQL
 * connection.
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

    if (con) {
        PQfinish(con);
    }

    if (rule_root) {
        tdestroy(rule_root, free);
    }

    if (table_root) {
        tdestroy(table_root, free);
    }

    env_var *variable = env_head;
    while (variable) {
        env_head = variable->forward;
        remque(variable);
        free(variable);
        variable = env_head;
    }
    env_tail = NULL;

    if (log_file_ptr && *log_file_ptr != stderr) {
        fclose(*log_file_ptr);
        *log_file_ptr = stderr;
    }
}

/*
 * mistral_received_log
 *
 * Function called whenever a log message is received. Simply store the log
 * entry received in a linked list until we reach the end of this data block.
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
 * to MySQL. Remove each log_entry from the linked list as they are processed
 * and destroy them.
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
    long rule_id = 0;

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        /* Get (or create) the appropriate rule id for this log entry */
        if (!set_rule_id(log_entry, &rule_id)) {
            mistral_shutdown();
            return;
        }

        char *values = build_log_values_string(log_entry, rule_id);
        size_t values_len = strlen(values);

        if ((log_insert_len + values_len) > BUFFER_SIZE) {
            if (!insert_log_to_db()) {
                mistral_err("Writing logs to mysql due to full buffer failed\n");
                mistral_shutdown();
                return;
            }
        }

        if (log_insert_len == 0) {
            /* Create the insert statement */
            if (asprintf(&log_insert,
                         "INSERT INTO mistral_log (scope, type, time_stamp, host," \
                         "rule_id, observed, pid, cpu, command,"                   \
                         "file_name, group_id, id, mpi_rank, plugin_run_id) VALUES %s", values) < 0)
            {
                mistral_err("Unable to allocate memory for log insert\n");
                mistral_shutdown();
                return;
            }
            log_insert_len = strlen(log_insert);
        } else {
            /* Append values on the insert statement */
            char *old_log_insert = log_insert;
            if (asprintf(&log_insert, "%s,%s", log_insert, values) < 0) {
                mistral_err("Unable to allocate memory for log insert\n");
                mistral_shutdown();
                free(old_log_insert);
                return;
            }
            free(old_log_insert);
            log_insert_len += values_len + 1;
        }

        free(values);
        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }
    log_list_tail = NULL;

    /* Send any log entries to the database that are still pending */
    if (!insert_log_to_db()) {
        mistral_err("Insert log entry at end of block failed\n");
        mistral_shutdown();
        return;
    }
}

/*
 * mistral_received_shutdown
 *
 * Function called whenever a shutdown message is received. Under normal
 * circumstances this message will be sent by Mistral outside of a data block
 * but in certain failure cases this may not be true.
 *
 * On receipt of a shutdown message check to see if there are any log entries
 * stored and, if so, call mistral_received_data_end to send them to MySQL.
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
