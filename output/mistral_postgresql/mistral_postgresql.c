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

#define DATE_FORMAT "YYYY-MM-DD"
#define DATETIME_FORMAT "YYYY-MM-DD HH-mm-SS"
#define DATE_LENGTH sizeof(DATE_FORMAT)
#define DATETIME_LENGTH sizeof(DATETIME_FORMAT)

#define VALID_NAME_CHARS "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_"

static char run_id[UUID_SIZE + 1];

static FILE **log_file_ptr = NULL;
static PGconn *con = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;
static void *rule_root = NULL;
static void *table_root = NULL;

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
                "  %s [-e file [-m octal-mode]] [-v var-name ...] [-h hostname] [-d database-name] [-u user] [-p password] [-P port]\n",
                name);
    mistral_err("\n"
                "  --mode=octal-mode\n"
                "  -m octal-mode\n"
                "     Permissions used to create the error log file specified by the -o\n"
                "     option.\n"
                "\n"
                "  --error=file\n"
                "  -e file\n"
                "     Specify location for error log. If not specified all errors will\n"
                "     be output on stderr and handled by Mistral error logging.\n"
                "\n"
                "  --var=var-name\n"
                "  -v var-name\n"
                "     The name of an environment variable, the value of which should be\n"
                "     stored by the plug-in. This option can be specified multiple times.\n"
                "\n"
                "  --host=hostname\n"
                "  -h hostname\n"
                "     The hostname of the PostgreSQL server with which to establish a\n"
                "     connection. If not specified the plug-in will default to \"localhost\".\n"
                "\n"
                "  --dbname=database_name\n"
                "  -d database_name\n"
                "     Set the database name to be used for storing data. Defaults to \"mistral_log\".\n"
                "\n"
                "  --password=secret\n"
                "  -p secret\n"
                "     The password required to access the PostgreSQL server if needed. If not\n"
                "     specified the plug-in will default to \"ellexus\".\n"
                "\n"
                "  --port=number\n"
                "  -P number\n"
                "     Specifies the port to connect to on the PostgreSQL server host.\n"
                "     If not specified the plug-in will default to \"5432\".\n"
                "\n"
                "  --username=user\n"
                "  -u user\n"
                "     The username required to access the PostgreSQL server if needed. If not\n"
                "     specified the plug-in will default to \"mistral\".\n"
                "\n");
}

