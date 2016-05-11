#include <mysql.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log_to_mysql.h"

#define STRING_SIZE 256

#define BIND_STRING(b, i, str, null_is, str_len)                     \
    b[i].buffer_type = MYSQL_TYPE_STRING;                            \
    b[i].buffer = (char *)str;                                       \
    b[i].buffer_length = STRING_SIZE;                                \
    b[i].is_null = null_is;                                          \
    b[i].length = &str_len;

#define BIND_INT(b, i, integer, null_is)                           \
    b[i].buffer_type = MYSQL_TYPE_LONG;                            \
    b[i].buffer = (char *)integer;                                 \
    b[i].is_null = null_is;                                        \
    b[i].length = 0;

enum fields {
    B_SCOPE = 0,
    B_TYPE,
    B_TIMESTAMP,
    B_LABEL,
    B_RULE_ID,
    B_OBSERVED,
    B_LIMIT,
    B_PID,
    B_COMMAND,
    B_FILENAME,
    B_GID,
    B_JID,
    B_EMPTY,
    B_SIZE
};

static MYSQL *con = NULL;

/* get_log_table_name()
 *
 * Gets the corresponding log message date and sets table_name to this
 *
 * Input :
 *   log_entry      - Structure containing output data from Mistral
 *
 *   result_str_data - This is set to the log table name corresponding to the
 *                   - date given in the log_entry
 *
 * Returns :
 *   0 on Success
 *   1 on Failure
 */
