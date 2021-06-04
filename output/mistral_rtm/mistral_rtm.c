#include <assert.h>             /* assert */
#include <ctype.h>              /* isdigit */
#include <errno.h>              /* errno */
#include <fcntl.h>              /* open */
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* uint32_t, uint64_t */
#include <mysql.h>              /* mysql_init, mysql_close, mysql_stmt_*, etc */
#include <search.h>             /* insque, remque */
#include <stdbool.h>            /* bool */
#include <stdio.h>              /* asprintf */
#include <stdlib.h>             /* calloc, free, getenv */
#include <string.h>             /* strerror_r, strdup, strchr */
#include <sys/stat.h>           /* open, umask */
#include <sys/types.h>          /* open, umask */
#include <unistd.h>             /* gethostname */

#include "mistral_plugin.h"

/* Define some database column sizes */
#define RATE_SIZE 64
#define STRING_SIZE 256
#define LONG_STRING_SIZE 1405
#define MEASUREMENT_SIZE 13

#define DATETIME_FORMAT "YYYY-MM-DD HH-mm-SS"
#define DATETIME_LENGTH sizeof(DATETIME_FORMAT)

#define BIND_STRING(b, i, str, null_is, str_len) \
    b[i].buffer_type = MYSQL_TYPE_STRING;        \
    b[i].buffer = (char *)str;                   \
    b[i].buffer_length = LONG_STRING_SIZE;       \
    b[i].is_null = null_is;                      \
    b[i].length = &str_len;

#define BIND_INT(b, i, integer, null_is) \
    b[i].buffer_type = MYSQL_TYPE_LONG;  \
    b[i].buffer = (char *)integer;       \
    b[i].is_null = null_is;              \
    b[i].length = 0;

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

static FILE *log_file = NULL;
static MYSQL *con = NULL;

static mistral_log *log_list_head = NULL;
static mistral_log *log_list_tail = NULL;
static void *rule_root = NULL;
/* Strictly speaking this should be HOST_NAME_MAX but we are not sure where this
 * will be run so may be different on the compilation box from the execution
 * target. Using 256 bytes should be as large as all current known possible
 * values according to the man pages so ought to be safe (at least for a while)
 */
static char hostname[STRING_SIZE] = "";
static char escaped_hostname[STRING_SIZE * 2 + 1] = "";
static char project[STRING_SIZE] = "";
static char escaped_project[STRING_SIZE * 2 + 1] = "";
static my_ulonglong submit_time = 0;
static uint64_t cluster_id;
static unsigned long debug_level = 0;

static char *log_insert = NULL;
static size_t log_insert_len = 0;

typedef struct rule_param {
    char label[STRING_SIZE + 1];
    char path[STRING_SIZE + 1];
    uint32_t call_types;
    enum mistral_measurement measurement;
    char size_range[RATE_SIZE + 1];
    my_ulonglong rule_id;
    char threshold[RATE_SIZE + 1];
    my_ulonglong cluster_id;
} rule_param;

