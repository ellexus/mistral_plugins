% Ellexus - InfluxDB Plug-in Configuration Guide

# Installing the InfluxDB Plug-in

Extract the Mistral plug-ins archive that has been provided to you
somewhere sensible. Please make sure that you use the appropriate
version of the plug-ins for the architecture of the machine on which the
plug-in will run.

In addition, if the Mistral Plug-ins package was obtained separately
from the main Mistral product, please ensure that the version of the
plug-in downloaded is compatible with the version of Mistral in use.
Version 4.0 of the InfluxDB Plug-in as described in this document is
compatible with Mistral v2.11.2 and above.

The InfluxDB plug-in can be found in, for example:

    <installation directory>/output/mistral_influxdb_v4.0/x86_64/

for the 64 bit Intel compatible version, and in:

    <installation directory>/output/mistral_influxdb_v4.0/aarch32/

for the 32 bit ARM compatible version.

The plug-in must be available in the same location on all execution
hosts in your cluster.

# Configuring Mistral to use the InfluxDB Plug-in

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
InfluxDB Plug-in.

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
the InfluxDB installation and the average length of jobs on the cluster.

### PLUGIN\_PATH directive

The *PLUGIN\_PATH* directive value must be the fully qualified path to
the InfluxDB plug-in as described above i.e.

    PLUGIN_PATH,<install_dir>/mistral_influxdb_v2.4/x86_64/mistral_influxdb

The *PLUGIN\_PATH* value will be passed to */bin/sh* for environment
variable expansion at the start of each execution host job.

### PLUGIN\_OPTION directive

