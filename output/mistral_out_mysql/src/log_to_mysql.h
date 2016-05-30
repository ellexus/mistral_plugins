#include <stdbool.h>

#ifndef LOG_SQL
#define LOG_SQL

typedef struct mistral_log_msg_s {
    char *timestamp;                            /* Time violation was logged. */
    char *label;                                /* Name of the contract rule. */
    char *path;                                 /* Information shared between rules. */
    char *measurement;
    char *call_type_set;
    char *observed;                             /* Measurement that exceeded the limit.*/
    char *limit;                                /* Maximum allowed measure per integration period. */
    char *pid;                                  /* Id of the process that violated the rule.*/
    char *command;                              /* Executable of the process that contributed mot in the violation. */
    char *file_name;                            /* Path to file that triggered the violation. */
    char *gid;                                  /* Id of the job group that violated the rule.*/
    char *jid;                                  /* Id of the job that violated the rule.*/

} mistral_log_msg_s, *mistral_log_msg_t;


typedef struct mistral_log_entry_s {
    char *scope;
    char *type;
    mistral_log_msg_t log_msg;

} mistral_log_entry_s, *mistral_log_entry_t;

/* Inserts rule param and set rule_id to this */
int insert_rule_parameters(mistral_log_entry_t log_entry, int *ptr_rule_id);

/* Sets the table_name variable from the control table. */
int get_log_table_name(mistral_log_entry_t log_entry, char *table_name);

/* Set the rule_id  */
int set_rule_id(mistral_log_entry_t log_entry, int *rule_id);

/* Inserts the log to log_X
 * Returns 0 on sucess
 */
int insert_log_to_db(char *table_name, mistral_log_entry_t log_entry,int rule_id);

/* write the whole log message to the db*/
extern bool write_log_to_db(mistral_log_entry_t log_entry);

/* connect to the db*/
extern bool connect_to_db();

/* disconnect from the db */
extern void disconnect_from_db();

#endif
