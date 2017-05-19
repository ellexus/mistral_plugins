Mistral MySQL Plugin
====================

Intro
-----
This plugin receives violation data from Mistral and enters it into a MySQL database named
"mistral_log". MySQL must be installed on every machine that will run jobs under Mistral.

Process Summary
---------------
The schema creation script creates a database called "mistral_log". Within it are 34 tables:
32 log tables; a control table named date_table_map; and a rule_details table. A user
"'mistral'@'%'" is created and granted all privileges on "mistral_log".

The plugin uses a 32 day rotating log system to store Mistral data. The data is the same as would
be output from Mistral to a log file. Each field is stored in a separate column with the exception
of those stored in the "rule_details" table (see below). At some point during each day, the
"end_of_day" script must be run. This script recycles the two oldest log tables in order to
create log tables for that day and the next. This is controlled by the "date_table_map" which keeps
an index of log tables and corresponding dates.

The "rule_details" table converts unique combinations of "Violation path, Call-Type, Size-Range, 
Measurement and Threshold" into integer indexes which are then stored in the log tables. The
rule_details table is never cleaned out by any of the scripts.

Password Hiding
---------------
MySQL requires a password for each user. If scripts are to be run automatically, the easiest way
to protect passwords is to include them in a config file and change the permissions of this file
to be read only to the user. MySQL can read in a config file using the tag "--defaults-file=".
This config file should be of the format ::

    [client]
    user=mistral
    password=mistral
    host=localhost
    port=3306
    database=mistral_log


Set-Up Instructions
-------------------
From a terminal on the host machine designated to house the database, run ::

    "mysql -u root -p < create_mistral.sql"

And enter the password to the root user account. This will create the database schema and the
related mistral user. Any user with sufficient privileges to create both databases and users can be
used in place of the root account.

Set up a ``CRON`` job to run ::

    "mysql --defaults-file=<path-to-password-file> -u mistral mistral_log < end_of_day.sql"

once a day. <path-to-password-file> should point to the config file as explained in "Password
Hiding".