static void usage(const char *name)
{
    /* This may be called before options have been processed so errors will go to stderr.
     * While this is designed to be run with Mistral to make the messages understandable
     * on a terminal add an explicit newline to each line. This will result is a second newline
     * in Mistral log messages and the log file.
     */
    mistral_err("Usage:\n");
    mistral_err("  %s -c config [-i id] [-o file] [-m octal-mode]\n", name);
    mistral_err("\n");
    mistral_err("  --defaults-file=config\n");
    mistral_err("  -c config\n");
    mistral_err("     Location of a MySQL formatted options file \"config\" that\n");
    mistral_err("     contains database connection configuration.\n");
    mistral_err("\n");
    mistral_err("  --debug=level\n");
    mistral_err("  -d level\n");
    mistral_err("     Output debug information. The value of level must be an integer\n");
    mistral_err("     between 1 and 4.\n");
    mistral_err("\n");
    mistral_err("  --cluster-id=id\n");
    mistral_err("  -i id\n");
    mistral_err("     Integer cluster ID. Defaults to 1 if not specified.\n");
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
static bool insert_rule_parameters(mistral_log *log_entry, my_ulonglong *ptr_rule_id)
{
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %p, %p", log_entry, ptr_rule_id);
    MYSQL_STMT   *insert_rule;
    MYSQL_BIND input_bind[7];
    unsigned long str_length_vio;
    unsigned long str_length_label;
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
    insert_rule_parameters_str = "INSERT INTO mistral_rule_parameters "
                                 "(rule_id, label, violation_path, call_type, "
                                 "measurement, size_range, threshold, "
                                 "clusterid) VALUES (NULL,?,?,?,?,?,?,?)";
    if (mysql_stmt_prepare(insert_rule, insert_rule_parameters_str,
                           strlen(insert_rule_parameters_str)))
    {
        mistral_err("mysql_stmt_prepare(insert_rule), failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_parameters;
    }

    /* Initialise the bind data structures */
    memset(input_bind, 0, sizeof(input_bind));

    /* Set the variables to use for input parameters in the INSERT query */
    BIND_STRING(input_bind, 0, log_entry->label, 0, str_length_label);
    BIND_STRING(input_bind, 1, log_entry->path, 0, str_length_vio);
    BIND_STRING(input_bind, 2, log_entry->call_type_names, 0, str_length_call);
    BIND_STRING(input_bind, 3, mistral_measurement_name[log_entry->measurement], 0,
                str_length_measure);
    BIND_STRING(input_bind, 4, log_entry->size_range, 0, str_length_size_range);
    BIND_STRING(input_bind, 5, log_entry->threshold_str, 0, str_length_threshold);
    BIND_INT(input_bind, 6, &cluster_id, 0);

    /* Connect the input variables to the prepared query */
    if (mysql_stmt_bind_param(insert_rule, input_bind)) {
        mistral_err("mysql_stmt_bind_param(insert_rule) failed\n");
        mistral_err("%s\n", mysql_stmt_error(insert_rule));
        goto fail_insert_rule_parameters;
    }

    /* Set the length of the values of the variables used in the query, these
     * will be truncated if they exceed the size of the database column.
     */
    str_length_label = strlen(log_entry->label);
    str_length_vio = strlen(log_entry->path);
    str_length_call = strlen(log_entry->call_type_names);
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
        mistral_err("Invalid number of rows inserted by insert_rule. Expected 1, saw %llu\n",
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

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
    return true;

fail_insert_rule_parameters:
    mistral_err("insert_rule_parameters failed\n");
    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
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
    DEBUG_OUTPUT(DBG_HIGH, "Entering function"); /* This is quite verbose so don't use DBG_ENTRY */
    const rule_param *rule1 = p;
    const rule_param *rule2 = q;
    int retval = 0;
    int64_t tmpint64 = 0;
    double tmpdouble = 0.0;

    retval = strcmp(rule1->label, rule2->label);
    if (retval) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
        return retval;
    }

    retval = strcmp(rule1->path, rule2->path);
    if (retval) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
        return retval;
    }

    /* Explicitly promote call_types from unsigned 32 bit to signed 64 bit */
    tmpint64 = (int64_t)rule1->call_types - (int64_t)rule2->call_types;
    if (tmpint64 < 0) {
        retval = -1;
    } else if (tmpint64 > 0) {
        retval = 1;
    }
    if (retval) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
        return retval;
    }

    retval = rule1->measurement - rule2->measurement;
    if (retval) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
        return retval;
    }

    retval = strcmp(rule1->size_range, rule2->size_range);
    if (retval) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
        return retval;
    }

    retval = strcmp(rule1->threshold, rule2->threshold);
    if (retval) {
        DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
        return retval;
    }

    /* Explicitly promote call_types from unsigned 64 bit to signed double */
    tmpdouble = (double)rule1->cluster_id - (double)rule2->cluster_id;
    if (tmpdouble < 0) {
        retval = -1;
    } else if (tmpdouble > 0) {
        retval = 1;
    }

    DEBUG_OUTPUT(DBG_HIGH, "Leaving function");
    return retval;
}

