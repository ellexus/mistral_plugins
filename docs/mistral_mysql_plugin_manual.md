% Ellexus - MySQL Output Plug-in Configuration Guide

# Summary

This document describes the MySQL logging plug-in written by Ellexus.
The plug-in is written in C and uses MySQL libraries.

This plug-in receives log messages from Mistral and writes them to a
MySQL database. Logs are kept in the database for at least 30 days
depending on usage.

# Installing the MySQL Plug-in

Extract the Mistral plug-ins archive that has been provided to you
somewhere sensible. Please make sure that you use the appropriate
version of the plug-ins (32 or 64 bit) for the architecture of the
machine on which the plug-in will run.

In addition, if the Mistral Plug-ins package was obtained separately
from the main Mistral product, please ensure that the version of the
plug-in downloaded is compatible with the version of Mistral in use.
Version 2.4 of the MySQL Plug-in as described in this document is
compatible with Mistral v2.10.4 and above.

The MySQL plug-in can be found in:

    <install_dir>/output/mistral_mysql_v2.4/x86_64/

for the 64 bit version and in:

    <install_dir>/output/mistral_mysql_v2.4/i386/

for the 32 bit version.

The plug-in must be available in the same location on all execution
hosts in your cluster.

# Installing the MySQL Mistral Database Schema

In order to set up the database and tables required for the plug-in, a
schema creation script has been provided and can be found in the
directory:

    <install_dir>/output/mistral_mysql_v2.4/sql/

This script should be run on the MySQL server host machine as root (or
other user with permission to create databases and users) using the
following command:

    $ mysql -u root -p < <script_path>/create_mistral.sql

This script creates 32 daily log and envirnoment tables that will be
used in a round robin fashion. A ***cron*** job should also be set up to
run the following command each day to correctly allocate log tables for
use:

    $ mysql -u root -p mistral_log < <script_path>/end_of_day.sql

This script will ensure the database is correctly configured to receive
log messages for the current date and will continue to function
uninterrupted when the date changes at midnight.

# Configuring Mistral to use the MySQL Plug-in

Please see the Plug-in Configuration section of the main Mistral User
Guide for full details of the plug-in configuration file specification.
Where these instructions conflict with information in the main Mistral
User Guide please verify that the plug-in version available is
compatible with the version of Mistral in use and, if so, the
information in the User Guide should be assumed to be correct.

## Mistral Plug-in Configuration File

The Mistral plug-in configuration file is an ASCII plain text file
containing all the information required by Mistral to use any OUTPUT or
UPDATE plug-ins required. Only a single plug-in of each type can be
configured.

Once complete the path to the configuration file must be set in the
environment variable ***MISTRAL\_PLUGIN\_CONFIG***. As with the plug-in
itself the configuration file must be available in the same canonical
path on all execution hosts in your cluster.

This section describes the specific settings required to enable the
MySQL Plug-in.

### PLUGIN directive

The *PLUGIN* directive must be set to “OUTPUT” i.e.

    PLUGIN,OUTPUT

### INTERVAL directive

The *INTERVAL* directive takes a single integer value parameter. This
value represents the time in seconds the Mistral application will wait
between calls to the specified plug-in e.g.

    INTERVAL,300

The value chosen is at the discretion of the user, however care should
be taken to balance the need for timely updates with the scalability of
the MySQL installation and the average length of jobs on the cluster.

### PLUGIN\_PATH directive

The *PLUGIN\_PATH* directive value must be the fully qualified path to
the MySQL plug-in as described above i.e.

    PLUGIN_PATH,<install dir>/mistral_mysql_v2.4/x86_64/mistral_mysql

The *PLUGIN\_PATH* value will be passed to */bin/sh* for environment
variable expansion at the start of each execution host job.

### PLUGIN\_OPTION directive

