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
#include <uuid/uuid.h>          /* uuid_generate, uuid_parse */

#include "mistral_plugin.h"

/* Define some database column sizes */
#define RATE_SIZE 64
#define STRING_SIZE 256
#define LONG_STRING_SIZE 1405
#define MEASUREMENT_SIZE 13
#define UUID_SIZE 36
/* The default maximum MySQL communication buffer size is 1MB (see
 * https://dev.mysql.com/doc/refman/5.7/en/c-api.html.)
 */
#define BUFFER_SIZE 1000000

#define DATE_FORMAT "YYYY-MM-DD"
#define DATETIME_FORMAT "YYYY-MM-DD HH-mm-SS"
#define DATE_LENGTH sizeof(DATE_FORMAT)
#define DATETIME_LENGTH sizeof(DATETIME_FORMAT)
#define LOG_TABLE_FMT "log_%02d"
#define LOG_TABLE_SIZE sizeof("log_nn")
#define ENV_TABLE_FMT "env_%02d"
#define ENV_TABLE_SIZE sizeof("env_nn")

#define VALID_NAME_CHARS "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_"

#define BIND_STRING(b, i, str, null_is, str_len) \
    b[i].buffer_type = MYSQL_TYPE_STRING;        \
    b[i].buffer = (char *)str;                   \
    b[i].buffer_length = STRING_SIZE;            \
    b[i].is_null = null_is;                      \
    b[i].length = &str_len;

#define BIND_INT(b, i, integer, null_is) \
    b[i].buffer_type = MYSQL_TYPE_LONG;  \
    b[i].buffer = (char *)integer;       \
    b[i].is_null = null_is;              \
    b[i].length = 0;

static char run_id[UUID_SIZE + 1];

static FILE **log_file_ptr = NULL;
static MYSQL *con = NULL;

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
    my_ulonglong rule_id;
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
                "     Location of a MySQL formatted options file \"config\" that\n"
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

/*
 * get_table_number
 *
 * This function retrieves the number suffix used for the log and environment
 * variable tables for this date.
 *
 * Parameters:
 *   log_entry      - A Mistral log record data structure containing the
 *                    received log information.
 *
 * Returns:
 *   Integer value of table suffix if successful
 *   -1 otherwise
 */
