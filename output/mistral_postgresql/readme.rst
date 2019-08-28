Mistral PostgreSQL Plugin
=========================

This plug-in receives violation data from Mistral and enters it into a PostgreSQL
database named ``mistral_log``.

The plug-in accepts the following command line options:

--defaults-file=config-file | -c config-file
  The name of a PostgreSQL formated ``options file`` containing the database
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

   PLUGIN_PATH,/path/to/mistral_postgresql

   INTERVAL,5

   PLUGIN_OPTION,--defaults-file=/path/to/connection-details.cnf
   PLUGIN_OPTION,--var=USER
   PLUGIN_OPTION,--var=SHELL
   PLUGIN_OPTION,--error=/path/to/mistral_psql.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plug-in configuration file.

Process Summary
---------------
The schema creation script creates a database called ``mistral_log`` containing
the following tables: mistral_log, env and a rule_details table. A user mistral is
created and granted all privileges on ``mistral_log``.

The data is the same as would be output from Mistral to a log file. Each field is
stored in a separate column with the exception of those stored in the ``rule_details``
table (see below). Additionally any environment variables specified on the
plug-in command line will be stored in the env table.

The ``rule_details`` table converts unique combinations of ``Violation path,
Call-Type, Size-Range, Measurement and Threshold`` into integer indexes which are
then stored in the log tables. The rule_details table is never cleaned out by
any of the scripts.

Password Hiding
---------------
PostgreSQL requires a password for each user. If scripts are to be run automatically,
the easiest way to protect passwords is to include them in a PostgreSQL format
``options file`` and change the permissions of this file to be read only to the user.
PostgreSQL can read in a configuration file using the option ``--defaults-file=``.  This
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

    # TODO: Write PostgreSQL instructions
    mysql -u root -p < create_mistral.sql

And enter the password to the root user account. This will create the database
schema and the related mistral user. Any user with sufficient privileges to
create both databases and users can be used in place of the root account.

Set up a ``CRON`` job to run ::

    # TODO: Write PostgreSQL instructions
    mysql --defaults-file=<path-to-password-file> -u mistral mistral_log < end_of_day.sql

once a day. <path-to-password-file> should point to the configuration file as
explained in ``Password Hiding``.


