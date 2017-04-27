#include <assert.h>             /* assert */
#include <errno.h>              /* errno */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <mysql.h>              /* mysql_init, mysql_close, mysql_stmt_*, etc */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */

#include "mistral_plugin.h"

/* Define some database column sizes */
#define RATE_SIZE 64
#define LOG_TABLE_NAME_SIZE 6
#define STRING_SIZE 256
#define MEASUREMENT_SIZE 13

#define DATE_FORMAT "YYYY-MM-DD"
#define DATETIME_FORMAT "YYYY-MM-DD HH-mm-SS"
#define DATE_LENGTH sizeof(DATE_FORMAT)
#define DATETIME_LENGTH sizeof(DATETIME_FORMAT)

#define BIND_STRING(b, i, str, null_is, str_len)    \
    b[i].buffer_type = MYSQL_TYPE_STRING;           \
    b[i].buffer = (char *)str;                      \
    b[i].buffer_length = STRING_SIZE;               \
    b[i].is_null = null_is;                         \
    b[i].length = &str_len;

#define BIND_INT(b, i, integer, null_is)            \
    b[i].buffer_type = MYSQL_TYPE_LONG;             \
    b[i].buffer = (char *)integer;                  \
    b[i].is_null = null_is;                         \
    b[i].length = 0;

static FILE *log_file = NULL;
static MYSQL *con = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;

/*
 * get_log_table_name
 *
 * This function retrieves the name of the table that contains log records for
 * the date of the logged event.
 *
 * Parameters:
 *   log_entry      - A Mistral log record data structure containing the
 *                    received log information.
 *   selected_table - Pointer to the string to be populated with appropriate
 *                    table name
 *
 * Returns:
 *   true if the table name was found
 *   false otherwise
 */
