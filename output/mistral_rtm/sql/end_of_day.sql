/* end_of_day.sql
 *
 * Eric Martin at Ellexus - 10/03/2016
 *
 * This text file should be imported using the command
 * "mysql --defaults-file=<path_to_password> -u mistral mistral_log < end_of_day.sql"
 * This will conduct the end of day procedures for mistral_mysql_plugin.
 *
 * This file assumes that create_multiple_tables.sql has been run sucessfully.
 */

USE mistral_log;
CALL end_of_day();
