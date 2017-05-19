#include <assert.h>             /* assert */
#include <errno.h>              /* errno */
#include <fcntl.h>              /* open */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <mysql.h>              /* mysql_init, mysql_close, mysql_stmt_*, etc */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, realloc, free */
#include <string.h>             /* strerror_r */
#include <sys/stat.h>           /* open, umask */
#include <sys/types.h>          /* open, umask */

#include "mistral_plugin.h"

/* Define some database column sizes */
#define RATE_SIZE 64
#define LOG_TABLE_NAME_SIZE 6
#define STRING_SIZE 256
#define LONG_STRING_SIZE 1405
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
static void *rule_root = NULL;

static char *log_insert = NULL;
static size_t log_insert_len = 0;

typedef struct rule_param {
    char label[STRING_SIZE + 1];
    char path[STRING_SIZE + 1];
    uint32_t call_types;
    enum mistral_measurement measurement;
    char size_range[RATE_SIZE + 1];
    char threshold[RATE_SIZE + 1];
    my_ulonglong rule_id;
} rule_param;

static void usage(const char *name)
{
    mistral_err("Usage:\n");
    mistral_err("  %s -c config [-i id] [-o file] [-m octal-mode]\n", name);
    mistral_err("\n");
    mistral_err("  --defaults-file=config\n");
    mistral_err("  -c config\n");
    mistral_err("     Location of a MySQL formatted options file \"config\" that\n");
    mistral_err("     contains database connection configuration.\n");
    mistral_err("\n");
    mistral_err("  --output=file\n");
    mistral_err("  -o file\n");
    mistral_err("     Specify location for error log. If not specified all errors will\n");
    mistral_err("     be output on stderr and handled by Mistral error logging.\n");
    mistral_err("\n");
    mistral_err("  --mode=octal-mode\n");
    mistral_err("  -m octal-mode\n");
    mistral_err("     Permissions used to create the error log file specified by the -o\n");
    mistral_err("     option.\n");
    mistral_err("\n");
    return;
}
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
    char *get_log_table_name_str = "SELECT CONCAT('log_', LPAD(table_num, 2 , '0')) FROM " \
                                   "date_table_map WHERE table_date = DATE_FORMAT(?,'%Y-%m-%d')";
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
        mistral_err("Expected 1 returned row but received %llu\n", received);
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
static bool insert_rule_details(mistral_log *log_entry, my_ulonglong *ptr_rule_id)
{
    MYSQL_STMT   *insert_rule;
    MYSQL_BIND    input_bind[6];
    unsigned long str_length_vio;
    unsigned long str_length_label;
    unsigned long str_length_call;
    unsigned long str_length_measure;
    unsigned long str_length_size_range;
    unsigned long str_length_threshold;
    char         *insert_rule_details_str;

    insert_rule = mysql_stmt_init(con);
    if (!insert_rule) {
        mistral_err("mysql_stmt_init() out of memory for insert_rule\n");
        goto fail_insert_rule_details;
    }

    /* Prepares the statement for use */
    insert_rule_details_str = "INSERT INTO rule_details"
                                 "(rule_id, label, violation_path, call_type,"
                                 "measurement, size_range, threshold)"
                                 "VALUES (NULL,?,?,?,?,?,?)";
    if (mysql_stmt_prepare(insert_rule, insert_rule_details_str,
                           strlen(insert_rule_details_str))) {
        mistral_err("mysql_stmt_prepare(insert_rule), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_details;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));

    /* Set the variables to use for input parameters in the INSERT query */
    BIND_STRING(input_bind, 0, log_entry->label, 0, str_length_label);
    BIND_STRING(input_bind, 1, log_entry->path, 0, str_length_vio);
    BIND_STRING(input_bind, 2, mistral_call_type_names[log_entry->call_type_mask], 0, str_length_call);
    BIND_STRING(input_bind, 3, mistral_measurement_name[log_entry->measurement], 0, str_length_measure);
    BIND_STRING(input_bind, 4, log_entry->size_range, 0, str_length_size_range);
    BIND_STRING(input_bind, 5, log_entry->threshold_str, 0, str_length_threshold);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(insert_rule, input_bind)) {
        mistral_err("mysql_stmt_bind_param(insert_rule) failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_details;
    }

    /* Set the length of the values of the variables used in the query, these will be truncated if
     * they exceed the size of the database column.
     */
    str_length_label = strlen(log_entry->label);
    str_length_vio = strlen(log_entry->path);
    str_length_call = strlen(mistral_call_type_names[log_entry->call_type_mask]);
    str_length_measure = strlen(mistral_measurement_name[log_entry->measurement]);
    str_length_size_range = strlen(log_entry->size_range);
    str_length_threshold = strlen(log_entry->threshold_str);

    /* Execute the query */
    if (mysql_stmt_execute(insert_rule)) {
        mistral_err("mysql_stmt_execute(insert_rule), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_details;
    }

    /* Get the total rows affected */
    my_ulonglong affected_rows = mysql_stmt_affected_rows(insert_rule);
    if (affected_rows != 1) {
        mistral_err("Invalid number of rows inserted by insert_rule. Expected 1, saw %llu\n",
                    affected_rows);
        goto fail_insert_rule_details;
    }

    /* Sets the rule_id to this new inserted value */
    assert(ptr_rule_id);
    *ptr_rule_id = mysql_stmt_insert_id(insert_rule);

    /* Close the statement */
    if (mysql_stmt_close(insert_rule)) {
        mistral_err("failed while closing the statement insert_rule\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_details;
    }

    return true;

fail_insert_rule_details:
    mistral_err("insert_rule_details failed\n");
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
bool set_rule_id(mistral_log *log_entry, my_ulonglong *ptr_rule_id)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT *get_rule_id;

    MYSQL_BIND    input_bind[6];
    MYSQL_BIND    output_bind[1];
    rule_param    *this_rule;
    void          *found;
    unsigned long str_length_vio;
    unsigned long str_length_label;
    unsigned long str_length_call;
    unsigned long str_length_measure;
    unsigned long str_length_size_range;
    unsigned long str_length_threshold;

    /* First let's check if we have seen the rule before */
    this_rule = calloc(1, sizeof(rule_param));
    if (this_rule) {
        strncpy(this_rule->label, log_entry->label, STRING_SIZE - 1);
        this_rule->label[STRING_SIZE - 1] = '\0';
        strncpy(this_rule->path, log_entry->path, STRING_SIZE - 1);
        this_rule->path[STRING_SIZE - 1] = '\0';
        this_rule->call_types = log_entry->call_type_mask;
        this_rule->measurement = log_entry->measurement;
        strncpy(this_rule->size_range, log_entry->size_range, RATE_SIZE - 1);
        this_rule->size_range[RATE_SIZE - 1] = '\0';
        strncpy(this_rule->threshold, log_entry->threshold_str, RATE_SIZE - 1);
        this_rule->threshold[RATE_SIZE - 1] = '\0';

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

    get_rule_id = mysql_stmt_init(con);
    if (!get_rule_id) {
        mistral_err("mysql_stmt_init() out of memory for get_rule_id\n");
        goto fail_set_rule_id;
    }

    /* Prepares the statement for use */
    char *get_rule_params_id_str = "SELECT rule_id FROM rule_details " \
                                   "WHERE label=? AND violation_path=? " \
                                   "AND call_type=? AND measurement=? AND " \
                                   "size_range=? AND threshold=?";
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
    BIND_STRING(input_bind, 0, log_entry->label, 0, str_length_label);
    BIND_STRING(input_bind, 1, log_entry->path, 0, str_length_vio);
    BIND_STRING(input_bind, 2, mistral_call_type_names[log_entry->call_type_mask], 0, str_length_call);
    BIND_STRING(input_bind, 3, mistral_measurement_name[log_entry->measurement], 0, str_length_measure);
    BIND_STRING(input_bind, 4, log_entry->size_range, 0, str_length_size_range);
    BIND_STRING(input_bind, 5, log_entry->threshold_str, 0, str_length_threshold);

    /* Set the variables to use to store the values returned by the SELECT query */
    BIND_INT(output_bind, 0, ptr_rule_id, 0);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(get_rule_id, input_bind)) {
        mistral_err("mysql_stmt_bind_param(get_rule_id) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_rule_id));
        goto fail_set_rule_id;
    }

    /* Set the length of the values of the variables used in the query */
    str_length_label = strlen(log_entry->label);
    str_length_vio = strlen(log_entry->path);
    str_length_call = strlen(mistral_call_type_names[log_entry->call_type_mask]);
    str_length_measure = strlen(mistral_measurement_name[log_entry->measurement]);
    str_length_size_range = strlen(log_entry->size_range);
    str_length_threshold = strlen(log_entry->threshold_str);

    /* Reset the lengths if they are larger than the column the string is being compared to. Doing
     * it this way round avoids calling strlen twice
     */
    str_length_label = (str_length_label > STRING_SIZE)? STRING_SIZE : str_length_vio;
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
        /* We found the rule in the DB, store this result in the tsearch tree */
        this_rule->rule_id = *ptr_rule_id;
    } else if (received == 0) {
        if (!insert_rule_details(log_entry, ptr_rule_id)) {
            tdelete((void *)this_rule, &rule_root, rule_compare);
            goto fail_set_rule_id;
        }
        /* Store the freshly inserted ID in the tsearch tree */
        this_rule->rule_id = *ptr_rule_id;
    } else {
        mistral_err("Expected 1 returned row but received %llu\n", received);
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
 * build_values_string
 *
 * Create a values string for use with the main log insert statement.
 *
 * The log insert values raw performance over absolute data integrity so uses
 * a custom insert query containing multiple insert VALUES strings which must
 * be built correctly in order to be safe to insert. We can insert as many rows
 * as we like up to the limit allowed by the communication buffer size (see
 * https://dev.mysql.com/doc/refman/5.7/en/c-api.html). We do the insert this
 * way rather than using bind variables as this would limit us to inserting a
 * single row at a time.
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
static char *build_values_string(mistral_log *log_entry, my_ulonglong rule_id)
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

    mysql_real_escape_string(con, escaped_command, log_entry->command,
                             strlen(log_entry->command));
    mysql_real_escape_string(con, escaped_filename, log_entry->file,
                             strlen(log_entry->file));
    mysql_real_escape_string(con, escaped_hostname, log_entry->hostname,
                             strlen(log_entry->hostname));
    mysql_real_escape_string(con, escaped_groupid, log_entry->job_group_id,
                             strlen(log_entry->job_group_id));
    mysql_real_escape_string(con, escaped_id, log_entry->job_id,
                             strlen(log_entry->job_id));
    /* Converts the timestamp to a formatted string */
    strftime(timestamp, sizeof(timestamp), "%F %H-%M-%S", &log_entry->time);

    #define LOG_VALUES "('%s', '%s', '%s', '%s', %llu, '%s', %" PRIu64 \
                       ", %" PRId32 ", '%s', '%s', '%s', '%s', %" PRId32 \
                       ", NULL)"

    if (asprintf(&values_string,
                 LOG_VALUES,
                 mistral_scope_name[log_entry->scope],
                 mistral_contract_name[log_entry->contract_type],
                 timestamp,
                 escaped_hostname,
                 rule_id,
                 log_entry->measured_str,
                 log_entry->pid,
                 log_entry->cpu,
                 escaped_command,
                 escaped_filename,
                 escaped_groupid,
                 escaped_id,
                 log_entry->mpi_rank) < 0) {
        mistral_err("build_values_string failed to allocate memory in asprintf\n");
        goto fail_build_values_string;
    }

    return values_string;

fail_build_values_string:
    mistral_err("build_values_string failed!\n");
    return NULL;
}

/*
 * insert_log_to_db
 *
 * Inserts the log entry to in the appropriate log_XX table.
 *
 * Performs the saved insert statement then frees the memory and resets the
 * related global variables.
 *
 * Parameters:
 *   void
 *
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
bool insert_log_to_db(void)
{
    /* Execute the statement */
    if (mysql_real_query(con, log_insert, log_insert_len)) {
        mistral_err("Failed while inserting log entry\n");
        mistral_err("%s\n", mysql_error(con));
        goto fail_insert_log_to_db;
    }

    free(log_insert);
    log_insert = NULL;
    log_insert_len = 0;

    return true;

fail_insert_log_to_db:
    mistral_err("Insert_log_to_db failed!\n");
    return false;
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
        {"mode", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'o'},
        {0, 0, 0, 0},
    };

    const char *config_file = NULL;
    const char *error_file = NULL;
    int opt;
    mode_t new_mode = 0;

    while ((opt = getopt_long(argc, argv, "c:do:", options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
            break;
        case 'm':{
            char *end = NULL;
            unsigned long tmp_mode = strtoul(optarg, &end, 8);
            if (tmp_mode <= 0 || !end || *end) {
                tmp_mode = 0;
            }
            new_mode = (mode_t)tmp_mode;

            if (new_mode <= 0 || new_mode > 0777)
            {
                mistral_err("Invalid mode '%s' specified, using default\n", optarg);
                new_mode = 0;
            }

            if ((new_mode & (S_IWUSR|S_IWGRP|S_IWOTH)) == 0)
            {
                mistral_err("Invalid mode '%s' specified, plug-in will not be able to write to log. Using default\n", optarg);
                new_mode = 0;
            }
            break;
        }
        case 'o':
            error_file = optarg;
            break;
        default:
            usage(argv[0]);
            return;
        }
    }

    if (error_file != NULL) {
        if (new_mode > 0) {
            mode_t old_mask = umask(00);
            int fd = open(error_file, O_CREAT | O_WRONLY | O_APPEND, new_mode);
            if (fd >= 0) {
                log_file = fdopen(fd, "a");
            }
            umask(old_mask);
        } else {
            log_file = fopen(error_file, "a");
        }

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
        usage(argv[0]);
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

    if (con) {
        mysql_close(con);
    }

    if (rule_root) {
        tdestroy(rule_root, free);
    }

    if (log_file && log_file != stderr) {
        fclose(log_file);
        log_file = stderr;
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
    my_ulonglong rule_id = 0;
    static char last_log_date[DATE_LENGTH] = "";
    static char table_name[LOG_TABLE_NAME_SIZE];

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        char log_date[DATE_LENGTH] = "";
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
                mistral_shutdown = true;
                return;
            }
        }

        /* Get (or create) the appropriate rule id for this log entry */
        if (! set_rule_id(log_entry, &rule_id)) {
            mistral_shutdown = true;
            return;
        }

        char *values = build_values_string(log_entry, rule_id);
        size_t values_len = strlen(values);

        /* The default maximum communication buffer size is 1MB (see
         * https://dev.mysql.com/doc/refman/5.7/en/c-api.html.
         * Let's assume that this has not been reduced. Check if this record
         * will fit within the buffer. If not we need to send the currently
         * built insert statement to MySQL and start a new statement.
         */
        if(log_insert_len + values_len + 2 > 1000000) {
            if(!insert_log_to_db()) {
                mistral_err("Insert log entry on max buffer size failed\n");
                mistral_shutdown = true;
                return;
            }
        }

        if (log_insert_len == 0) {
            /* Create the insert statement */
            if (asprintf(&log_insert,
                         "INSERT INTO %s (scope, type, time_stamp, host," \
                         "rule_id, observed, pid, cpu, command," \
                         "file_name, group_id, id, mpi_rank, log_id) "\
                         "VALUES %s", table_name, values) < 0) {
                mistral_err("Unable to allocate memory for log insert\n");
                mistral_shutdown = true;
                return;
            }
            log_insert_len = strlen(log_insert);
        } else {
            /* Append values on the insert statement */
            char *old_log_insert = log_insert;
            if (asprintf(&log_insert, "%s,%s", log_insert, values) < 0) {
                mistral_err("Unable to allocate memory for log insert\n");
                mistral_shutdown = true;
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
    if(!insert_log_to_db()) {
        mistral_err("Insert log entry at end of block failed\n");
        mistral_shutdown = true;
        return;
    }

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