The *PLUGIN\_OPTION* directive is optional and can occur multiple times.
Each *PLUGIN\_OPTION* directive is treated as a separate command line
argument to the plug-in. Whitespace is respected in these values. A full
list of valid options for this plug-in can be found in section
[4.2](#anchor-9) [Plug-in Command Line Options](#anchor-9).

As whitespace is respected command line options that take parameters
must be specified as separate *PLUGIN\_OPTION* values. For example to
specify the username the plug-in should use to connect to the MySQL
server the option “*--user username*” must be provided, this must be
specified in the plug-in configuration file as:

    PLUGIN_OPTION,--user
    PLUGIN_OPTION,username

Options will be passed to the plug-in in the order in which they are
defined and each *PLUGIN\_OPTION* value will be passed to */bin/sh* for
environment variable expansion at the start of each execution host job.

### END Directive

The END directive indicates the end of a configuration block and does
not take any values.

## Plug-in Command Line Options

The following command line options are supported by the MySQL plug-in.

    -c filename, --defaults-file=filename

Set the location of the MySQL defaults file. This option must be
provided and point to a file that contains database connection details
in MySQL option file format, for example:

```
[client]
user=mistral
password=mistral
host=localhost
port=3306
database=mistral\_log
```

    -e filename, --error=filename

Set the location of the file which should be used to log any errors
encountered by the plug-in. Defaults to sending messages to stderr for
handling by Mistral.

    -v var-name, --var=var-name

The name of an environment variable, the value of which should be stored
by the plug-in. This option can be specified multiple times.

# Mistral’s MySQL data model

This section describes how the Mistral MySQL plug-in stores data within
MySQL.

## Tables

### The rule\_details table

The *rule\_details* table contains the stable rule data and is defined
as follows:

|                  |                 |          |                               |
| :--------------- | :-------------- | :------- | :---------------------------- |
| `rule_id`        | BIGINT UNSIGNED | NOT NULL | `AUTO_INCREMENT`, Primary key |
| `label`          | VARCHAR(256)    | NOT NULL | Unique key 1                  |
| `violation_path` | VARCHAR(256)    | NOT NULL | Unique key 1                  |
| `call_type`      | VARCHAR(45)     | NOT NULL | Unique key 1                  |
| `measurement`    | VARCHAR(13)     | NOT NULL | Unique key 1                  |
| `size_range`     | VARCHAR(64)     | NOT NULL | Unique key 1                  |
| `threshold`      | VARCHAR(64)     | NOT NULL | Unique key 1                  |

On receipt of a log message any new rule detected will be inserted into
this table. The values used are as follows:

|                              |                                                                                                                                           |
| :--------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------- |
| `rule_id`                    | An AUTO\_INCREMENT column used as the primary key.                                                                                        |
| `label`                      | Copied from the log message *LABEL* field unchanged. This field will be truncated if the log message value is longer than 256 characters. |
| `violation_path`             | Copied from the log message *PATH* field unchanged. This field will be truncated if the log message value is longer than 256 characters.  |
| `call_type`                  | Copied from the log message *CALL-TYPE*field unchanged.                                                                                   |
| `measurement`                | Copied from the log message *MEASUREMENT* field unchanged.                                                                                |
| `size_range`                 | Copied from the log message *SIZE-RANGE* field unchanged.                                                                                 |
| `threshold`                  | Copied from the log message *THRESHOLD* field unchanged.                                                                                  |

### The date\_table\_map table

The *date\_table\_map* table contains a mapping of dates to
*log\_nn*/*env\_nn* tables and is defined as follows:

|             |         |          |              |
| :---------- | :------ | :------- | :----------- |
| table\_date | DATE    | NOT NULL | Primary key  |
| table\_num  | TINYINT | NOT NULL | Unique key 1 |

### The log\_nn tables

There are 32 *log\_nn* tables where *nn* is a simple left zero padded
numeric counter from 1 to 32. Each of these tables contains log data for
a single day and are used in a round robin fashion. The end of day
processing script described in section [3](#anchor-2) [Installing the
MySQL Mistral Database Schema](#anchor-2) will reallocate the tables
containing the oldest data for use for the next day’s data as well as
the current date if necessary. Each *log\_nn* table is defined as
follows:

|                 |              |          |                              |
| :-------------- | :----------- | :------- | :--------------------------- |
| scope           | VARCHAR(6)   | NOT NULL | Unique key ScopeIndex        |
| type            | VARCHAR(8)   | NOT NULL | Unique key TypeIndex         |
| time\_stamp     | DATETIME(6)  | NOT NULL | Unique key TimeStampIndex    |
| rule\_id        | INT          | NOT NULL |                              |
| observed        | VARCHAR(64)  | NOT NULL |                              |
| pid             | INT          |          |                              |
| cpu             | INT          |          |                              |
| command         | VARCHAR(256) |          |                              |
| file\_name      | VARCHAR(256) |          |                              |
| group\_id       | VARCHAR(256) |          | Unique key IDsIndex          |
| id              | VARCHAR(256) |          | Unique key IDsIndex          |
| mpi\_rank       | INT          |          |                              |
| plugin\_run\_id | VARCHAR(36)  | NOT NULL | Unique key RunIDIndex        |
| log\_id         | INT          | NOT NULL | AUTO\_INCREMENT, Primary key |

On receipt of a log message a new record will be inserted into the
appropriate *log\_nn* table for the date specified in the *TIME-STAMP*
field. The values used are as follows:

|                 |                                                                                                                                               |
| :-------------- | :-------------------------------------------------------------------------------------------------------------------------------------------- |
| scope           | Either “local” or “global” indicating the scope of the contract containing the rule that generated the log.                                   |
| type            | Either “monitor” or “throttle” indicating the type of rule that generated the log.                                                            |
| time\_stamp     | Copied from the log message *TIME-STAMP*field unchanged.                                                                                      |
| rule\_id        | Cross reference to the *rule\_details* table pointing to the related rule information.                                                        |
| observed        | Copied from the log message *MEASURED-DATA* field unchanged.                                                                                  |
| pid             | Copied from the log message *PID* field unchanged.                                                                                            |
| cpu             | Copied from the log message *CPU* field unchanged.                                                                                            |
| command         | Copied from the log message *COMMAND* field unchanged. This field will be truncated if the log message value is longer than 256 characters.   |
| file\_name      | Copied from the log message *FILE-NAME* field unchanged. This field will be truncated if the log message value is longer than 256 characters. |
| group\_id       | Copied from the log message *JOB-GROUP-ID* field unchanged.                                                                                   |
| id              | Copied from the log message *JOB-ID* field unchanged.                                                                                         |
| mpi\_rank       | Copied from the log message *MPI-WORLD-RANK* field unchanged.                                                                                 |
| plugin\_run\_id | Unique 36 character UUID generated when the plug-in is initialised.                                                                           |
| log\_id         | An AUTO\_INCREMENT column used as the primary key.                                                                                            |

### The env\_nn tables

There are 32 *env\_nn* tables where *nn* is a simple left zero padded
numeric counter from 1 to 32. Each of these tables contains the value of
any optional environment variable settings requested be saved via the
use of one or more *--var* options. Each table contains data for a
single day and are used in a round robin fashion. The end of day
processing script described in section [3](#anchor-2) [Installing the
MySQL Mistral Database Schema](#anchor-2) will reallocate the tables
containing the oldest data for use for the next day’s data as well as
the current date if necessary. Each *env\_nn* table is defined as
follows:

|                 |              |          |                              |
| :-------------- | :----------- | :------- | :--------------------------- |
| plugin\_run\_id | VARCHAR(36)  | NOT NULL | Unique key RunIndex          |
| env\_name       | VARCHAR(256) | NOT NULL |                              |
| env\_value      | VARCHAR(256) | NOT NULL |                              |
| env\_id         | INT          | NOT NULL | AUTO\_INCREMENT, Primary key |

If the plug-in is started with one or more *--var* options, on receipt
of a log message with a previously unprocessed date specified in the
*TIME-STAMP* field a new record will be inserted into the appropriate
*env\_nn* table for each of the environment variables specified. The
values used are as follows:

|                     |                                                                                                                                                              |
| :------------------ | :----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| plugin\_run\_id     | Unique 36 character UUID generated when the plug-in is initialised.                                                                                          |
| env\_name           | The environment variable name as specified in the *--var* option. This field will be truncated if the specified variable name is longer than 256 characters. |
| env\_value          | The value of the environment variable specified in the *--var* option as detected when the plug-in is initialised.                                           |
| env\_id             | An AUTO\_INCREMENT column used as the primary key.                                                                                                           |