/*
 * set_rule_id
 *
 * This function checks to see if the violated rule details are already present
 * in the mistral_rule_parameters table and, if so, selects the related record
 * ID. If the record does not already exist insert_rule_parameters is called to
 * create it.
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %p, %p", log_entry, ptr_rule_id);
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT *get_rule_id;

    MYSQL_BIND input_bind[7];
    MYSQL_BIND output_bind[1];
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
        this_rule->cluster_id = cluster_id;

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
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
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
    char *get_rule_params_id_str = "SELECT rule_id FROM mistral_rule_parameters " \
                                   "WHERE label=? AND violation_path=? "          \
                                   "AND call_type=? AND measurement=? AND "       \
                                   "size_range=? AND threshold=? AND "            \
                                   "clusterid=?";
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
    BIND_STRING(input_bind, 2, log_entry->call_type_names, 0, str_length_call);
    BIND_STRING(input_bind, 3, mistral_measurement_name[log_entry->measurement], 0,
                str_length_measure);
    BIND_STRING(input_bind, 4, log_entry->size_range, 0, str_length_size_range);
    BIND_STRING(input_bind, 5, log_entry->threshold_str, 0, str_length_threshold);
    BIND_INT(input_bind, 6, &cluster_id, 0);

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
    str_length_call = strlen(log_entry->call_type_names);
    str_length_measure = strlen(mistral_measurement_name[log_entry->measurement]);
    str_length_size_range = strlen(log_entry->size_range);
    str_length_threshold = strlen(log_entry->threshold_str);

    /* Reset the lengths if they are larger than the column the string is being
     * compared to. Doing it this way round avoids calling strlen twice.
     */
    str_length_vio = (str_length_vio > STRING_SIZE) ? STRING_SIZE : str_length_vio;
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
        if (!insert_rule_parameters(log_entry, ptr_rule_id)) {
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

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
    return true;

fail_set_rule_id:
    mistral_err("Set_rule_ID failed!\n");
    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
    return false;
}

/*
 * parse_lsf_jobid
 *
 * Converts a Job ID string into two numbers, a job id  and an array index
 *
 *
 * Parameters:
 *   input_str   - A string containing the Job ID to parse in format
 *                 "job-id[array-idx]"
 *   job_id      - The address of a my_ulonglong variable used to hold the
 *                 parsed job ID
 *   array_idx   - The address of a unsigned long variable used to hold the
 *                 parsed job array index
 *
 * Returns:
 *   true if the string was parsed successfully
 *   false otherwise
 */
