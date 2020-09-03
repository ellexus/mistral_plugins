% Ellexus - Elasticsearch Plug-in Configuration Guide

# Installing the Elasticsearch Plug-in

Extract the Mistral plug-ins archive that has been provided to you
somewhere sensible. Please make sure that you use the appropriate
version of the plug-ins for the architecture of the machine on which the
plug-in will run.

In addition, if the Mistral Plug-ins package was obtained separately
from the main Mistral product, please ensure that the version of the
plug-in downloaded is compatible with the version of Mistral in use.
Version 5.3.2 of the Elasticsearch Plug-in as described in this document
is compatible with all versions of Mistral compatible with plug-in API
version 5. At the time of writing this is Mistral v2.11.2 and above.

The Elasticsearch plug-in can be found in, for example:

    <installation directory>/output/mistral_elasticsearch_v5.4.4/x86_64/

for the 64 bit Intel compatible version, and in:

    <installation directory>/output/mistral_elasticsearch_v5.4.4/aarch32/

for the 32 bit ARM compatible version.

The plug-in must be available in the same location on all execution
hosts in your cluster.

## Installing the Elasticsearch Index Mapping Template

Prior to using the Mistral Elasticsearch plug-in the index datatype
mapping template should be configured within Elasticsearch. The files
mappings\_`<n>`.x.json contains the appropriate configuration for
Elasticsearch installations where `<n>` corresponds to the major version
of Elasticsearch in use.

The provided templates ensure that date fields are correctly identified.
The template for Elasticsearch version 2.x also specifies some key
fields should not be analysed to make working with Grafana simpler.
Other, site specific, configuration can be added at the user’s
discretion.

If you have access to curl the file
mistral\_create\_elastic\_template.sh can be run to create the template
from the command line. This script will attempt to detect the
Elasticsearch version in use and create the template using the
appropriate mapping file.

The script takes the following command line options:

    -?, --help

Display usage instructions

    -i idx_name, --index=idx_name

The basename of the index. If not specified the template will be created
for indexes called mistral. If a custom value is used here a matching
option must be provided to the plug-in.

    -h hostname, --host=hostname

The name of the machine on which Elasticsearch is hosted. If not
specified the script will use localhost.

    -p filename, --password=filename

The name of a file containing the password to be used when creating the
index if needed. The password must be on a single line.

    -P n, --port=n

The port to be used when connecting to Elasticsearch. If not specified
the script will use port 9200.

    -s, --ssl

Use HTTPS protocol instead of HTTP to connect to Elasticsearch.

    -u user, --username=user

The username to be used when connecting to Elasticsearch if needed.

# Configuring Mistral to use the Elasticsearch Plug-in

Please see the Plug-in Configuration section of the main Mistral User
Guide for full details of the plug-in configuration file specification.
Where these instructions conflict with information in the main Mistral
User Guide please verify that the plug-in version available is
compatible with the version of Mistral in use and, if so, the
information in the User Guide should be assumed to be correct.

## Mistral Plug-in Configuration File

The Mistral plug-in configuration file is an ASCII plain text file
containing all the information required by Mistral to use any `OUTPUT`
or `UPDATE` plug-ins required. Only a single plug-in of each type can be
configured.

Once complete the path to the configuration file must be set in the
environment variable `MISTRAL_PLUGIN_CONFIG`. As with the plug-in itself
the configuration file must be available in the same canonical path on
all execution hosts in your cluster.

This section describes the specific settings required to enable the
Elasticsearch Plug-in.

### PLUGIN directive

The `PLUGIN` directive must be set to `OUTPUT` i.e.

PLUGIN,OUTPUT

### INTERVAL directive

The `INTERVAL` directive takes a single integer value parameter. This
value represents the time in seconds the Mistral application will wait
between calls to the specified plug-in e.g.

    INTERVAL,300

The value chosen is at the discretion of the user, however care should
be taken to balance the need for timely updates with the scalability of
the Elasticsearch installation and the average length of jobs on the
cluster.

### PLUGIN\_PATH directive

The `PLUGIN_PATH` directive value must be the fully qualified path to
the Elasticsearch plug-in as described above i.e.

    PLUGIN\_PATH,<install_dir>/output/mistral_elasticsearch_v2.0/x86_64/mistral_elasticsearch

The `PLUGIN_PATH` value will be passed to `/bin/sh` for environment
variable expansion at the start of each execution host job.

### PLUGIN\_OPTION directive