The *PLUGIN\_OPTION* directive is optional and can occur multiple times.
Each *PLUGIN\_OPTION* directive is treated as a separate command line
argument to the plug-in. Whitespace is respected in these values. A full
list of valid options for this plug-in can be found in section
[2.2](#anchor-7) [Plug-in Command Line Options](#anchor-7).

As whitespace is respected command line options that take parameters
must be specified as separate *PLUGIN\_OPTION* values. For example to
specify the username the plug-in should use to connect to the InfluxDB
server the option “*-u username*” must be provided, this must be
specified in the plug-in configuration file as:

    PLUGIN_OPTION,-u
    PLUGIN_OPTION,username

Options will be passed to the plug-in in the order in which they are
defined and each *PLUGIN\_OPTION* value will be passed to */bin/sh* for
environment variable expansion at the start of each execution host job.

### END Directive

The END directive indicates the end of a configuration block and does
not take any values.

## Plug-in Command Line Options

The following command line options are supported by the InfluxDB
plug-in.

    -d dbname, --database=dbname

Set the name of the database instance the plug-in should connect to.
Defaults to “mistral”

    -e filename, --error=filename

Set the location of the file which should be used to log any errors
encountered by the plug-in. Defaults to sending messages to *stderr* for
handling by Mistral.

    -h hostname, --host=hostname

Set the location of the InfluxDB host the plug-in should connect to.
Defaults to “localhost”

    -m octal-mode, --mode=octal-mode

Permissions used to create the error log file specified by the -e
option.

    -p passwd, --password=passwd

Set the password the plug-in should use to authenticate its connection
to InfluxDB. Defaults to “” if the *--user* option is specified. If both
the *--user* and *--password* options are unspecified the plug-in will
attempt to initiate an unauthenticated connection.

    -P number, --port=number

Set the port the plug-in should use for the connection. Defaults to
“8086”

    -s, --https

Use https to connect to InfluxDB rather than plain http.

    -u username, --user=username

Set the username the plug-in should use to authenticate its connection
to InfluxDB. Defaults to “” if the *--password* option is specified. If
both the *--user* and *--password* options are unspecified the plug-in
will attempt to initiate an unauthenticated connection.

    -v var-name, --var=var-name

The name of an environment variable, the value of which should be stored
by the plug-in. This option can be specified multiple times.

# Mistral’s InfluxDB data model

This section describes how the Mistral InfluxDB plug-in stores data
within InfluxDB. It should be noted that there is some duplication of
terminology between Mistral and InfluxDB.

## Measurements

Both products use the term “measurement”, in Mistral’s case this refers
to the type of statistic being recorded e.g. bandwidth versus
mean-latency, for InfluxDB it is the primary identifier of related data
(analogous to a table in a standard RDBMS).

The use of this term by both products is closely enough aligned that the
Mistral InfluxDB plug-in uses its measurement value as the measurement
key that data is recorded under in InfluxDB. As such the term may be
used interchangeably below.

The following is a list of all possible measurement values that can be
produced by Mistral. As above where this list conflicts with information
in the main Mistral User Guide please verify that the plug-in version
available is compatible with the version of Mistral in use and, if so,
the information in the User Guide should be assumed to be correct.

|               |                                                                                                                                                                                                                                                                  |
| :-------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| bandwidth     | Amount of data processed by calls of the specified type in the time frame. This applies only to “read” and “write” calls.                                                                                                                                        |
| count         | The number of calls of the specified type in the time frame.                                                                                                                                                                                                     |
| seek-distance | Total distance moved within files by calls of the specified type in the time frame. This applied only to “seek” calls.                                                                                                                                           |
| max-latency   | The maximum duration of any call of the specified type in the time frame. Subject to data sampling, see the Mistral User Guide for more information.                                                                                                             |
| mean-latency  | The mean duration of any call of the specified type in the time frame, provided the number of calls is higher than the value of ***MISTRAL\_MONITOR\_LATENCY\_MIN\_IO***. Subject to data sampling, see the Mistral User Guide for more information.             |
| total-latency | The total duration of time spent in calls of the specified type in the time frame, provided the number of calls is higher than the value of ***MISTRAL\_MONITOR\_LATENCY\_MIN\_IO***. Subject to data sampling, see the Mistral User Guide for more information. |

## Series Timestamps

Mistral produces log entries with time stamps to a precision of one
microsecond. The Mistral InfluxDB plug-in will attempt to normalise any
timezone information to derive the correct value for the “microseconds
since epoch” required by InfluxDB.

## Tag Keys

The Mistral InfluxDB plug-in records log entries using the following Tag
Keys

| Tag Key     | Description                                             |
| :---------- | :------------------------------------------------------ |
| `calltype`  | The list of call types specified in the log message `CALL-TYPE` field. The Mistral InfluxDB plug-in will always log compound types in alphabetical order. E.g. if the log message listed call types as `read+write+seek` the plug-in will normalise this to `read+seek+write` |
| `host`      | Copied from the log message `HOSTNAME` field with any domain component removed. |
| `job-group` | Copied from the log message `JOB-GROUP-ID` field unchanged or `N/A` if this field is blank. |
| `job-id`    | Copied from the log message `JOB-ID` field unchanged or `N/A` if this field is blank. |
| `label`     | Copied from the log message `LABEL` field unchanged. |


In addition, if the plug-in is started with one or more *--var* options
a tag key will be created for each of the environment variables
specified and populated with the value detected when the plug-in is
initialised. For example if the plug-in is started with the option
*--var=USER* then a tag key *USER* will also be created and populated
with the value of *$USER* read when the plug-in is started.

## Field Values

The Mistral InfluxDB plug-in records log entries using the following
Field Values:

| Field       | Value                                                   |
| :---------- | :------------------------------------------------------ |
| `command`   | Copied from the log message `COMMAND-LINE` field unchanged. |
| `cpu`       | Copied from the log message `CPU` field unchanged. |
| `file`      | Copied from the log message `FILE-NAME` field unchanged. |
| `logtype`   | Either `monitor` or `throttle` indicating the type of rule that generated the log. |
| `mpirank`   | Copied from the log message `MPI-WORLD-RANK` field unchanged. |
| `path`      | Copied from the log message `PATH` field unchanged. |
| `pid`       | Copied from the log message `PID` field unchanged. |
| `scope`     | Either `local` or `global` indicating the scope of the contract containing the rule that generated the log. |
| `size-max`  | The upper bound of the operation size range as reported in the log message `SIZE-RANGE` field, converted into bytes. If this field was set to `all` in the log message this value will be set to the maximum value of an `ssize_t`. This value is system dependent but for 64 bit machines this will typically be 9223372036854775807. |
| `size-min`  | The lower bound of the operation size range as reported in the log message `SIZE-RANGE` field, converted into bytes. If this field was set to `all` in the log message this value will be set to `0`. |
| `threshold` | The rule limit as reported in the log message `THRESHOLD` field converted into the smallest unit for the measurement type. For bandwidth rules this field will be bytes, for latency rules it is microseconds and for count rules the simple raw count. |
| `timeframe` | The timeframe the measurement was taken over as reported in the log message `THRESHOLD` field, converted into microseconds. |
| `value`     | The measured value as reported in the log message `MEASURED-DATA` field converted into the smallest unit for the measurement type. For bandwidth rules this field will be bytes, for latency rules it is microseconds and for count rules the simple raw count. |


# Querying Mistral InfluxDB data

Although it is possible to query InfluxDB data directly the Mistral
InfluxDB plug-in is packaged with a simple command line script
*mistral\_query\_influx.sh* which can be used to identify jobs that have
violated rules.

This script will return a list of job IDs and rule labels of InfluxDB
log entries that satisfy the parameters provided.

The following command line options are supported by the
*mistral\_query\_influx.sh* script.

    -?, --help

Show a summary of the command line options available.

    -c calltype, --call-type=calltype

Limit the query to the provided call type. The provided *calltype* value
must be a valid combination of call types as described in the Mistral
User Guide.

    -d dbname, --database=dbname

Set the name of the database instance the script should connect to.
Defaults to “mistral”

    -f file, --filename=file

Limit the query to the provided filename. Used as a regular expression
in a sub-string match.

    -g groupid, --group=groupid

Limit the query to the provided job group ID.

    -h hostname, --host=hostname

Set the location of the InfluxDB host the script should connect to.
Defaults to “localhost”

    -i pid, --pid=pid

Limit the query to the provided PID.

    -j id, --job=id

Limit the query to the provided job ID.

    -m measure, --measurement=measure

Type of rule to check. Must be a valid measurement type as describe in
section [3.1](#anchor-11) [Measurements](#anchor-11) above. Defaults to
“bandwidth”

    -n num, --port=num

Set the port the script should use for the connection. Defaults to
“8086”

    -o cmd, --command=cmd

Limit the query to the provided command line. Used as a regular
expression in a sub-string match.

    -p file, --password=file

Filename that contains the connection password. Implies *-u*.

    -q, --quiet

Suppress informative messages. If set the script will only output the
list of job IDs and rule labels found.

    -r label, --rule=label

Limit the query to the provided rule label.

    -s, --ssl

Use https to connect to InfluxDB rather than plain http.

    -t n[m|h], --time=n[m|h]

Limit the time period to check for rule violations to the last “n”
minutes or hours. Must be specified in the format ***n\[m|h\]*** where
'n' is an integer which must be immediately followed by one of 'm' or
'h' indicating 'minutes' or 'hours' respectively. For example five
minutes would be specified as "5m".

    -u username, --user=username

Set the username the script should use to authenticate its connection to
InfluxDB. If *-p* is not specified the user will be prompted for the
password.

At least one of the options that limit the output of the query, *-c*,
*-f*, *-g*, *-i*, *-j*, *-o*, *-r*, or *-t*, must be specified.

Output is a comma separated list of job IDs and the rule labels recorded
with one record per line.