static bool parse_lsf_jobid(const char *input_str, my_ulonglong *job_id, unsigned long *array_idx)
{
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %s, %p, %p", input_str, job_id, array_idx);
    char *s = (char *)input_str;
    char *end = NULL;
    errno = 0;
    *job_id = (my_ulonglong)strtoull(s, &end, 10);
    if (errno || !end || job_id == 0 || s == end || (end && *end != '\0' && *end != '[')) {
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return false;
    } else if (end && *end == '\0') {
        *array_idx = 0;
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
        return true;
    }

    s = end + 1;
    *array_idx = strtoul(s, &end, 10);
    if (errno || !end || s == end || *end != ']') {
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return false;
    }

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
    return true;
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %p, %llu", log_entry, rule_id);
    /* Set up variables to hold the mysql escaped version of potentially
     * unsafe strings
     */
    char escaped_command[LONG_STRING_SIZE * 2 + 1];
    char escaped_filename[LONG_STRING_SIZE * 2 + 1];
    char escaped_fstype[STRING_SIZE * 2 + 1];
    char escaped_fsname[STRING_SIZE * 2 + 1];
    char escaped_fshost[STRING_SIZE * 2 + 1];
    char escaped_groupid[STRING_SIZE * 2 + 1];
    char escaped_id[STRING_SIZE * 2 + 1];
    char timestamp[DATETIME_LENGTH];
    char *values_string = NULL;

    mysql_real_escape_string(con, escaped_command, log_entry->command,
                             strlen(log_entry->command));
    mysql_real_escape_string(con, escaped_filename, log_entry->file,
                             strlen(log_entry->file));
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

    /* Set the appropriate observed unit */
    char *observed_unit;
    switch (mistral_unit_type[log_entry->measured_unit]) {
    case UNIT_CLASS_TIME:
        observed_unit = "Microseconds";
        break;
    case UNIT_CLASS_SIZE:
        observed_unit = "Bytes";
        break;
    default:
        observed_unit = "Count";
        break;
    }

    my_ulonglong temp_gid = 0;
    unsigned long temp_gid_array_idx = 0;
    if (strlen(log_entry->job_group_id) &&
        !parse_lsf_jobid(log_entry->job_group_id, &temp_gid, &temp_gid_array_idx))
    {
        mistral_err("build_values_string failed with job id: %s\n", log_entry->job_id);
        goto fail_build_values_string;
    }

    my_ulonglong temp_id = 0;
    unsigned long temp_array_idx = 0;
    if (strlen(log_entry->job_id) &&
        !parse_lsf_jobid(log_entry->job_id, &temp_id, &temp_array_idx))
    {
        mistral_err("build_values_string failed with job id: %s\n", log_entry->job_id);
        goto fail_build_values_string;
    }

    #define LOG_VALUES "('%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', "          \
                       " %llu, %" PRIu64 ", '%s', %" PRIu64 ", %" PRId64 ", '%s', " \
                       " '%s', '%s',"                                               \
                       " IF(%llu > 0, %llu, NULL),"                                 \
                       " IF(%lu > 0, %lu, NULL), '%s', IF(%llu > 0, %llu, NULL),"   \
                       " IF(%lu > 0, %lu, NULL), FROM_UNIXTIME(%llu), NULL, %"      \
                       PRIu64 ")"

    if (asprintf(&values_string,
                 LOG_VALUES,
                 mistral_scope_name[log_entry->scope],
                 mistral_contract_name[log_entry->contract_type],
                 timestamp,
                 escaped_hostname,
                 escaped_fstype,
                 escaped_fsname,
                 escaped_fshost,
                 escaped_project,
                 rule_id,
                 log_entry->measured,
                 observed_unit,
                 log_entry->measured_time,
                 log_entry->pid,
                 escaped_command,
                 escaped_filename,
                 escaped_groupid,
                 temp_gid, temp_gid,
                 temp_gid_array_idx, temp_gid_array_idx,
                 escaped_id,
                 temp_id, temp_id,
                 temp_array_idx, temp_array_idx,
                 submit_time,
                 cluster_id) < 0)
    {
        mistral_err("build_values_string failed to allocate memory in asprintf\n");
        goto fail_build_values_string;
    }

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
    return values_string;

fail_build_values_string:
    mistral_err("build_values_string failed!\n");
    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function");
    /* Execute the statement */
    if (mysql_real_query(con, log_insert, log_insert_len)) {
        mistral_err("Failed while inserting log entry\n");
        mistral_err("%s\n", mysql_error(con));
        goto fail_insert_log_to_db;
    }

    free(log_insert);
    log_insert = NULL;
    log_insert_len = 0;

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
    return true;

fail_insert_log_to_db:
    mistral_err("Insert_log_to_db failed!\n");
    DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed");
    return false;
}

/*
 * get_lsf_hostname
 *
 * Examine the environment to find the hostname as expected by LSF and stores
 * it in the global variable hostname
 *
 * Parameters:
 *   void
 *
 * Returns:
 *   void
 */
