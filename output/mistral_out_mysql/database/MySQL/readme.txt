Mistral MySQL Plugin
====================

This is the readme for using the Mistral mysql plugin.

Intro
-----
This plugin receives violation data from Mistral and enters it into a MySQL database named
"multiple_mistral_log". MySQL must be installed on every machine that is involved.

Process Summary
---------------
The initiation scripts create a database called "multiple_mistral_log". Within it are 34 tables:
32 log tables; a control table; and a rule_parameters table. A user "'mistral'@'%'" is created and given [SELECT, EXECUTE, INSERT] permission on "multiple_mistral_log".

The plugin uses a 32 day rotating log system to store Mistral data. The data is the same as would
be outputted from Mistral to a log file. Each field is stored in a separate column with the
exception of those stored in the "rule_parameters" (see below). At some point during each day,
the "end_of_day" script must be run. This script recycles the two oldest log tables in order to
create log tables for that day and the next. This is controlled by the "control_table" which keeps
an index of log tables and corresponding dates.

The "rule_parameters" table converts unique combinations of "Violation path, Call-Type and Measurement" into integer indexes which are then stored in the log tables. The rule_parameters
table is never cleaned out by any of the scripts.

Password Hiding
---------------
MySQL requires a password for each user. If scritps are to be ran automatically, the easiest way
to protect passwords are to include them in a config file and change the permissions of this file
to be read only to the user. MySQL can read in a config file using the tag "--defaults-file=".
This config file should be of the format :
[client]
user=mistral
password=mistral
host=localhost
port=3306
database=multiple_mistral_log


Set-Up Instruction
------------------
From a terminal on the host machine designated to house the database, run
"mysql -u root -p < create_multiple_tables.sql"
And enter the password to the root user account. This will create the database with tables and
mistral user.

Run the "end_of_day" script once to set up tables for today and tomorrow :
"mysql -u root -p < end_of_day.sql"

Set up a CRON job to run "mysql --defaults-file=<path-to-password-file> -u mistral < end_of_day.sql" at the some point each day.