bool get_log_table_name(const mistral_log *log_entry, char *selected_table)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT      *get_table_name;
    MYSQL_BIND      input_bind[1];
    MYSQL_BIND      output_bind[1];
    unsigned long   str_length;
    unsigned long   result_str_length;
    char            log_date[STRING_SIZE];

    get_table_name = mysql_stmt_init(con);
    if (!get_table_name) {
        mistral_err("mysql_stmt_init() out of memory for get_table_name\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Prepares the statement for use */
    char *get_log_table_name_str =
        "SELECT table_name FROM control_table WHERE table_date = DATE_FORMAT(?,'%Y-%m-%d')";
    if (mysql_stmt_prepare(get_table_name, get_log_table_name_str,
                           strlen(get_log_table_name_str)))
    {
        mistral_err("mysql_stmt_prepare(get_table_name) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));
    memset(output_bind, 0, sizeof(output_bind));

    /* Set the variables to use for input parameters in the SELECT query */
    BIND_STRING(input_bind, 0, log_date, 0, str_length);

    /* Set the variables to use to store the values returned by the SELECT query */
    BIND_STRING(output_bind, 0, selected_table, 0, result_str_length);


    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(get_table_name, input_bind)) {
        mistral_err("mysql_stmt_bind_param(get_table_name) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Set the date to look up */
    strftime(log_date, sizeof(log_date), "%F", &log_entry->time);
    str_length = strlen(log_date);

    /* Execute the query */
    if (mysql_stmt_execute(get_table_name)) {
        mistral_err("mysql_stmt_execute(get_table_name), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Connect the output variables to the results of the query */
    if (mysql_stmt_bind_result(get_table_name, output_bind)) {
        mistral_err("mysql_stmt_bind_result(get_table_name), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
    }

    /* Get all returned rows locally */
    mysql_stmt_store_result(get_table_name);
    my_ulonglong received = mysql_stmt_num_rows(get_table_name);

    if (received != 1) {
        mistral_err("Expected 1 returned row but received %d\n", received);
        goto fail_get_log_table_name;
    }

    /* Populate the output variables with the returned data */
    if (mysql_stmt_fetch(get_table_name)) {
        mistral_err("mysql_stmt_fetch(get_table_name), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Close the statement */
    if (mysql_stmt_close(get_table_name)) {
        mistral_err("Failed while closing the statement get_table_name\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    return true;

fail_get_log_table_name:
    mistral_err("get_log_table_name failed!\n");
    return false;
}

/*
 * insert_rule_parameters
 *
 * Inserts a record into the rule_parameters table and retrieves the related ID
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
bool insert_rule_parameters(mistral_log *log_entry, my_ulonglong *ptr_rule_id)
{
    MYSQL_STMT   *insert_rule;
    MYSQL_BIND    input_bind[5];
    unsigned long str_length_vio;
    unsigned long str_length_call;
    unsigned long str_length_measure;
    unsigned long str_length_size_range;
    unsigned long str_length_threshold;
    char         *insert_rule_parameters_str;

    insert_rule = mysql_stmt_init(con);
    if (!insert_rule) {
        mistral_err("mysql_stmt_init() out of memory for insert_rule\n");
        goto fail_insert_rule_parameters;
    }

    /* Prepares the statement for use */
    insert_rule_parameters_str = "INSERT INTO rule_parameters (rule_id, violation_path, call_type,"\
                                 "measurement, size_range, threshold) VALUES (NULL,?,?,?,?,?)";
    if (mysql_stmt_prepare(insert_rule, insert_rule_parameters_str,
                           strlen(insert_rule_parameters_str))) {
        mistral_err("mysql_stmt_prepare(insert_rule), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_parameters;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));

    /* Set the variables to use for input parameters in the INSERT query */
    BIND_STRING(input_bind, 0, log_entry->path, 0, str_length_vio);
    BIND_STRING(input_bind, 1, mistral_call_type_names[log_entry->call_type_mask], 0, str_length_call);
    BIND_STRING(input_bind, 2, mistral_measurement_name[log_entry->measurement], 0, str_length_measure);
    BIND_STRING(input_bind, 3, log_entry->size_range, 0, str_length_size_range);
    BIND_STRING(input_bind, 4, log_entry->threshold_str, 0, str_length_threshold);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(insert_rule, input_bind)) {
        mistral_err("mysql_stmt_bind_param(insert_rule) failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_parameters;
    }

    /* Set the length of the values of the variables used in the query, these will be truncated if
     * they exceed the size of the database column.
     */
    str_length_vio = strlen(log_entry->path);
    str_length_call = strlen(mistral_call_type_names[log_entry->call_type_mask]);
    str_length_measure = strlen(mistral_measurement_name[log_entry->measurement]);
    str_length_size_range = strlen(log_entry->size_range);
    str_length_threshold = strlen(log_entry->threshold_str);

    /* Execute the query */
    if (mysql_stmt_execute(insert_rule)) {
        mistral_err("mysql_stmt_execute(insert_rule), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_parameters;
    }

    /* Get the total rows affected */
    my_ulonglong affected_rows = mysql_stmt_affected_rows(insert_rule);
    if (affected_rows != 1) {
        mistral_err("Invalid number of rows inserted by insert_rule. Expected 1, saw %d\n",
                    affected_rows);
        goto fail_insert_rule_parameters;
    }

    /* Sets the rule_id to this new inserted value */
    assert(ptr_rule_id);
    *ptr_rule_id = mysql_stmt_insert_id(insert_rule);

    /* Close the statement */
    if (mysql_stmt_close(insert_rule)) {
        mistral_err("failed while closing the statement insert_rule\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_parameters;
    }

    return true;

fail_insert_rule_parameters:
    mistral_err("insert_rule_parameters failed\n");
    return false;
}

/*
 * set_rule_id
 *
 * This function checks to see if the violated rule details are already present
 * in the rule_parameters table and, if so, selects the related record ID. If
 * the record does not already exist insert_rule_parameters is called to create
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
bool set_rule_id(mistral_log *log_entry, my_ulonglong *ptr_rule_id)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT *get_rule_id;

    MYSQL_BIND    input_bind[5];
    MYSQL_BIND    output_bind[1];
    unsigned long str_length_vio;
    unsigned long str_length_call;
    unsigned long str_length_measure;
    unsigned long str_length_size_range;
    unsigned long str_length_threshold;

    get_rule_id = mysql_stmt_init(con);
    if (!get_rule_id) {
        mistral_err("mysql_stmt_init() out of memory for get_rule_id\n");
        goto fail_set_rule_id;
    }

    /* Prepares the statement for use */
    char *get_rule_params_id_str = "SELECT rule_id FROM rule_parameters WHERE violation_path=? " \
                                   "AND call_type=? AND measurement=? AND size_range=? AND " \
                                   "threshold=?";
    if (mysql_stmt_prepare(get_rule_id, get_rule_params_id_str,
        strlen(get_rule_params_id_str)))
    {
        mistral_err("mysql_stmt_prepare(get_rule_id) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_rule_id));
        goto fail_set_rule_id;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));
    memset(output_bind, 0, sizeof(output_bind));

    /* Set the variables to use for input parameters in the SELECT query */
    BIND_STRING(input_bind, 0, log_entry->path, 0, str_length_vio);
    BIND_STRING(input_bind, 1, mistral_call_type_names[log_entry->call_type_mask], 0, str_length_call);
    BIND_STRING(input_bind, 2, mistral_measurement_name[log_entry->measurement], 0, str_length_measure);
    BIND_STRING(input_bind, 3, log_entry->size_range, 0, str_length_size_range);
    BIND_STRING(input_bind, 4, log_entry->threshold_str, 0, str_length_threshold);

    /* Set the variables to use to store the values returned by the SELECT query */
    BIND_INT(output_bind, 0, ptr_rule_id, 0);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(get_rule_id, input_bind)) {
        mistral_err("mysql_stmt_bind_param(get_rule_id) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_rule_id));
        goto fail_set_rule_id;
    }

    /* Set the length of the values of the variables used in the query */
    str_length_vio = strlen(log_entry->path);
    str_length_call = strlen(mistral_call_type_names[log_entry->call_type_mask]);
    str_length_measure = strlen(mistral_measurement_name[log_entry->measurement]);
    str_length_size_range = strlen(log_entry->size_range);
    str_length_threshold = strlen(log_entry->threshold_str);

    /* Reset the lengths if they are larger than the column the string is being compared to. Doing
     * it this way round avoids calling strlen twice
     */
    str_length_vio = (str_length_vio > STRING_SIZE)? STRING_SIZE : str_length_vio;
    str_length_call = (str_length_call > STRING_SIZE)? STRING_SIZE : str_length_call;
    str_length_measure = (str_length_measure > MEASUREMENT_SIZE)? MEASUREMENT_SIZE : str_length_measure;
    str_length_size_range = (str_length_size_range > RATE_SIZE)? RATE_SIZE : str_length_size_range;
    str_length_threshold = (str_length_threshold > RATE_SIZE)? RATE_SIZE : str_length_threshold;


    /* Execute the query */
    if (mysql_stmt_execute(get_rule_id)) {
        mistral_err("mysql_stmt_execute(get_rule_id), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_rule_id));
        goto fail_set_rule_id;
    }

    /* Connect the output variables to the results of the query */
    if (mysql_stmt_bind_result(get_rule_id, output_bind)) {
        mistral_err("mysql_stmt_bind_result(get_rule_id), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_rule_id));
        goto fail_set_rule_id;
    }

    /* Get all returned rows locally so we can do error checking */
    mysql_stmt_store_result(get_rule_id);
    my_ulonglong received = mysql_stmt_num_rows(get_rule_id);

    if (received == 1) {
        /* Populate the output variables with the returned data */
        if (mysql_stmt_fetch(get_rule_id)) {
            mistral_err("mysql_stmt_fetch(get_rule_id), failed\n");
            mistral_err("%s\n", mysql_stmt_error(get_rule_id));
            goto fail_set_rule_id;
        }
    } else if (received == 0) {
        if (!insert_rule_parameters(log_entry, ptr_rule_id)) {
            goto fail_set_rule_id;
        }
    } else {
        mistral_err("Expected 1 returned row but received %d\n", received);
        goto fail_set_rule_id;
    }

    /* Close the statement */
    if (mysql_stmt_close(get_rule_id)) {
        mistral_err("failed while closing the statement get_rule_id\n");
        mistral_err("%s\n", mysql_stmt_error(get_rule_id));
        goto fail_set_rule_id;
    }

    return true;

fail_set_rule_id:
    mistral_err("Set_rule_ID failed!\n");
    return false;
}

/*
 * insert_log_to_db
 *
 * Inserts the log entry to in the appropriate log_XX table.
 *
 * This function values data integrity over raw performance and uses bind
 * variables to construct the query. Unfortunatly MySQL is limited to inserting
 * a single row at a time using this approach which is less perfomant than bulk
 * inserts. This function could be re-written to create a custom insert query
 * containing multiple insert VALUES strings (up to the limit allowed by the
 * communication buffer size - https://dev.mysql.com/doc/refman/5.7/en/c-api.html)
 *
 * Parameters:
 *   table_name  - A string containing the name of the table into which the
 *                 record must be inserted.
 *   log_entry   - A Mistral log record data structure containing the received
 *                 log information.
 *   rule_id     - The rule ID related to this log message.
 *
 * Returns:
 *   true if the record was inserted successfully
 *   false otherwise
 */
bool insert_log_to_db(char *table_name, mistral_log *log_entry, my_ulonglong rule_id)
{
    #define LOG_INSERT "INSERT INTO %s (scope, type, time_stamp, label, rule_parameters," \
                                       "observed, host, pid, cpu, command, file_name, group_id," \
                                       "id, mpi_rank, log_id)" \
                                       "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,NULL)"
    enum fields {
        B_SCOPE = 0,
        B_TYPE,
        B_TIMESTAMP,
        B_LABEL,
        B_RULE_ID,
        B_OBSERVED,
        B_HOST,
        B_PID,
        B_CPU,
        B_COMMAND,
        B_FILENAME,
        B_GID,
        B_JID,
        B_MPI_RANK,
        B_SIZE
    };
    MYSQL_STMT       *insert_log;
    MYSQL_BIND       input_bind[B_SIZE];
    unsigned long    str_length[B_SIZE];
    /* The insert statement is static apart from the table name, allocate a char array big enough
     * for both parts. As the format string contains %s it is two characters longer than needed once
     * we add the max table name length hence no need to add 1 for the trailing null
     */
    const int        log_str_len = sizeof(LOG_INSERT) + LOG_TABLE_NAME_SIZE;
    char             insert_log_str[log_str_len];
    char             timestamp[DATETIME_LENGTH];

    insert_log = mysql_stmt_init(con);
    if (!insert_log) {
        mistral_err("mysql_stmt_init() out of memory for insert_log\n");
        goto fail_insert_log_to_db;
    }

    /* Converts the timestamp to a formatted string */
    strftime(timestamp, sizeof(timestamp), "%F %H-%M-%S", &log_entry->time);

    /* Prepares the statement for use */
    snprintf(insert_log_str, log_str_len, LOG_INSERT, table_name);

    if (mysql_stmt_prepare(insert_log, insert_log_str, strlen(insert_log_str))) {
        mistral_err("mysql_stmt_prepare(insert_log) failed with statement: %s\n",
                insert_log_str);
        mistral_err("%s\n", mysql_stmt_error(insert_log));
        goto fail_insert_log_to_db;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));

    /* Set the variables to use for input parameters in the INSERT query */
    BIND_STRING(input_bind, B_SCOPE, mistral_scope_name[log_entry->scope], 0, str_length[B_SCOPE]);
    BIND_STRING(input_bind, B_TYPE, mistral_contract_name[log_entry->contract_type], 0, str_length[B_TYPE]);
    BIND_STRING(input_bind, B_TIMESTAMP, timestamp, 0, str_length[B_TIMESTAMP]);
    BIND_STRING(input_bind, B_LABEL, log_entry->label, 0, str_length[B_LABEL]);
    BIND_INT(input_bind, B_RULE_ID, &rule_id, 0);
    BIND_STRING(input_bind, B_OBSERVED, log_entry->measured_str, 0, str_length[B_OBSERVED]);
    BIND_STRING(input_bind, B_HOST, log_entry->hostname, 0, str_length[B_HOST]);
    BIND_INT(input_bind, B_PID, &log_entry->pid, 0);
    BIND_INT(input_bind, B_CPU, &log_entry->cpu, 0);
    BIND_STRING(input_bind, B_COMMAND, log_entry->command, 0, str_length[B_COMMAND]);
    BIND_STRING(input_bind, B_FILENAME, log_entry->file, 0, str_length[B_FILENAME]);
    BIND_STRING(input_bind, B_GID, log_entry->job_group_id, 0, str_length[B_GID]);
    BIND_STRING(input_bind, B_JID, log_entry->job_id, 0, str_length[B_JID]);
    BIND_INT(input_bind, B_MPI_RANK, &log_entry->mpi_rank, 0);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(insert_log, input_bind)) {
        mistral_err("mysql_stmt_bind_param(insert_log) failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_log));
        goto fail_insert_log_to_db;
    }

    /* Set the values of the variables used in the query */
    str_length[B_SCOPE] = strlen(mistral_scope_name[log_entry->scope]);
    str_length[B_TYPE] = strlen(mistral_contract_name[log_entry->contract_type]);
    str_length[B_TIMESTAMP] = strlen(timestamp);
    str_length[B_LABEL] = strlen(log_entry->label);
    str_length[B_OBSERVED] = strlen(log_entry->measured_str);
    str_length[B_HOST] = strlen(log_entry->hostname);
    str_length[B_COMMAND] = strlen(log_entry->command);
    str_length[B_FILENAME] = strlen(log_entry->file);
    str_length[B_GID] = strlen(log_entry->job_group_id);
    str_length[B_JID] = strlen(log_entry->job_id);

    /* Execute the query */
    if (mysql_stmt_execute(insert_log)) {
        mistral_err("mysql_stmt_execute(insert_log), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_log));
        goto fail_insert_log_to_db;
    }

    /* Get the total rows affected */
    my_ulonglong affected_rows = mysql_stmt_affected_rows(insert_log);
    if (affected_rows != 1) {
        mistral_err("Invalid number of rows inserted by insert_log. Expected 1, saw %d\n",
                    affected_rows);
        goto fail_insert_log_to_db;
    }

    /* Close the statement */
    if (mysql_stmt_close(insert_log)) {
        mistral_err("failed while closing the statement insert_rule\n");
        mistral_err("%s\n", mysql_stmt_error(insert_log));
        goto fail_insert_log_to_db;
    }

    return true;

fail_insert_log_to_db:
    mistral_err("Insert_log_to_db failed!\n");
    return false;
}

/*
 * write_log_to_db
 *
 * Function called once per log message to do all the processing required to
 * insert a log message into the appropriate log table.
 *
 * Parameters:
 *   log_entry   - A Mistral log record data structure containing the received
 *                 log information.
 *
 * Returns:
 *   true if the log record was inserted successfully
 *   false otherwise
 */

bool write_log_to_db(mistral_log *log_entry)
{
    static char last_log_date[DATE_LENGTH] = "";
    static char table_name[LOG_TABLE_NAME_SIZE];
    char log_date[DATE_LENGTH] = "";
    my_ulonglong rule_id = 0;
    size_t result = 0;

    /* Is the date on this record the same as the last record processed? */
    result = strftime(log_date, DATE_LENGTH, "%F", &log_entry->time);

    if (result > 0 && strncmp(log_date, last_log_date, DATE_LENGTH) != 0) {
        /* The date is different to the last log seen, update the last seen
         * value and look up the table name appropriate for this date.
         */
        strncpy(last_log_date, log_date, DATE_LENGTH);

        if (!get_log_table_name(log_entry, table_name)) {
            mistral_err("get_log_table_name failed\n");
            return false;
        }
    }

    /* Sets the rule id */
    if (! set_rule_id(log_entry, &rule_id)) {
        return false;
    }

    /* Inserts data into the log_XX table */
    if (!insert_log_to_db(table_name, log_entry, rule_id)) {
        return false;
    }

    return true;
}

/*
 * mistral_startup
 *
 * Required function that initialises the type of plug-in we are running. This
 * function is called immediately on plug-in start-up.
 *
 * In addition this plug-in needs to initialise the MySQL connection
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
        {"defaults-file", required_argument, NULL, 'c'},
        {"error", required_argument, NULL, 'o'},
        {"output", required_argument, NULL, 'o'},
        {0, 0, 0, 0},
    };

    const char *config_file = NULL;
    const char *error_file = NULL;
    int opt;

    while ((opt = getopt_long(argc, argv, "c:o:", options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
            break;
        case 'o':
            error_file = optarg;
            break;
        default:
            return;
        }
    }

    if (error_file != NULL) {
        log_file = fopen(error_file, "a");
        if (!log_file) {
            char buf[256];
            mistral_err("Could not open error file %s: %s\n", error_file,
                        strerror_r(errno, buf, sizeof buf));
        }
    }

    /* If we've opened an error log file use it in preference to stderr */
    if (log_file) {
        plugin->error_log = log_file;
    }

    if (config_file == NULL) {
        mistral_err("Missing option -c\n");
        return;
    }

    /* Initialize a MySQL object suitable for connection */
    con = mysql_init(NULL);

    if (con == NULL) {
        mistral_err("Unable to initialise MySQL: %s\n", mysql_error(con));
        return;
    }

    /* Get the config and credentials from file */
    int opt_ret = mysql_options(con, MYSQL_READ_DEFAULT_FILE, config_file);
    if (opt_ret) {
        mistral_err("Couldn't get MYSQL_READ_DEFAULT_FILE option: %s. File path %s %d\n",
                    mysql_error(con),  config_file, opt_ret);
        return;
    }

    /* Makes a connection to MySQl */
    if (mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0) == NULL) {
        mistral_err("Unable to connect to MySQL: %s\n", mysql_error(con));
        mysql_close(con);
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
 * found. Clean up any open error log and the MySQL connection.
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

    if (con) {
        mysql_close(con);
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

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        if (!write_log_to_db(log_entry)) {
            mistral_shutdown = true;
            return;
        }

        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
    }
    log_list_tail = NULL;

    return;
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