int get_log_table_name(mistral_log_entry_t log_entry, char *result_str_data)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    static MYSQL_STMT      *get_table_name;
    static MYSQL_BIND      bbind[1];
    static MYSQL_BIND      cbind[1];
    static unsigned long   str_length;
    static unsigned long   result_str_length;
    char                   str_data[STRING_SIZE];

    get_table_name = mysql_stmt_init(con);
    if (!get_table_name) {
        fprintf(stderr, " mysql_stmt_init(), out of memory\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Prepares the statement for use */
    char *get_log_table_name_str =
        "SELECT table_name FROM control_table WHERE table_date= DATE_FORMAT(?,'%Y-%m-%d')";
    if (mysql_stmt_prepare(get_table_name, get_log_table_name_str, strlen(get_log_table_name_str))) {
        fprintf(stderr, " mysql_stmt_prepare(), SELECT failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Binds the String Data */
    memset(bbind, 0, sizeof(bbind));
    memset(cbind, 0, sizeof(cbind));

    BIND_STRING( bbind, 0, str_data, 0, str_length);
    BIND_STRING( cbind, 0, result_str_data, 0, result_str_length);


    /* Bind the buffers */
    if (mysql_stmt_bind_param(get_table_name, bbind)) {
        fprintf(stderr, " mysql_stmt_bbind_param() failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Specifies the data */
    strncpy(str_data, log_entry->log_msg->timestamp, STRING_SIZE); /* string  */
    str_length= strlen(str_data);

    /* Executes the statement */
    if (mysql_stmt_execute(get_table_name)) {
        fprintf(stderr, " mysql_stmt_execute(), 1 failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Binds the results */
    if (mysql_stmt_bind_result(get_table_name, cbind)) {
        fprintf(stderr, " mysql_stmt_bbind_result(), failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(get_table_name));
    }

    mysql_stmt_store_result(get_table_name);
    int received = mysql_stmt_num_rows(get_table_name);

    if (received != 1) {
        fprintf(stderr, "Expected 1 returned row but received %d\n", received);
        goto fail_get_log_table_name;
    }

    /* Fetches the result */
    if (mysql_stmt_fetch(get_table_name)) {
        fprintf(stderr, " mysql_stmt_fetch(), failed\n");
        fprintf(stderr, "%s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Close the statement */
    if (mysql_stmt_close(get_table_name)) {
        fprintf(stderr, " failed while closing the statement\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(get_table_name));
        goto fail_get_log_table_name;
    }

    /* Frees the structures */
    return 0;

fail_get_log_table_name:
    fprintf(stderr, " get_log_table_name failed!\n");
    return 1;
}

/* set_rule_id()
 *
 * If combination of rule_parameters exists in the db, get the rule_id.
 * If not, insert the combination and return the rule_id.
 *
 * Input:
 *  log_entry       - Structure containing output data from Mistral
 *
 *  ptr_rule_id     - Pointer to the rule_id string which will be set to the
 *                    unique rule_id from the database
 *
 * Returns:
 *  0 on Success
 *  1 on Failure
 */
int set_rule_id(mistral_log_entry_t log_entry, int *ptr_rule_id)
{
    /* Allocates memory for a MYSQL_STMT and initializes it */
    MYSQL_STMT *stmt;

    static MYSQL_BIND dbind[3];
    static MYSQL_BIND ebind[1];
    static unsigned long str_length_vio;
    static unsigned long str_length_call;
    static unsigned long str_length_measure;
    static unsigned long result_str_length;
    static char          vio_path_str[STRING_SIZE];
    static char          call_type_str[STRING_SIZE];
    static char          measurement_str[STRING_SIZE];

    stmt = mysql_stmt_init(con);
    if (!stmt) {
        fprintf(stderr, " mysql_stmt_init(), out of memory\n");
        goto fail_set_rule_id;
    }

    /* Prepares the statement for use */
    const char *get_rule_params_id_str =
        "SELECT rule_id FROM rule_parameters WHERE violation_path=? AND call_type=? AND measurement=?";
    if (mysql_stmt_prepare(stmt, get_rule_params_id_str, strlen(get_rule_params_id_str))) {
        fprintf(stderr, " mysql_stmt_prepare(), SELECT failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
        goto fail_set_rule_id;
    }

    /* Binds the String Data */
    memset(dbind, 0, sizeof(dbind));
    memset(ebind, 0, sizeof(ebind));
    BIND_STRING( dbind, 0, vio_path_str, 0, str_length_vio);
    BIND_STRING( dbind, 1, call_type_str, 0, str_length_call);
    BIND_STRING( dbind, 2, measurement_str, 0, str_length_measure);
    BIND_INT( ebind, 0, ptr_rule_id, 0);

    /* Bind the buffers */
    if (mysql_stmt_bind_param(stmt, dbind)) {
        fprintf(stderr, " mysql_stmt_dbind_param() failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
        goto fail_set_rule_id;
    }

    /* Specifies the data */
    strncpy(vio_path_str, log_entry->log_msg->path, STRING_SIZE);
    strncpy(call_type_str, log_entry->log_msg->call_type_set, STRING_SIZE);
    strncpy(measurement_str, log_entry->log_msg->measurement, STRING_SIZE);
    str_length_vio = strlen(vio_path_str);
    str_length_call = strlen(call_type_str);
    str_length_measure = strlen(measurement_str);

    /* Executes the statement */
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, " mysql_stmt_execute(), failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
        goto fail_set_rule_id;
    }

    /* Binds the results */
    if (mysql_stmt_bind_result(stmt, ebind)) {
        fprintf(stderr, " mysql_stmt_ebind_result(), failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
        goto fail_set_rule_id;
    }

    /* Stores the result so we can do error checking */
    mysql_stmt_store_result(stmt);
    int received = mysql_stmt_num_rows(stmt);

    if (received == 1) {

        /* Fetches the result */
        if (mysql_stmt_fetch(stmt)) {
            fprintf(stderr, " mysql_stmt_fetch(), failed\n");
            fprintf(stderr, "%s\n", mysql_stmt_error(stmt));
            goto fail_set_rule_id;
        }
    } else if (received == 0) {
        if (insert_rule_parameters(log_entry, ptr_rule_id) != 0) {
            goto fail_set_rule_id;
        }
    } else {
        fprintf(stderr, "Expected 1 returned row but received %d\n", received);
        goto fail_set_rule_id;
    }

    /* Close the statement */
    if (mysql_stmt_close(stmt)) {
        fprintf(stderr, " failed while closing the statement\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
        goto fail_set_rule_id;
    }

    return 0;

fail_set_rule_id:
    fprintf(stderr, "Set_rule_ID failed!\n");
    return 1;
}

/* insert_rule_parameters()
 *
 * Inserts rule parameters into table 'rule_parameters' and sets rule_id
 *
 * Input:
 *  log_entry       - Structure containing output data from Mistral
 *
 *  ptr_rule_id     - Pointer to a string containing the rule_id
 *
 * Returns:
 *  0 on Success
 *  1 on Failure
 *
 */
int insert_rule_parameters(mistral_log_entry_t log_entry, int *ptr_rule_id)
{
    static                  MYSQL_STMT *insert_stmt;
    static                  MYSQL_BIND bbind[4];
    static unsigned long    str_length_empty;
    static unsigned long    str_length_vio;
    static unsigned long    str_length_call;
    static unsigned long    str_length_measure;
    static char             empty_field[STRING_SIZE];
    static char             vio_path_str[STRING_SIZE];
    static char             call_type_str[STRING_SIZE];
    static char             measurement_str[STRING_SIZE];
    char                    *insert_rule_parameters_str;

    insert_stmt = mysql_stmt_init(con);
    if (!insert_stmt) {
        fprintf(stderr, " mysql_stmt_init(), out of memory\n");
        goto fail_insert_rule_parameters;
    }

    /* Prepares the statement for use */
    insert_rule_parameters_str = "INSERT INTO rule_parameters VALUES(?,?,?,?)";
    if (mysql_stmt_prepare(insert_stmt, insert_rule_parameters_str,
                           strlen(insert_rule_parameters_str))) {
        fprintf(stderr, " mysql_stmt_prepare(), INSERT failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_stmt));
        goto fail_insert_rule_parameters;
    }

    /* Binds the String Date */
    memset(bbind, 0, sizeof(bbind));

    BIND_STRING( bbind, 0, empty_field, 0, str_length_empty);
    BIND_STRING( bbind, 1, vio_path_str, 0, str_length_vio);
    BIND_STRING( bbind, 2, call_type_str, 0, str_length_call);
    BIND_STRING( bbind, 3, measurement_str, 0, str_length_measure);

    /* Bind the buffers */
    if (mysql_stmt_bind_param(insert_stmt, bbind)) {
        fprintf(stderr, " mysql_stmt_bbind_param() failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_stmt));
        goto fail_insert_rule_parameters;
    }

    /* Specifies the data */
    strncpy(empty_field, "", STRING_SIZE);
    strncpy(vio_path_str, log_entry->log_msg->path, STRING_SIZE);
    strncpy(call_type_str, log_entry->log_msg->call_type_set, STRING_SIZE);
    strncpy(measurement_str, log_entry->log_msg->measurement, STRING_SIZE);
    str_length_empty = strlen(empty_field);
    str_length_vio = strlen(vio_path_str);
    str_length_call = strlen(call_type_str);
    str_length_measure = strlen(measurement_str);

    /* Executes the statement */
    if (mysql_stmt_execute(insert_stmt)) {
        fprintf(stderr, " mysql_stmt_execute(), failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_stmt));
        goto fail_insert_rule_parameters;
    }

    /* Get the total rows affected */
    int affected_rows = mysql_stmt_affected_rows(insert_stmt);
    if (affected_rows != 1) {
        fprintf(stderr, "Invalid affected rows by MySQL \n");
        goto fail_insert_rule_parameters;
    }

    /* Sets the rule_id to this new inserted value */
    *ptr_rule_id = mysql_stmt_insert_id(insert_stmt);

    /* Close the statement */
    if (mysql_stmt_close(insert_stmt)) {
        fprintf(stderr, " failed while closing the statement\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_stmt));
        goto fail_insert_rule_parameters;
    }

    return 0;

fail_insert_rule_parameters:
    fprintf(stderr, "insert_rule_parameters failed\n");
    return 1;
}

/* insert_rlog_to_db()
 *
 * Inserts the log entry to log_X
 *
 * Input:
 *  log_entry       - Structure containing output data from Mistral
 *
 *  rule_id         - Pointer to a string containing the rule_id
 *
 * Returns:
 *  0 on Success
 *  1 on Failure
 */
int insert_log_to_db(char *table_name, mistral_log_entry_t log_entry,int rule_id)
{
    static MYSQL_STMT       *insert_log_stmt;
    static MYSQL_BIND       bbind[B_SIZE];
    static unsigned long    str_length[B_SIZE];
    const static int        log_str_len = 55;           /* Fixed length of insert statement*/
    char                    insert_log_str[log_str_len];
    static struct           tm tm;
    static char             timestamp[100];
    static char             *empty_field = "";

    insert_log_stmt = mysql_stmt_init(con);
    if (!insert_log_stmt) {
        fprintf(stderr, " mysql_stmt_init(), out of memory\n");
        goto fail_insert_log_to_db;
    }

    /* Converts the timestamp to a formatted string */
    strptime(log_entry->log_msg->timestamp, "%Y-%m-%dT%H:%M:%S", &tm);
    strftime(timestamp, sizeof(timestamp), "%F %H-%M-%S", &tm);

    /* Prepares the statement for use */
    snprintf(insert_log_str, log_str_len, "INSERT INTO %s VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
             table_name);

    if (mysql_stmt_prepare(insert_log_stmt, insert_log_str, strlen(insert_log_str))) {
        fprintf(stderr, " mysql_stmt_prepare(), INSERT log to db failed with statement: %s\n",
                insert_log_str);
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_log_stmt));
        goto fail_insert_log_to_db;
    }

    /* Binds the String Date */
    memset(bbind, 0, sizeof(bbind));

    BIND_STRING( bbind, B_SCOPE , log_entry->scope , 0, str_length[B_SCOPE]);
    BIND_STRING( bbind, B_TYPE , log_entry->type , 0, str_length[B_TYPE]);
    BIND_STRING( bbind, B_TIMESTAMP , timestamp , 0, str_length[B_TIMESTAMP]);
    BIND_STRING( bbind, B_LABEL , log_entry->log_msg->label , 0, str_length[B_LABEL]);
    BIND_INT( bbind, B_RULE_ID, &rule_id, 0);
    BIND_STRING( bbind, B_OBSERVED , log_entry->log_msg->observed , 0, str_length[B_OBSERVED]);
    BIND_STRING( bbind, B_LIMIT , log_entry->log_msg->limit , 0, str_length[B_LIMIT]);
    BIND_INT( bbind, B_PID, &log_entry->log_msg->pid, 0);
    BIND_STRING( bbind, B_COMMAND , log_entry->log_msg->command , 0, str_length[B_COMMAND]);
    BIND_STRING( bbind, B_FILENAME , log_entry->log_msg->file_name , 0, str_length[B_FILENAME]);
    BIND_STRING( bbind, B_GID , log_entry->log_msg->gid , 0, str_length[B_GID]);
    BIND_STRING( bbind, B_JID , log_entry->log_msg->jid , 0, str_length[B_JID]);
    BIND_STRING( bbind, B_EMPTY , empty_field, 0, str_length[B_EMPTY]);

    /* Bind the buffers */
    if (mysql_stmt_bind_param(insert_log_stmt, bbind)) {
        fprintf(stderr, " mysql_stmt_bbind_param() failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_log_stmt));
        goto fail_insert_log_to_db;
    }

    /* Specifies the data */
    str_length[B_SCOPE] = strlen(log_entry->scope);
    str_length[B_TYPE] = strlen(log_entry->type);
    str_length[B_TIMESTAMP] = strlen(log_entry->log_msg->timestamp);
    str_length[B_LABEL] = strlen(log_entry->log_msg->label);
    str_length[B_OBSERVED] = strlen(log_entry->log_msg->observed);
    str_length[B_LIMIT] = strlen(log_entry->log_msg->limit);
    str_length[B_COMMAND] = strlen(log_entry->log_msg->command);
    str_length[B_FILENAME] = strlen(log_entry->log_msg->file_name);
    str_length[B_GID] = strlen(log_entry->log_msg->gid);
    str_length[B_JID] = strlen(log_entry->log_msg->jid);
    str_length[B_EMPTY] = strlen(empty_field);

    /* Executes the statement */
    if (mysql_stmt_execute(insert_log_stmt)) {
        fprintf(stderr, " mysql_stmt_execute(), 1 failed\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_log_stmt));
        goto fail_insert_log_to_db;
    }

    /* Get the total rows affected */
    int affected_rows = mysql_stmt_affected_rows(insert_log_stmt);
    if (affected_rows != 1) {
        fprintf(stderr, "Invalid affected rows by MySQL \n");
        goto fail_insert_log_to_db;
    }

    /* Close the statement */
    if (mysql_stmt_close(insert_log_stmt)) {
        fprintf(stderr, " failed while closing the statement\n");
        fprintf(stderr, " %s\n", mysql_stmt_error(insert_log_stmt));
        goto fail_insert_log_to_db;
    }

    return 0;

fail_insert_log_to_db:
    fprintf(stderr, "Insert_log_to_db failed!\n");
    return 1;
}


/* write_log_to_db()
 *
 * First function called.
 * This is the main controlling function. It receives log_entry and returns true on success.
 * It gets the table name from get_log_table_name
 * If the rule_params already exists, it finds and sets rule_id to this
 * If not, it adds to the table and sets the new rule_id
 * It then writes the log to the database
 *
 * Input:
 *  log_entry       - Structure containing output data from Mistral
 *
 * Returns:
 *  true on Success
 *  false on Failure
 *
 */

bool write_log_to_db(mistral_log_entry_t log_entry)
{

    char date_today[20];
    char table_name[6];
    int rule_id = -54;      /* magic number for error checking */

    /* Checks the log date is the same as today. If so, finds the table name matching this date. */
    if (strncmp(log_entry->log_msg->timestamp, date_today, 10) != 0) {
        strncpy(date_today, log_entry->log_msg->timestamp, 10);

        if (get_log_table_name(log_entry, table_name) != 0) {
            fprintf(stderr, "get_log_table_name failed\n");
            return false;
        }
    }

    /* Sets the rule id */
    set_rule_id(log_entry, &rule_id);
    if (rule_id < 1) {
        fprintf(stderr, "Setting rule_ID failed \n");
        return false;
    }

    /* Inserts data into the log_X table */
    if (insert_log_to_db(table_name, log_entry, rule_id) != 0) {
        fprintf(stderr, "Inserting failed \n");
        return false;
    }

    return true;
}

/* connect_to_db()
 *
 * Connects to a MySQL database defined by the config file
 *
 * Input:
 *  default_file_path          - string containing path to config file
 *                               containing log in details and IP for MySQL
 *                               database
 *
 * Returns:
 *  true on Success
 *  false on Failure
 */
bool connect_to_db(char *default_file_path)
{
    /* Initialize a MySQL object suitable for connection */
    con = mysql_init(NULL);

    /* Deals with being unable to connect */
    if (con == NULL) {
        fprintf(stderr, "%s\n", mysql_error(con));
        return false;
    }

    /* Get the config and credentials from file */
    int opt_ret = mysql_options(con, MYSQL_READ_DEFAULT_FILE, default_file_path);
    if (opt_ret) {
        fprintf(stderr, "Couldn't get MYSQL_READ_DEFAULT_FILE option: %s. File path %s %d\n",
                mysql_error(con),  default_file_path, opt_ret);
        return false;
    }

    /* Makes a connection to MySQl */
    if (mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0) == NULL) {
        fprintf(stderr, "%s\n",mysql_error(con));
        mysql_close(con);
        return false;
    }
    return true;
}

void disconnect_from_db()
{
    mysql_close(con);
}
