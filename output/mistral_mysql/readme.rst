Mistral MySQL Plugin
====================

This plug-in receives violation data from Mistral and enters it into a MySQL
database named ``mistral_log``.

The plug-in accepts the following command line options:

--defaults-file=config-file | -c config-file
  The name of a MySQL formated ``options file`` containing the database
  connection details. See ``Password Hiding`` below for more details.

--error=filename | -e filename
  The name of the file to which any error messages will be written.

--mode=octal-mode | -m octal-mode
  Permissions used to create the error log file specified by the -e option.

--var=var-name | -v var-name
  The name of an environment variable, the value of which should be stored by
  the plug-in. This option can be specified multiple times.

The options would normally be included in a plug-in configuration file, such as

::

   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_mysql

   INTERVAL,5

   PLUGIN_OPTION,--defaults-file=/path/to/connection-details.cnf
   PLUGIN_OPTION,--var=USER
   PLUGIN_OPTION,--var=SHELL
   PLUGIN_OPTION,--error=/path/to/mistral_influxdb.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plug-in configuration file.

Process Summary
---------------
The schema creation script creates a database called ``mistral_log`` containing
the following tables: 32 log tables; 32 environment tables; a control table
named date_table_map; and a rule_details table. A user ``'mistral'@'%'`` is
created and granted all privileges on ``mistral_log``.

The plug-in uses a 32 day rotating log system to store Mistral data. The data is
the same as would be output from Mistral to a log file. Each field is stored in
a separate column with the exception of those stored in the ``rule_details``
table (see below). Additionally any environment variables specified on the
plug-in command line will be stored in a similar set of 32 rotating environment
tables.

At some point during each day, the ``end_of_day`` script must be run. This script
recycles the two oldest log and environment tables in order to prepare empty
tables for that day and the next if needed. This is controlled by the
``date_table_map`` which keeps an index of table numbers and corresponding dates.
Records in the log and environment tables can be joined using a unique run ID
held in the column named ``plugin_run_id``

The ``rule_details`` table converts unique combinations of ``Violation path,
Call-Type, Size-Range, Measurement and Threshold`` into integer indexes which are
then stored in the log tables. The rule_details table is never cleaned out by
any of the scripts.

Password Hiding
---------------
MySQL requires a password for each user. If scripts are to be run automatically,
the easiest way to protect passwords is to include them in a MySQL format
``options file`` (see https://dev.mysql.com/doc/refman/5.7/en/option-files.html)
and change the permissions of this file to be read only to the user. MySQL can
read in a configuration file using the option ``--defaults-file=``.  This
configuration file should be of the format ::

    [client]
    user=mistral
    password=mistral
    host=localhost
    port=3306
    database=mistral_log

The plug-in requires the use of such a file to specify the database connection
parameters.

Set-Up Instructions
-------------------
From a terminal on the host machine designated to house the database, run ::

    mysql -u root -p < create_mistral.sql

And enter the password to the root user account. This will create the database
schema and the related mistral user. Any user with sufficient privileges to
create both databases and users can be used in place of the root account.

Set up a ``CRON`` job to run ::

    mysql --defaults-file=<path-to-password-file> -u mistral mistral_log < end_of_day.sql

once a day. <path-to-password-file> should point to the configuration file as
explained in ``Password Hiding``.


