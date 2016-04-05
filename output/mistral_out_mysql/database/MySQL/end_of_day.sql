/* end_of_day.sql
 *
 * Eric Martin at Ellexus - 10/03/2016
 *
 * This text file should be imported using the command
 * "mysql --defaults-file=<path_to_password> -u mistral < end_of_day.sql"
 * This will conduct the end of day procedures for mistral_mysql_plugin.
 *
 * It should be set up as a CRON job to run at a quiet point during each day.
 *
 * This file assumes that create_multiple_tables.sql has been run sucessfully.
 */

USE multiple_mistral_log;
CALL end_of_day();
