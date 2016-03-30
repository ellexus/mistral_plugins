This is the readme for using the Mistral mysql plugin.

This plugin takes the data given by mistral and writes it to a MySQL database called multiple_mistral_log. The database contains 34 tables and operates on a 32 day rotating log system. At each new day, the CRON job updates the control_table and truncates the oldest log.
It also creates a new user in the database 'mistral'@'%' identified by 'mistral' with ALL permissions on multiple_mistral_log.*

INSTRUCTIONS FOR USE
Run " mysql -u root -p < create_multiple_tables.sql "
Set up a CRON job to run "mysql --defaults-file=<path-to-password-file> -u mistral < end_of_day" at the end of each day.