The `PLUGIN_OPTION` directive is optional and can occur multiple times.
Each `PLUGIN_OPTION` directive is treated as a separate command line
argument to the plug-in. Whitespace is respected in these values. A full
list of valid options for this plug-in can be found in section
[2.2](#anchor-9) [Plug-in Command Line Options](#anchor-9).

As whitespace is respected command line options that take parameters
must be specified as separate `PLUGIN_OPTION` values. For example, to
specify the hostname the plug-in should use to connect to the
Elasticsearch server the option `-h
hostname` or `--host=hostname` must be provided, this must be specified
in the plug-in configuration file as:

    PLUGIN_OPTION,-h
    PLUGIN_OPTION,hostname

or

    PLUGIN_OPTION,--host=hostname

Options will be passed to the plug-in in the order in which they are
defined and each `PLUGIN_OPTION` value will be passed to `/bin/sh` for
environment variable expansion at the start of each execution host job.

### END Directive

The `END` directive indicates the end of a configuration block and does
not take any values.

## Plug-in Command Line Options

The following command line options are supported by the Elasticsearch
plug-in.

    -e file, --error=file

Specify location for error log. If not specified all errors will be
output on `stderr` and handled by Mistral error logging.

    -h hostname, --host=hostname

The hostname of the Elasticsearch server with which to establish a
connection. If not specified the plug-in will default to `localhost`.

    -i index_name, --index=index_name

Set the index to be used for storing data. This should match the index
name provided when defining the index mapping template (see section
[1.1](#anchor-2)). The plug-in will create indexes named
`<idx_name>-yyyy-MM-dd`. If not specified the plug-in will default to
`mistral`.

    -m octal-mode, --mode=octal-mode

Permissions used to create the error log file specified by the -e
option.

    -p secret, --password=secret

The password required to access the Elasticsearch server if needed.

    -P number, --port=number

Specifies the port to connect to on the Elasticsearch server host. If
not specified the plug-in will default to `9200`.

    -s, --ssl

Connect to the Elasticsearch server via secure HTTP.

    -u user, --username=user

The username required to access the Elasticsearch server if needed.

    -v var-name, --var=var-name

The name of an environment variable, the value of which should be stored
by the plug-in. This option can be specified multiple times.

    -V num, --es-version=num

The major version of the Elasticsearch server to connect to. If not
specified the plug-in will default to "`5`".

# Mistral’s Elasticsearch Document Model

This section describes how the Mistral Elasticsearch Plug-in stores data
within Elasticsearch.

The Mistral Elasticsearch Plug-in will create indexes with a date
appended, by default these will be named `mistral-yyyy-MM-dd`. This
allows for better management of historic data.

Documents are inserted into these indexes with the following labels and
structure:

```json
{
    "@timestamp",
    "rule": {
        "scope",
        "type",
        "label",
        "measurement",
        "calltype",
        "path",
        "threshold",
        "timeframe",
        "size-min",
        "size-max"
    },
    "job": {
        "host",
        "job-group-id",
        "job-id"
    },
    "process": {
        "pid",
        "command",
        "file",
        "cpu-id",
        "mpi-world-rank"
    },
    "environment": {
        "var-name",
        …
    },
    "value"
}
```

By default, the only explicit type mapping defined is for `@timestamp`
which is set to be a date field.

The Mistral Elasticsearch Plug-in will insert documents as described in
the following table.

| Field                    | Value                                                   |
| :----------------------- | :------------------------------------------------------ |
| `@timestamp`             | Inserted as a UTC text date in the format `yyyy-MM-ddTHH:mm:ss.nnnZ `for example `2017-04-25T15:27:28.345Z` |
| `rule.scope`             | Inserted as a text string, set to either local or global indicating the scope of the contract containing the rule that generated the log. |
| `rule.type`              | Inserted as a text string, set to either monitor or throttle indicating the type of rule that generated the log. |
| `rule.label`             | Inserted as a text string, copied from the log message `LABEL` field unchanged. |
| `rule.measurement`       | Inserted as a text string, copied from the log message `MEASUREMENT` field unchanged. |
| `rule.calltype`          | Inserted as a text string, the list of call types specified in the log message `CALL-TYPE` field. The Mistral Elasticsearch plug-in will always log compound types in alphabetical order. E.g. if the log message listed call types as `read+write+seek` the plug-in will normalise this to `read+seek+write`. |
| `rule.path`              | Inserted as a text string, copied from the log message `PATH` field unchanged. |
| `rule.threshold`         | Inserted as a number, the rule limit as reported in the log message `THRESHOLD` field converted into the smallest unit for the measurement type. For bandwidth rules this field will be bytes, for latency rules it is microseconds and for count rules the simple raw count. |
| `rule.timeframe`         | Inserted as a number, the timeframe the measurement was taken over as reported in the log message `THRESHOLD` field, converted into microseconds. |
| `rule.size-min`          | Inserted as a number, the lower bound of the operation size range as reported in the log message `SIZE-RANGE` field, converted into bytes. If this field was set to `all` in the log message this value will be set to 0. |
| `rule.size-max`          | Inserted as a number, the upper bound of the operation size range as reported in the log message `SIZE-RANGE` field, converted into bytes. If this field was set to `all` in the log message this value will be set to the maximum value of an `ssize_t`. This value is system dependent but for 64 bit machines this will typically be `9223372036854775807`. |
| `job.host`               | Inserted as a text string, copied from the log message `HOSTNAME` field with any domain component removed. |
| `job.job-group-id`       | Inserted as a text string, copied from the log message `JOB-GROUP-ID` field unchanged or `N/A` if this field is blank. |
| `job.job-id`             | Inserted as a text string, copied from the log message `JOB-ID` field unchanged or `N/A` if this field is blank. |
| `process.pid`            | Inserted as a number, copied from the log message `PID` field unchanged. |
| `process.command`        | Inserted as a text string, copied from the log message `COMMAND-LINE` field unchanged. |
| `process.file`           | Inserted as a text string, copied from the log message `FILE-NAME` field unchanged. |
| `process.cpu-id`         | Inserted as a number, copied from the log message `CPU` field unchanged. |
| `process.mpi-world-rank` | Inserted as a number, copied from the log message `MPI-WORLD-RANK` field unchanged. |
| `environment.var-name`   | Inserted as a text string. The string `var-name` will be replaced by an environment variable name as specified in a `--var` option. The value stored will be the value of this variable as detected when the plug-in is initialised. If no `--var` options are specified the environment block will be omitted. |
| `value`                  | Inserted as a number, copied from the log message `MEASURED-DATA` field converted into the smallest unit for the measurement type. For bandwidth rules this field will be bytes, for latency rules it is microseconds and for count rules the simple raw count. |