static bool statements_prepared = false;
const char *get_rule_stmt_name = "GET_RULE_ID_FROM_PARAMS";
const char *insert_rule_details_stmt_name = "PUT_RULE_DETAILS";
const char *insert_count_stmt_name = "PUT_COUNT_RECORD";
const char *insert_bandwidth_stmt_name = "PUT_BANDWIDTH_RECORD";
const char *insert_latency_stmt_name = "PUT_LATENCY_RECORD";
const char *insert_memory_stmt_name = "PUT_MEMORY_RECORD";
const char *insert_cpu_stmt_name = "PUT_CPU_RECORD";
const char *insert_seek_stmt_name = "PUT_SEEK_RECORD";

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
                                        "(rule_label, violation_path, " \
                                        "call_type, measurement, "      \
                                        "size_range, threshold)"        \
                                        "VALUES ($1,$2,$3,$4,$5,$6) "   \
                                        "RETURNING rule_id";
        res = PQprepare(con, insert_rule_details_stmt_name, insert_rule_details_sql, 6,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert rule prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_count_sql =
            "INSERT INTO counts (plugin_run_id, rule_id, time_stamp, scope, type, mistral_record, " \
            " measure, timeframe, host, fstype, fsname, fshost, pid, cpu, command, file_name, "     \
            " group_id, id, mpi_rank"                                                               \
            ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19)";
        res = PQprepare(con, insert_count_stmt_name, insert_count_sql, 19,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert counts prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_bandwidth_sql =
            "INSERT INTO bandwidth (plugin_run_id, rule_id, time_stamp, scope, type, mistral_record," \
            " measure, timeframe, host, fstype, fsname, fshost, pid, cpu, command, file_name, "       \
            " group_id, id, mpi_rank"                                                                 \
            ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19)";
        res = PQprepare(con, insert_bandwidth_stmt_name, insert_bandwidth_sql, 19,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert bandwidth prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_latency_sql =
            "INSERT INTO latency (plugin_run_id, rule_id, time_stamp, scope, type, mistral_record," \
            " measure, timeframe, host, fstype, fsname, fshost, pid, cpu, command, file_name, "     \
            " group_id, id, mpi_rank"                                                               \
            ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19)";
        res = PQprepare(con, insert_latency_stmt_name, insert_latency_sql, 19,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert latency prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_memory_sql =
            "INSERT INTO memory (plugin_run_id, rule_id, time_stamp, scope, type, mistral_record," \
            " measure, timeframe, host, fstype, fsname, fshost, pid, cpu, command, file_name, "    \
            " group_id, id, mpi_rank"                                                              \
            ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19)";
        res = PQprepare(con, insert_memory_stmt_name, insert_memory_sql, 19,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert memory prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_cpu_sql =
            "INSERT INTO cpu (plugin_run_id, rule_id, time_stamp, scope, type, mistral_record," \
            " measure, timeframe, host, fstype, fsname, fshost, pid, cpu, command, file_name, " \
            " group_id, id, mpi_rank"                                                           \
            ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19)";
        res = PQprepare(con, insert_cpu_stmt_name, insert_cpu_sql, 19,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert cpu prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_seek_sql =
            "INSERT INTO seek_distance (plugin_run_id, rule_id, time_stamp, scope, type, mistral_record," \
            " measure, timeframe, host, fstype, fsname, fshost, pid, cpu, command, file_name, group_id, " \
            " id, mpi_rank"                                                                               \
            ") VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19)";
        res = PQprepare(con, insert_seek_stmt_name, insert_seek_sql, 19,
                        NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert seek distance prepared statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
            PQclear(res);
            goto fail_prepared_statements;
        }
        PQclear(res);

        char *insert_env_sql =
            "INSERT INTO env (plugin_run_id, env_name, env_value) VALUES ($1,$2,$3)";
        res = PQprepare(con, insert_env_stmt_name, insert_env_sql, 6, NULL);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Insert into environment statement creation failed\n");
            mistral_err("%s\n",  PQresultErrorMessage(res));
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
        *ptr_rule_id = atoi(PQgetvalue(res, 0, 0));
        this_rule->rule_id = *ptr_rule_id;
    } else if (received == 0) {
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
        {"host", required_argument, NULL, 'h'},
        {"username", required_argument, NULL, 'u'},
        {"password", required_argument, NULL, 'p'},
        {"port", required_argument, NULL, 'P'},
        {"databasename", required_argument, NULL, 'd'},
        {0, 0, 0, 0},
    };

    const char *error_file = NULL;
    int opt;
    mode_t new_mode = 0;

    const char *host = "localhost";
    const char *username = "mistral";
    const char *password = "ellexus";
    const char *dbname = "mistral_log";
    uint16_t port = 5432;

    while ((opt = getopt_long(argc, argv, "m:e:v:h:u:p:P:d:", options, NULL)) != -1) {
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
        case 'h':
            host = optarg;
            break;
        case 'u':
            username = optarg;
            break;
        case 'p':
            password = optarg;
            break;
        case 'd':
            dbname = optarg;
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

    static char *connection_string = NULL;

    if (asprintf(&connection_string, "user=%s password=%s host=%s port=%d dbname=%s", username,
                 password, host, port, dbname) < 0)
    {
        mistral_err("Could not allocate memory for connection String\n");
        return;
    }

    /* Setup the PostgreSQL database connection */
    con = PQconnectdb(connection_string);

    if (PQstatus(con) == CONNECTION_BAD) {
        mistral_err("Unable to connect to PostgreSQL: %s\n", PQerrorMessage(con));
        return;
    }

    /* Generate a unique run_id to link records in the database */
    uuid_t urun_id;
    uuid_generate(urun_id);
    uuid_unparse(urun_id, run_id);

    /* Setup the prepared statements used by the plug-in */
    if (!setup_prepared_statements()) {
        mistral_err("Unable to setup Prepared statements in PostgreSQL");
        return;
    }

    /* Insert the environment records - we only get these at the start of the run, so might as well
     * record them here too */
    if (!insert_env_records()) {
        mistral_err("Unable to record environment variables");
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
 * found. Clean up linked lists, search trees, any open error log and the PostgreSQL
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
 * to PostgreSQL. Remove each log_entry from the linked list as they are processed
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
    char timestamp[DATETIME_LENGTH];
    const char *values[19];

    char *value = NULL;
    char *tf = NULL;
    char *ruleid = NULL;
    char *pid = NULL;
    char *cpu = NULL;
    char *mpirank = NULL;

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        /* Get (or create) the appropriate rule id for this log entry */
        if (!set_rule_id(log_entry, &rule_id)) {
            mistral_shutdown();
            return;
        }

        strftime(timestamp, sizeof(timestamp), "%F %T", &log_entry->time);

        values[0] = run_id;
        if (asprintf(&ruleid, "%ld", rule_id)) {
            values[1] = ruleid;
        }
        values[2] = timestamp;
        values[3] = mistral_scope_name[log_entry->scope];
        values[4] = mistral_contract_name[log_entry->contract_type];
        values[5] = log_entry->measured_str;
        if (asprintf(&value, "%" PRIu64, log_entry->measured)) {
            values[6] = value;
        }
        if (asprintf(&tf, "%" PRIu64, log_entry->timeframe)) {
            values[7] = tf;
        }
        values[8] = log_entry->hostname;
        values[9] = log_entry->fstype;
        values[10] = log_entry->fsname;
        values[11] = log_entry->fshost;
        if (asprintf(&pid, "%" PRIu64, log_entry->pid)) {
            values[12] = pid;
        }

        if (asprintf(&cpu, "%d", log_entry->cpu)) {
            values[13] = cpu;
        }

        values[14] = log_entry->command;
        values[15] = log_entry->file;
        values[16] = log_entry->job_group_id;
        values[17] = log_entry->job_id;
        if (asprintf(&mpirank, "%d", log_entry->mpi_rank)) {
            values[18] = mpirank;
        }

        const char *correct_table_stmt = insert_bandwidth_stmt_name;
        switch (log_entry->measurement) {
        case MEASUREMENT_CPU_TIME:
        case MEASUREMENT_SYSTEM_TIME:
        case MEASUREMENT_USER_TIME:
        case MEASUREMENT_HOST_USER:
        case MEASUREMENT_HOST_SYSTEM:
        case MEASUREMENT_HOST_IOWAIT:
            correct_table_stmt = insert_cpu_stmt_name;
            break;
        case MEASUREMENT_MEMORY_VSIZE:
        case MEASUREMENT_MEMORY_RSS:
        case MEASUREMENT_MEMORY:
            correct_table_stmt = insert_memory_stmt_name;
            break;
        case MEASUREMENT_TOTAL_LATENCY:
        case MEASUREMENT_MEAN_LATENCY:
        case MEASUREMENT_MAX_LATENCY:
        case MEASUREMENT_MIN_LATENCY:
            correct_table_stmt = insert_latency_stmt_name;
            break;
        case MEASUREMENT_SEEK_DISTANCE:
            correct_table_stmt = insert_seek_stmt_name;
            break;
        case MEASUREMENT_COUNT:
            correct_table_stmt = insert_count_stmt_name;
            break;
        case MEASUREMENT_BANDWIDTH:
            correct_table_stmt = insert_bandwidth_stmt_name;
            break;
        case MEASUREMENT_MAX:
            /* This shouldn't be a rule type - I'm not sure what it is. */
            correct_table_stmt = insert_latency_stmt_name;
            break;
        }

        PGresult *res = PQexecPrepared(con, correct_table_stmt, 19, values, NULL, NULL, 0);
        /* Has the prepared statement inserted correctly? */
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            mistral_err("Unable to save log record (%s) %s\n", correct_table_stmt,
                        PQresultErrorMessage(res));
            PQclear(res);
            mistral_shutdown();
            return;
        }
        PQclear(res);

        free(ruleid);
        free(pid);
        free(cpu);
        free(mpirank);

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
 * stored and, if so, call mistral_received_data_end to send them to PostgreSQL.
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