static void get_lsf_hostname(void)
{
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function");
    char env_hostname[STRING_SIZE];
    char *temp_hostname = getenv("HOSTNAME");
    if (temp_hostname) {
        strncpy(env_hostname, temp_hostname, STRING_SIZE - 1);
        env_hostname[STRING_SIZE - 1] = '\0';
    } else {
        env_hostname[0] = '\0';
    }

    char dns_hostname[STRING_SIZE];
    if (gethostname(dns_hostname, sizeof(dns_hostname)) == -1) {
        dns_hostname[0] = '\0';
    }

    char *lsb_hosts = NULL;
    char **host_arr = NULL;
    char *temp_lsb_hosts = getenv("LSB_MCPU_HOSTS");
    size_t num_hosts = 1;
    if (temp_lsb_hosts) {
        lsb_hosts = strdup(temp_lsb_hosts);
        if (lsb_hosts) {
            /* Convert this into an array of hostname strings - first count separators */
            char sep = ' ';
            for (size_t i = 0; lsb_hosts[i]; i++) {
                if (lsb_hosts[i] == sep) {
                    num_hosts++;
                }
            }
            /* If the number of hosts is odd then there is at least one repeated separator as each
             * host name is also followed by a count of CPUs which we will discard.
             */
            num_hosts = num_hosts / 2;
            if (num_hosts > 0) {
                host_arr = calloc(num_hosts, sizeof(char *));
            }
            if (host_arr) {
                char **p = host_arr;
                char *q = NULL;
                *p = strtok_r(lsb_hosts, " ", &q);
                p++;
                if (strtok_r(NULL, " ", &q)) {
                    while ((*p = strtok_r(NULL, " ", &q))) {
                        p++;
                        if (!strtok_r(NULL, " ", &q)) {
                            break;
                        }
                    }
                }
            }
        }
    }

    /* We now have all the information we need to detect the hostname as recorded in LSF
     *
     * Host name checks in order (move on if the test fails or results in an empty string):
     *  Use $HOSTNAME if it is in $LSB_MCPU_HOSTS exactly
     *  Remove any domain component from $HOSTNAME and use this if it is in $LSB_MCPU_HOSTS
     *  Repeat the tests above with hostname provided by gethostname() instead of $HOSTNAME
     *  Use the first node listed in LSB_MCPU_HOSTS
     *  **Print warning before progressing**
     *  Use $HOSTNAME without any domain component
     *  Use gethostname() without any domain component
     *  localhost
     */
    if (lsb_hosts && host_arr) {
        char *p = NULL;
        for (size_t i = 0; host_arr[i]; i++) {
            if (!strcmp(host_arr[i], env_hostname)) {
                strncpy(hostname, env_hostname, STRING_SIZE - 1);
                hostname[STRING_SIZE - 1] = '\0';
                break;
            }
        }

        if (hostname[0] == '\0') {
            p = strchr(env_hostname, '.');
            if (p) {
                *p = '\0';

                for (size_t i = 0; host_arr[i]; i++) {
                    if (!strcmp(host_arr[i], env_hostname)) {
                        strncpy(hostname, env_hostname, STRING_SIZE - 1);
                        hostname[STRING_SIZE - 1] = '\0';
                        break;
                    }
                }
            }
        }

        if (hostname[0] == '\0') {
            for (size_t i = 0; host_arr[i]; i++) {
                if (!strcmp(host_arr[i], dns_hostname)) {
                    strncpy(hostname, dns_hostname, STRING_SIZE - 1);
                    hostname[STRING_SIZE - 1] = '\0';
                    break;
                }
            }
        }

        if (hostname[0] == '\0') {
            p = strchr(dns_hostname, '.');
            if (p) {
                *p = '\0';

                for (size_t i = 0; host_arr[i]; i++) {
                    if (!strcmp(host_arr[i], dns_hostname)) {
                        strncpy(hostname, dns_hostname, STRING_SIZE - 1);
                        hostname[STRING_SIZE - 1] = '\0';
                        break;
                    }
                }
            }
        }

        if (hostname[0] == '\0' && host_arr[0]) {
            strncpy(hostname, host_arr[0], STRING_SIZE - 1);
            hostname[STRING_SIZE - 1] = '\0';
        }

        free(lsb_hosts);
        free(host_arr);
    }

    if (hostname[0] == '\0') {
        mistral_err(
            "Unable to find hostname in LSF environment. Attempting to use System environment\n");
        char *p = NULL;

        p = strchr(env_hostname, '.');
        if (p) {
            *p = '\0';
        }
        strncpy(hostname, env_hostname, STRING_SIZE - 1);
        hostname[STRING_SIZE - 1] = '\0';

        if (hostname[0] == '\0') {
            p = strchr(dns_hostname, '.');
            if (p) {
                *p = '\0';
            }
            strncpy(hostname, dns_hostname, STRING_SIZE - 1);
            hostname[STRING_SIZE - 1] = '\0';
        }

        if (hostname[0] == '\0') {
            strncpy(hostname, "localhost", STRING_SIZE - 1);
            hostname[STRING_SIZE - 1] = '\0';
        }
    }
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function %d, %p, %p", argc, argv, plugin);
    /* Returning without setting plug-in type will cause a clean exit */

    const struct option options[] = {
        {"cluster-id", required_argument, NULL, 'i'},
        {"defaults-file", required_argument, NULL, 'c'},
        {"debug", required_argument, NULL, 'd'},
        {"error", required_argument, NULL, 'o'},
        {"mode", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'o'},
        {0, 0, 0, 0},
    };

    const char *config_file = NULL;
    const char *error_file = NULL;
    int opt;
    mode_t new_mode = 0;

    while ((opt = getopt_long(argc, argv, "c:i:m:o:", options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
            break;
        case 'd': {
            char *end = NULL;
            unsigned long tmp_level = strtoul(optarg, &end, 10);
            if (tmp_level <= 0 || !end || *end || tmp_level > DBG_LIMIT) {
                mistral_err("Invalid debug level '%s', using '1'\n", optarg);
                tmp_level = 1;
            }
            /* For now just allow cumulative debug levels rather than selecting messages */
            debug_level = (2 << tmp_level) - 1;
            break;
        }
        case 'i': {
            char *end = NULL;
            unsigned long tmp_id = strtoul(optarg, &end, 10);
            if (tmp_id <= 0 || !end || *end) {
                mistral_err("Invalid cluster id specified '%s', using '1'\n", optarg);
                tmp_id = 1;
            }
            cluster_id = (uint64_t)tmp_id;
            break;
        }
        case 'm': {
            char *end = NULL;
            unsigned long tmp_mode = strtoul(optarg, &end, 8);
            if (tmp_mode <= 0 || !end || *end) {
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
        case 'o':
            error_file = optarg;
            break;
        default:
            usage(argv[0]);
            DEBUG_OUTPUT(DBG_HIGH, "Leaving function, failed");
            return;
        }
    }

    /* Error file is opened/created by the first error_msg
     */
    plugin->error_log = stderr;
    plugin->error_log_name = (char *)error_file;
    plugin->error_log_mode = new_mode;
    plugin->flags = 0;

    log_file = stderr;
 
    /* Get the current machine hostname as expected by LSF */
    get_lsf_hostname();

    /* Try and detect the job project from the environment */
    char *job_project = getenv("LSB_PROJECT_NAME");
    if (job_project) {
        strncpy(project, job_project, STRING_SIZE - 1);
        project[STRING_SIZE - 1] = '\0';
    } else {
        mistral_err("Unable to find job project\n");
        /* Do not treat this as fatal */
    }

    /* Try and detect the job submission time from the environment */
    char *job_filename = getenv("LSB_JOBFILENAME");
    if (job_filename) {
        char *filebase = basename(job_filename);
        if (filebase) {
            char *subtime_str = strdup(filebase);
            if (subtime_str) {
                char *p = subtime_str;
                while (isdigit(*p)) {
                    p++;
                }
                *p = '\0';
                errno = 0;
                submit_time = (my_ulonglong)strtoull(subtime_str, NULL, 10);
                free(subtime_str);
                if (errno) {
                    /* Just be absoultely sure that the time is set to 0 which is handled below */
                    submit_time = 0;
                }
            }
        }
    }

    if (submit_time == 0) {
        mistral_err("Unable to parse job submission time\n");
        /* Do not treat this as fatal */
    }

    if (config_file == NULL) {
        mistral_err("Missing option -c\n");
        usage(argv[0]);
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return;
    }

    /* Initialize a MySQL object suitable for connection */
    con = mysql_init(NULL);

    if (con == NULL) {
        mistral_err("Unable to initialise MySQL: %s\n", mysql_error(con));
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return;
    }

    /* Get the config and credentials from file */
    int opt_ret = mysql_options(con, MYSQL_READ_DEFAULT_FILE, config_file);
    if (opt_ret) {
        mistral_err("Couldn't get MYSQL_READ_DEFAULT_FILE option: %s. File path %s %d\n",
                    mysql_error(con),  config_file, opt_ret);
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return;
    }

    /* Now we have initialised the connection we can escape strings */
    mysql_real_escape_string(con, escaped_hostname, hostname, strlen(hostname));
    mysql_real_escape_string(con, escaped_project, project, strlen(project));

    /* Makes a connection to MySQl */
    if (mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0) == NULL) {
        mistral_err("Unable to connect to MySQL: %s\n", mysql_error(con));
        mysql_close(con);
        con = NULL;
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return;
    }

    /* Returning after this point indicates success */
    plugin->type = OUTPUT_PLUGIN;

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function");
    if (log_list_head) {
        DEBUG_OUTPUT(DBG_LOW, "Log entries existed at exit");
        mistral_received_data_end(0, false);
    }

    if (con) {
        mysql_close(con);
    }

    if (rule_root) {
        tdestroy(rule_root, free);
    }

    if (log_file && log_file != stderr) {
        DEBUG_OUTPUT(DBG_ENTRY, "Closing log file");
        fclose(log_file);
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function, %p", log_entry);
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entered function, %" PRIu64 ", %d", block_num, block_error);
    UNUSED(block_num);
    UNUSED(block_error);
    my_ulonglong rule_id = 0;

    mistral_log *log_entry = log_list_head;

    while (log_entry) {
        DEBUG_OUTPUT(DBG_MED, "Processing log entry, %p", log_entry);
        /* Get (or create) the appropriate rule id for this log entry */
        if (!set_rule_id(log_entry, &rule_id)) {
            mistral_shutdown();
            DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
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
        if (log_insert_len + values_len + 2 > 1000000) {
            DEBUG_OUTPUT(DBG_MED, "Buffer full, sending data to db, %zd, %zd", log_insert_len,
                         values_len);
            if (!insert_log_to_db()) {
                mistral_err("Insert log entry on max buffer size failed\n");
                mistral_shutdown();
                DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
                return;
            }
        }

        if (log_insert_len == 0) {
            DEBUG_OUTPUT(DBG_HIGH, "Buffer empty, creating new statement");
            /* Create the insert statement */
            if (asprintf(&log_insert,
                         "INSERT INTO mistral_events (scope, type, time,"      \
                         "host, project, rule_parameters, observed,"           \
                         "observed_unit, observed_time, pid, command,"         \
                         "file_name, groupid, group_jobid, group_indexid, id," \
                         "jobid, indexid, submit_time, log_id, clusterid) "    \
                         "VALUES %s", values) < 0)
            {
                mistral_err("Unable to allocate memory for log insert\n");
                mistral_shutdown();
                DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
                return;
            }
            log_insert_len = strlen(log_insert);
        } else {
            DEBUG_OUTPUT(DBG_HIGH, "Buffer exists, appending new row");
            /* Append values on the insert statement */
            char *old_log_insert = log_insert;
            if (asprintf(&log_insert, "%s,%s", log_insert, values) < 0) {
                mistral_err("Unable to allocate memory for log insert\n");
                mistral_shutdown();
                free(old_log_insert);
                DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
                return;
            }
            free(old_log_insert);
            log_insert_len += values_len + 1;
        }
        DEBUG_OUTPUT(DBG_HIGH, "Current query length %zd", log_insert_len);

        free(values);
        log_list_head = log_entry->forward;
        remque(log_entry);
        mistral_destroy_log_entry(log_entry);

        log_entry = log_list_head;
        DEBUG_OUTPUT(DBG_HIGH, "next log entry, %p", log_entry);
    }
    log_list_tail = NULL;

    DEBUG_OUTPUT(DBG_MED, "All logs processed, sending data to db, %p, %p, %zd", log_list_head,
                 log_list_tail, log_insert_len);
    DEBUG_OUTPUT(DBG_HIGH, "SQL %s", log_insert);
    /* Send any log entries to the database that are still pending */
    if (!insert_log_to_db()) {
        mistral_err("Insert log entry at end of block failed\n");
        mistral_shutdown();
        DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, failed");
        return;
    }

    DEBUG_OUTPUT(DBG_ENTRY, "Leaving function, success");
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
    DEBUG_OUTPUT(DBG_ENTRY, "Entering function");
    if (log_list_head) {
        DEBUG_OUTPUT(DBG_LOW, "Log entries existed when shutdown seen");
        mistral_received_data_end(0, false);
    }
}
