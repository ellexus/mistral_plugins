Mistral PostgreSQL Plugin
=========================

This plug-in receives violation data from Mistral and enters it into a PostgreSQL
database named ``mistral_log``.

The plug-in accepts the following command line options:

--error=filename | -e filename
  The name of the file to which any error messages will be written.

--mode=octal-mode | -m octal-mode
  Permissions used to create the error log file specified by the -e option.

--var=var-name | -v var-name
  The name of an environment variable, the value of which should be stored by
  the plug-in. This option can be specified multiple times.

--host=hostname | -h hostname
  The hostname of the PostgreSQL server with which to establish a
  connection. If not specified the plug-in will default to 'localhost'

--dbname=database_name | -d database_name
  Set the database name to be used for storing data. Defaults to "mistral_log"
  
--password=secret | -p secret\
  The password required to access the PostgreSQL server if needed. If not
  specified the plug-in will default to "ellexus".
  
--port=number | -P number
  Specifies the port to connect to on the PostgreSQL server host.
  If not specified the plug-in will default to "5432".

--username=user | -u user
  The username required to access the PostgreSQL server if needed. If not
  specified the plug-in will default to "mistral"

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
the following tables: bandwidth, counts, memory, latency, cpu, seek_distance, env and a 
rule_details table. A user mistral is created and granted all privileges on ``mistral_log``.

The data is the same as would be output from Mistral to a log file. Each field is
stored in a separate column with the exception of those stored in the ``rule_details``
table (see below). Additionally any environment variables specified on the
plug-in command line will be stored in the env table.

The ``rule_details`` table converts unique combinations of ``Violation path,
Call-Type, Size-Range, Measurement and Threshold`` into integer indexes which are
then stored in the log tables. The rule_details table is never cleaned out by
any of the scripts.

Passwords
---------
PostgreSQL has quite a few options for user security. This plug-in is currently
setup to work with an md5 password specified at runtime. This plug-in will need
modifications in order to work with any other method of authentication.

Set-Up Instructions
-------------------
From a terminal on the host machine designated to house the database, run ::

    sudo -u postgres psql < create_mistral.sql

This will create the database schema and the related mistral user. Any user with
sufficient privileges to create both databases and users can be used in place of
the postgres account.

There are no data maintenance functions currently present. We would recommend
retaining data for a few days or weeks at most, and a regular clear down from 
all the tables based on the time_stamp column would be sensible.