static bool get_table_number(const mistral_log *log_entry)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT      *get_table_num;
    MYSQL_BIND input_bind[1];
    MYSQL_BIND output_bind[1];
    unsigned long str_length;
    char log_date[STRING_SIZE];
    int table_num;

    get_table_num = mysql_stmt_init(con);
    if (!get_table_num) {
        mistral_err("mysql_stmt_init() out of memory for get_table_num\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
        goto fail_get_table_number;
    }

    /* Prepares the statement for use */
    char *get_table_number_str = "SELECT table_num FROM date_table_map WHERE " \
                                 "table_date = DATE_FORMAT(?,'%Y-%m-%d')";
    if (mysql_stmt_prepare(get_table_num, get_table_number_str,
                           strlen(get_table_number_str)))
    {
        mistral_err("mysql_stmt_prepare(get_table_num) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
        goto fail_get_table_number;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));
    memset(output_bind, 0, sizeof(output_bind));

    /* Set the variables to use for input parameters in the SELECT query */
    BIND_STRING(input_bind, 0, log_date, 0, str_length);

    /* Set the variables to use to store the values returned by the SELECT query */
    BIND_INT(output_bind, 0, &table_num, 0);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(get_table_num, input_bind)) {
        mistral_err("mysql_stmt_bind_param(get_table_num) failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
        goto fail_get_table_number;
    }

    /* Set the date to look up */
    strftime(log_date, sizeof(log_date), "%F", &log_entry->time);
    str_length = strlen(log_date);

    /* Execute the query */
    if (mysql_stmt_execute(get_table_num)) {
        mistral_err("mysql_stmt_execute(get_table_num), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
        goto fail_get_table_number;
    }

    /* Connect the output variables to the results of the query */
    if (mysql_stmt_bind_result(get_table_num, output_bind)) {
        mistral_err("mysql_stmt_bind_result(get_table_num), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
    }

    /* Get all returned rows locally */
    mysql_stmt_store_result(get_table_num);
    my_ulonglong received = mysql_stmt_num_rows(get_table_num);

    if (received != 1) {
        mistral_err("Expected 1 returned row but received %llu\n", received);
        goto fail_get_table_number;
    }

    /* Populate the output variables with the returned data */
    if (mysql_stmt_fetch(get_table_num)) {
        mistral_err("mysql_stmt_fetch(get_table_num), failed\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
        goto fail_get_table_number;
    }

    /* Close the statement */
    if (mysql_stmt_close(get_table_num)) {
        mistral_err("Failed while closing the statement get_table_num\n");
        mistral_err("%s\n", mysql_stmt_error(get_table_num));
        goto fail_get_table_number;
    }

    return table_num;

fail_get_table_number:
    mistral_err("get_table_number failed!\n");
    return -1;
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
    MYSQL_BIND input_bind[6];
    unsigned long str_length_path;
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
                              "(rule_id, label, violation_path, call_type, "
                              "measurement, size_range, threshold)"
                              "VALUES (NULL,?,?,?,?,?,?)";
    if (mysql_stmt_prepare(insert_rule, insert_rule_details_str,
                           strlen(insert_rule_details_str)))
    {
        mistral_err("mysql_stmt_prepare(insert_rule), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_details;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));

    /* Set the variables to use for input parameters in the INSERT query */
    BIND_STRING(input_bind, 0, log_entry->label, 0, str_length_label);
    BIND_STRING(input_bind, 1, log_entry->path, 0, str_length_path);
    BIND_STRING(input_bind, 2, log_entry->call_type_names, 0, str_length_call);
    BIND_STRING(input_bind, 3, mistral_measurement_name[log_entry->measurement], 0,
                str_length_measure);
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
    str_length_path = strlen(log_entry->path);
    str_length_call = strlen(log_entry->call_type_names);
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
 * env_table_compare
 *
 * Comparison function for use with tsearch to compare two environment tables.
 *
 * Parameters:
 *   p  - pointer to the first env_table structure
 *   q  - pointer to the second env_table structure
 *
 * Returns:
 *   < 0 if first table is "less than" the second table
 *   0   if first table is "equal to" the second table
 *   > 0 if first table is "greater than" the second table
 */
static int env_table_compare(const void *p, const void *q)
{
    const env_table *tab1 = p;
    const env_table *tab2 = q;
    int retval = 0;

    retval = tab1->table_number - tab2->table_number;
    if (retval) {
        return retval;
    }

    retval = strcmp(tab1->date, tab2->date);
    return retval;
}

/*
 * insert_env_records
 *
 * This function checks to see if any additional environment varaibles that were
 * specified at the command line have been stored for this job in the
 * appropriate 'env_nn' table for the log record date.
 *
 * We do not expect a large number of these environment variables to be
 * specified and, even so, will only need to do this once for each date seen so
 * there is no need to construct a large bulk insert.
 *
 * Parameters:
 *   table_number - The integer table number suffix to use when building the
 *                  table name.
 *   table_date   - The date associated with this table as, for long runing
 *                  jobs, it is possible a table may get reused (although this
 *                  is highly unlikely).
 *
 * Returns:
 *   true if environment variables are saved successfully
 *   false otherwise
 */
static bool insert_env_records(int table_number, char *table_date)
{
    /* Do we have any environment variables to store? */
    if (!env_head) {
        /* Nothing to do */
        return true;
    }

    /* First let's check if we have already stored the environment variables in
     * the specified table.
     */
    env_table *this_table;
    void *found;

    this_table = calloc(1, sizeof(env_table));
    if (this_table) {
        this_table->table_number = table_number;
        strncpy(this_table->date, table_date, DATE_LENGTH - 1);
        this_table->date[DATE_LENGTH - 1] = '\0';

        found = tsearch((void *)this_table, &table_root, env_table_compare);
        if (found == NULL) {
            mistral_err("Out of memory in tsearch - env var check\n");
            free(this_table);
            this_table = NULL;
            goto fail_insert_env_records;
        } else if (*(env_table **)found != this_table) {
            /* Already saved details in this table */
            free(this_table);
            this_table = NULL;
            return true;
        }
    } else {
        mistral_err("Unable to allocate memory for table to be used in tsearch\n");
        goto fail_insert_env_records;
    }
    /* If we have got here this is the first time we have seen this table -
     * insert all the specified environment variables into it. Insert each
     * variable one at a time as we are not expecting to see a large number of
     * --var options on the command line and we only need to do this once a day
     */
    char env_table_name[ENV_TABLE_SIZE];
    if (snprintf(env_table_name, ENV_TABLE_SIZE, ENV_TABLE_FMT, table_number) >=
        (int)ENV_TABLE_SIZE)
    {
        mistral_err("Unable to build environment table name\n");
        goto fail_insert_env_records;
    }

    env_var *variable = env_head;
    while (variable) {
        #define ENV_INSERT "INSERT INTO %s (plugin_run_id, env_name," \
                           "env_value, env_id) VALUES (?,?,?,NULL)"
        MYSQL_STMT *insert_env;
        MYSQL_BIND input_bind[3];
        unsigned long str_length_run_id;
        unsigned long str_length_name;
        unsigned long str_length_value;
        /* The insert statement is static apart from the table name,
         * allocate a char array big enough for both parts. As the
         * format string contains %s it is two characters longer than
         * needed once we add the max table name length hence no need to
         * add 1 for the trailing null
         */
        const int env_str_len = sizeof(ENV_INSERT) + ENV_TABLE_SIZE;
        char insert_env_str[env_str_len];

        insert_env = mysql_stmt_init(con);
        if (!insert_env) {
            mistral_err("mysql_stmt_init() out of memory for insert_log\n");
            goto fail_insert_env_records;
        }

        /* Prepares the statement for use */
        snprintf(insert_env_str, env_str_len, ENV_INSERT, env_table_name);

        if (mysql_stmt_prepare(insert_env, insert_env_str, strlen(insert_env_str))) {
            mistral_err("mysql_stmt_prepare(insert_env) failed with statement: %s\n",
                        insert_env_str);
            mistral_err("%s\n", mysql_stmt_error(insert_env));
            goto fail_insert_env_records;
        }

        /* Initialise the bind data structures */
        memset(input_bind, 0, sizeof(input_bind));

        BIND_STRING(input_bind, 0, run_id, 0, str_length_run_id);
        BIND_STRING(input_bind, 1, variable->name, 0, str_length_name);
        BIND_STRING(input_bind, 2, variable->value, 0, str_length_value);

        /* Connect the input variables to the prepared query */
        if (mysql_stmt_bind_param(insert_env, input_bind)) {
            mistral_err("mysql_stmt_bind_param(insert_env) failed\n");
            mistral_err("%s\n", mysql_stmt_error(insert_env));
            goto fail_insert_env_records;
        }

        /* Set the appropriate string lengths (these have already been
         * restricted to the max length for the appropriate column).
         */
        str_length_run_id = strlen(run_id);
        str_length_name = strlen(variable->name);
        str_length_value = strlen(variable->value);

        /* Execute the query */
        if (mysql_stmt_execute(insert_env)) {
            mistral_err("mysql_stmt_execute(insert_env), failed\n");
            mistral_err("%s\n", mysql_stmt_error(insert_env));
            goto fail_insert_env_records;
        }

        /* Get the total rows affected */
        my_ulonglong affected_rows = mysql_stmt_affected_rows(insert_env);
        if (affected_rows != 1) {
            mistral_err("Invalid number of rows inserted by insert_env. Expected 1, saw %llu\n",
                        affected_rows);
            goto fail_insert_env_records;
        }

        /* Close the statement */
        if (mysql_stmt_close(insert_env)) {
            mistral_err("failed while closing the statement insert_env\n");
            mistral_err("%s\n", mysql_stmt_error(insert_env));
            goto fail_insert_env_records;
        }

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
static bool set_rule_id(mistral_log *log_entry, my_ulonglong *ptr_rule_id)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT *get_rule_id;

    MYSQL_BIND input_bind[6];
    MYSQL_BIND output_bind[1];
    rule_param    *this_rule;
    void          *found;
    unsigned long str_length_path;
    unsigned long str_length_label;
    unsigned long str_length_call;
    unsigned long str_length_measure;
    unsigned long str_length_size_range;
    unsigned long str_length_threshold;

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

    get_rule_id = mysql_stmt_init(con);
    if (!get_rule_id) {
        mistral_err("mysql_stmt_init() out of memory for get_rule_id\n");
        goto fail_set_rule_id;
    }

    /* Prepares the statement for use */
    char *get_rule_params_id_str = "SELECT rule_id FROM rule_details "      \
                                   "WHERE label=? AND violation_path=? "    \
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
    BIND_STRING(input_bind, 1, log_entry->path, 0, str_length_path);
    BIND_STRING(input_bind, 2, log_entry->call_type_names, 0, str_length_call);
    BIND_STRING(input_bind, 3, mistral_measurement_name[log_entry->measurement], 0,
                str_length_measure);
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
    str_length_path = strlen(log_entry->path);
    str_length_call = strlen(log_entry->call_type_names);
    str_length_measure = strlen(mistral_measurement_name[log_entry->measurement]);
    str_length_size_range = strlen(log_entry->size_range);
    str_length_threshold = strlen(log_entry->threshold_str);

    /* Reset the lengths if they are larger than the column the string is being compared to. Doing
     * it this way round avoids calling strlen twice
     */
    str_length_label = (str_length_label > STRING_SIZE) ? STRING_SIZE : str_length_label;
    str_length_path = (str_length_path > STRING_SIZE) ? STRING_SIZE : str_length_path;
    str_length_call = (str_length_call > STRING_SIZE) ? STRING_SIZE : str_length_call;
    str_length_measure = (str_length_measure > MEASUREMENT_SIZE) ? MEASUREMENT_SIZE :
                                                                   str_length_measure;
    str_length_size_range = (str_length_size_range > RATE_SIZE) ? RATE_SIZE : str_length_size_range;
    str_length_threshold = (str_length_threshold > RATE_SIZE) ? RATE_SIZE : str_length_threshold;

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
static char *build_log_values_string(mistral_log *log_entry, my_ulonglong rule_id)
{
    /* Set up variables to hold the mysql escaped version of potentially
     * unsafe strings
     */
    char escaped_command[LONG_STRING_SIZE * 2 + 1];
    char escaped_filename[LONG_STRING_SIZE * 2 + 1];
    char escaped_hostname[STRING_SIZE * 2 + 1] = "";
    char escaped_fstype[STRING_SIZE * 2 + 1] = "";
    char escaped_fsname[STRING_SIZE * 2 + 1] = "";
    char escaped_fshost[STRING_SIZE * 2 + 1] = "";
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
    mysql_real_escape_string(con, escaped_fstype, log_entry->fstype,
                             strlen(log_entry->fstype));
    mysql_real_escape_string(con, escaped_fsname, log_entry->fsname,
                             strlen(log_entry->fsname));
    mysql_real_escape_string(con, escaped_fshost, log_entry->fshost,
                             strlen(log_entry->fshost));
    mysql_real_escape_string(con, escaped_groupid, log_entry->job_group_id,
                             strlen(log_entry->job_group_id));
    mysql_real_escape_string(con, escaped_id, log_entry->job_id,
                             strlen(log_entry->job_id));
    /* Converts the timestamp to a formatted string */
    strftime(timestamp, sizeof(timestamp), "%F %H-%M-%S", &log_entry->time);

    #define LOG_VALUES "('%s', '%s', '%s.%06" PRIu32 "', '%s', '%s', '%s', '%s', %llu, "  \
                       "'%s', %" PRIu64 ", %" PRId32 ", '%s', '%s', '%s', '%s', %" PRId32 \
                       ",'%s', NULL)"

    if (asprintf(&values_string,
                 LOG_VALUES,
                 mistral_scope_name[log_entry->scope],
                 mistral_contract_name[log_entry->contract_type],
                 timestamp,
                 log_entry->microseconds,
                 escaped_hostname,
                 escaped_fstype,
                 escaped_fsname,
                 escaped_fshost,
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
    if (mysql_real_query(con, log_insert, log_insert_len)) {
        mistral_err("Failed while inserting log entry\n");
        mistral_err("%s\n", mysql_error(con));
        mistral_err("Insert_log_to_db failed!\n");
        return false;
    }

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
        {"error", required_argument, NULL, 'e'},
        {"mode", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'e'},
        {"var", required_argument, NULL, 'v'},
        {0, 0, 0, 0},
    };

    const char *config_file = NULL;
    const char *error_file = NULL;
    int opt;
    mode_t new_mode = 0;

    while ((opt = getopt_long(argc, argv, "c:m:e:v:", options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
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

    /* Error file is opened/created by the first error_msg
     */
    plugin->error_log = stderr;
    plugin->error_log_name = (char *)error_file;
    plugin->error_log_mode = new_mode;
    plugin->flags = 0;

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
        con = NULL;
        return;
    }

    /* Generate a unique run_id to link records in the database */
    uuid_t urun_id;
    uuid_generate(urun_id);
    uuid_unparse(urun_id, run_id);

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
        mysql_close(con);
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
    my_ulonglong rule_id = 0;
    static char last_log_date[DATE_LENGTH] = "";
    static char table_name[LOG_TABLE_SIZE];
    bool date_changed = false;

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        char log_date[DATE_LENGTH] = "";
        size_t result = 0;

        /* Is the date on this record the same as the last record processed? */
        result = strftime(log_date, DATE_LENGTH, "%F", &log_entry->time);

        if (result > 0 && strncmp(log_date, last_log_date, DATE_LENGTH) != 0) {
            /* The date is different to the last log seen, update the last seen
             * value and look up the table name appropriate for this date.
             *
             * There is an assumption here that Mistral is running with live
             * data on machines with correctly configured clocks and that the
             * end_of_day processing has successfully run. This should mean that
             * we are only seeing data from "today" and when the date rolls over
             * at midnight we already have an empty table prepared for data.
             *
             * Processing saved data / clock errors will still not be an issue
             * as long as the date on the records falls between -30 days (by
             * default) and 23:59:59 on the current date as configured on the
             * MySQL server. Data one day either side of this range may still be
             * inserted successfully depending on whether the end_of_day
             * processing has been completed or not.
             */
            strncpy(last_log_date, log_date, DATE_LENGTH);

            int num = get_table_number(log_entry);
            if (num < 0) {
                mistral_err("get_table_number failed\n");
                mistral_shutdown();
                return;
            }
            date_changed = true;

            if (snprintf(table_name, LOG_TABLE_SIZE, LOG_TABLE_FMT, num) >=
                (int)LOG_TABLE_SIZE)
            {
                mistral_err("Unable to build log table name\n");
                mistral_shutdown();
                return;
            }
            /* Check if we have already stored our environment variables in the
             * corresponding table, if not insert them.
             */
            if (!insert_env_records(num, log_date)) {
                mistral_shutdown();
                return;
            }
        }

        /* Get (or create) the appropriate rule id for this log entry */
        if (!set_rule_id(log_entry, &rule_id)) {
            mistral_shutdown();
            return;
        }

        char *values = build_log_values_string(log_entry, rule_id);
        size_t values_len = strlen(values);

        /* Check if this record will fit within the MySQL communication buffer.
         * If not we need to send the currently built insert statement to MySQL
         * and start a new statement.
         *
         * We also need to send the current query if the date has changed as
         * this will change the table we are inserting into.
         */
        if (log_insert_len + values_len + 2 > BUFFER_SIZE ||
            (log_insert_len && date_changed))
        {
            if (!insert_log_to_db()) {
                if (date_changed) {
                    mistral_err("Insert log entry on date change failed\n");
                } else {
                    mistral_err("Insert log entry on max buffer size failed\n");
                }
                mistral_shutdown();
                return;
            }
            date_changed = false;
        }

        if (log_insert_len == 0) {
            /* Create the insert statement */
            if (asprintf(&log_insert,
                         "INSERT INTO %s (scope, type, time_stamp, host, "     \
                         "fstype, fsname, fshost, rule_id, observed, pid, "    \
                         "cpu, command, rule_id, observed, pid, cpu, command," \
                         "file_name, group_id, id, mpi_rank, plugin_run_id, "  \
                         "log_id) VALUES %s", table_name, values) < 0)
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
