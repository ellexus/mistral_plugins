% Mistral Plug-in API version 5

# Summary

The following document details version 5 of the API used by the Mistral
application to communicate with plug-ins. This version of the API
introduces support for the extended log format introduced in Mistral
2.11.2.

There are also instructions on how to configure Mistral to use a plug-in
in normal use. Where these instructions conflict with information in the
main Mistral User Guide please verify that the API version is compatible
and, if so, the information in the User Guide should be assumed to be
correct.

# Change Log

## Changes in API version 5

The following changes have been made to the API in version 5

|            |                                                 |                            |
| :--------- | :---------------------------------------------- | :------------------------- |
| Feature    | Description of change                           | Section(s)                 |
| Log format | Log entries include the new log Backtrace field | [5.3.1](#output-plug-in-1) |

Version 3 of the API is now no longer supported and any plug-ins that
use this version of the API will not work with Mistral version 2.11.2 or
higher.

Existing plug-ins that use version 4 of the API will continue to
function with the following behaviour:

  - Log messages will be output in the log format used by Mistral
    between versions 2.11.0 and 2.11.1.

Please note that version 6 of the API is expected to be a major re-write
to increase functionality and reduce the number of breaking API changes
in the future. `As a result it is expected that
with the introduction of version
6
of the API all existing plug-ins will cease to
function.` Please contact Ellexus support if you require early access to
the update API.

## Changes in API version 4

The following changes have been made to the API in version 4

|            |                                                                    |                            |
| :--------- | :----------------------------------------------------------------- | :------------------------- |
| Feature    | Description of change                                              | Section(s)                 |
| Log format | Timestamps in log entries are now output to microsecond precision. | [5.3.1](#output-plug-in-1) |

Version 2 of the API is now no longer supported and any plug-ins that
use this version of the API will not work with Mistral version 2.11.0 or
higher.

Version 4 of the API is now deprecated and will be removed with the
introduction of version 6.

Existing plug-ins that use version 3 of the API will continue to
function with the following behaviour:

  - Log messages will be output in the log format used by Mistral
    between versions 2.10.0 and 2.10.3.

## Changes in API version 3

The following changes have been made to the API in version 3

|            |                                                                          |                            |
| :--------- | :----------------------------------------------------------------------- | :------------------------- |
| Feature    | Description of change                                                    | Section(s)                 |
| Log format | Log entries include three new log fields, hostname, CPU ID, and MPI Rank | [5.3.1](#output-plug-in-1) |

Version 1 of the API is now no longer supported and any plug-ins that
use this version of the API will not work with Mistral version 2.10.0 or
higher.

Version 2 of the API is now deprecated and will be removed with the
introduction of version 4.

Existing plug-ins that use version 2 of the API will continue to
function with the following behaviour:

  - Log messages will be output in the log format used by Mistral
    between versions 2.9.1 and 2.10.0.

## Changes in API version 2

The following changes have been made to the API in version 2

|                 |                                                      |                            |
| :-------------- | :--------------------------------------------------- | :------------------------- |
| Feature         | Description of change                                | Section(s)                 |
| Contract format | Support added for version 2 contracts.               | [5.3.2](#update-plug-ins)  |
| Log format      | Plug-in log field separator changed from ':' to '\#' | [5.3.1](#output-plug-in-1) |
| Log format      | Log entries include the new size range rule field    | [5.3.1](#output-plug-in-1) |

Version 1 of the API is now deprecated and will be removed with the
introduction of version 3.

Existing plug-ins that use version 1 of the API will continue to
function with the following behaviour:

  - Log messages will be output in the log format used prior to Mistral
    version 2.9.1

  - If the contract used specified a size range other than “all” the
    range will be appended to the rule label separated by a single
    underscore character “\_” in both contract rules and output log
    messages.

  - There is no way to update size range values using existing update
    plug-ins. All rules generated this way will have the size range set
    to “all” regardless of whether any original rule specified a range.

# Available Plug-ins

In version 5 of the API two plug-ins are supported.

## Update Plug-in

The `update plug-in` is used to modify ` local  `Mistral configuration
contracts dynamically during a job execution run according to conditions
on the node and / or cluster. Using an update plug-in is the only way to
modify the local contracts in use by a running job.

` Global  `contracts are assumed to be configured with high “system
threatening” rules that should not be frequently changed. These
contracts are intended to be maintained by system administrators and
will be polled periodically for changes on disk. Global contracts cannot
be modified by the update plug-in in any way.

If an update plug-in configuration is not defined Mistral will use the
same local configuration contracts throughout the life of the job.

## Output Plug-in

The `output plug-in` is used to record alerts generated by the Mistral
application. All event alerts raised against any of the four valid
contract types are sent to the output plug-in. The four contract types
are:

  - Global Monitoring

  - Global Throttling

  - Local Monitoring

  - Local Throttling

If an output plug-in configuration is not defined Mistral will default
to recording alerts to disk as described in the Mistral User Guide.

# Plug-in Configuration

On start up Mistral will check the environment variable
`MISTRAL_PLUGIN_CONFIG`. If this environment variable is defined it must
point to a file that the user running the application can read. If the
environment variable is not defined Mistral will assume that no plug-ins
are required and will use the default behaviours as described above.

The expected format of the configuration file consists of one block of
configuration lines for each configured plug-in. Each line is a comma
separated pair of a single configuration option directive and its value.
Whitespace is treated as significant in this file. The full
specification for a plug-in configuration block is as follows:

PLUGIN,\<OUTPUT|UPDATE\>

INTERVAL,\<Calling interval in seconds\>

PLUGIN\_PATH,\<Fully specified path to plug-in\>

\[PLUGIN\_OPTION,\<Option to pass to plug-in\>\]

…

END

## PLUGIN directive

The ` PLUGIN  `directive can take one of only two values, “UPDATE” or
“OUTPUT” which indicates the type of plug-in being configured. It
should be the first directive of the configuration block. If multiple
configuration blocks are defined for the same plug-in the values
specified in the later block will take precedence.

## INTERVAL directive

The `INTERVAL` directive takes a single integer value parameter. This
value represents the time in seconds the Mistral application will wait
between calls to the specified plug-in.

## PLUGIN\_PATH directive

The ` PLUGIN_PATH  `directive value must be the fully qualified path to
the plug-in to be run e.g. `/home/ellexus/bin/output_plugin.sh`. This
plug-in must be executable by the user that starts the Mistral
application. The plug-in must also be available in the same location on
all possible remote host nodes where Mistral is expected to run.

The `PLUGIN_PATH` value will be passed to `/bin/sh` for environment
variable expansion at the start of each execution host job.

## PLUGIN\_OPTION directive

Unlike the previous three directives which are mandatory, the
`PLUGIN_OPTION` directive is optional and can occur multiple times. Each
`PLUGIN_OPTION` directive is treated as a separate command line argument
to the plug-in. Whitespace is respected in these values.

    As whitespace is respected command line options that take parameters must be specified as separate PLUGIN_OPTION values. For example if the plug-in uses the option
    “--output /dir/name/” to specify where to store its output then this must be specified in the plug-in configuration file as:

PLUGIN\_OPTION,--output

PLUGIN\_OPTION,/dir/name/

Options will be passed to the plug-in in the order in which they are
defined.

Each `PLUGIN_OPTION` value will be passed to `/bin/sh` for environment
variable expansion at the start of each execution host job.

## END Directive

The `END` directive indicates the end of a configuration block and does
not take any values.

## Comments and Blank Lines

Blank lines and lines starting with “\#” are silently ignored.

## Invalid Configuration

All lines that do not match one of the configuration directives defined
above cause a warning to be raised.

## Example Configuration

Consider the following configuration file; line numbers have been added
for clarity:

1\# Plug-ins configuration

2PLUGIN,OUTPUT

3INTERVAL,300

4PLUGIN\_PATH,/home/ellexus/bin/output\_plugin.sh

5PLUGIN\_OPTION,--output

6PLUGIN\_OPTION,/home/ellexus/log files

7END

8

9PLUGIN,UPDATE

10INTERVAL,60

11PLUGIN\_PATH,$HOME/bin/update\_plugin

12END

The configuration file above sets up both update and output plug-ins.

Line 1 starts with a '`#`' character and is ignored as comment.

The first configuration block defines an output plug-in (line 2) that
will be called every 300 seconds (line 3) using the command line

`/home/ellexus/bin/output_plugin.sh –-output
"/home/ellexus/log files"`

(lines 4-6). The configuration block is terminated on line 7.

The blank line is ignored (line 8).

The second configuration block defines an update plug-in (line 9) that
will be called every 60 seconds (line 10) using the command line
`/home/ellexus/bin/update_plugin` (line 11), assuming `$HOME` is set to
`/home/ellexus`. The configuration block is terminated on line 12.

# Communication to Plug-ins

Mistral communicates with plug-ins via the default `stdin`, `stdout` and
`stderr` streams. Mistral will pass all data to the plug-in's `stdin`
stream and listen to both the plug-in's `stdout` and `stderr` streams.
Mistral will format any output received on `stderr` as a Mistral error
message.

All Mistral control messages are ASCII text. Data messages containing
rules and log messages may contain any character that is valid in a
shell command.

All Mistral control messages start on a new line, begin with a single
colon (“:” - ASCII 0x3A) and end with a single colon followed by a
single new line (“:\\n” -ASCII 0x3A, 0x0A).Any additional message
parameter values are delimited with a single colon (“:” - ASCII 0x3A).

In version 3 of the plug-in API all Mistral control message identifiers
are 10 characters long but this is not guaranteed for future API
versions.

## Control Messages Sent to Plug-ins

All the following messages can be read by a plug-in from `stdin`.
Trailing new lines are omitted from the message formats below for
readability.

### PGNSUPVRSN

|                   |                                                          |
| :---------------- | :------------------------------------------------------- |
| Sent to Plug-in   | OUTPUT, UPDATE                                           |
| Response Required | [PGNVERSION](#pgnversion) or [PGNSHUTDWN](#pgnshutdwn-1) |
| Message Format    | :PGNSUPVRSN:\<min\>:\<max\>:                             |

This message will be sent by the Mistral application immediately after
starting a plug-in. The message will be followed by two integer values
that indicate the minimum and maximum plug-in API version supported by
the version of Mistral in use.

The plug-in must respond immediately with either a
[PGNVERSION](#pgnversion) message with the version of the API it wishes
to use (see section [5.2.1](#pgnversion)) or with a
[PGNSHUTDWN](#pgnshutdwn-1) message if it is unable to use any of the
supported versions (see section [5.2.4](#pgnshutdwn-1)).

### PGNINTRVAL

|                   |                          |
| :---------------- | :----------------------- |
| Sent to Plug-in   | UPDATE                   |
| Response Required | None                     |
| Message Format    | :PGNINTRVAL:\<seconds\>: |

This message will be sent by the Mistral application, to the update
plug-in only, after a valid [PGNVERSION](#pgnversion) message has been
received (see section [5.2.1](#pgnversion)). The message will be
followed by a single integer value that indicates the period in seconds
that the Mistral application will wait between calls to the plug-in.

No response is required, this is an informational message only.

### PGNDATASRT

|                   |                        |
| :---------------- | :--------------------- |
| Sent to Plug-in   | OUTPUT, UPDATE         |
| Response Required | None                   |
| Message Format    | :PGNDATASRT:\<count\>: |

This message will be sent by the Mistral application indicating the
start of a data payload block. The message will be followed by a single
integer value that specifies the count of data payload blocks the
Mistral application has attempted to send including this one. The same
count will be appended to the corresponding [PGNDATAEND](#pgndataend)
message (see section [5.1.4](#pgndataend)) indicating the end of the
data block.

Once this message has been received all subsequent data read from
`stdin` must be considered as input data i.e. contract information in
the update plug-in and logging information in the output plug-in.

The only valid control messages that can be received during receipt of a
data payload block are [PGNDATAEND](#pgndataend) and
[PGNSHUTDWN](#pgnshutdwn) (see sections [5.1.4](#pgndataend) and
[5.1.5](#pgnshutdwn) respectively). Any other control message must be
treated as an error in the data payload.

### PGNDATAEND

|                   |                                                      |
| :---------------- | :--------------------------------------------------- |
| Sent to Plug-in   | OUTPUT, UPDATE                                       |
| Response Required | Optional - [PGNDATASRT](#pgndatasrt-1) (UPDATE only) |
| Message Format    | :PGNDATAEND:\<count\>:                               |

This message will be sent by the Mistral application after sending a
data payload block is complete. The data payload must be terminated with
a single new line character (“\\n” - ASCII 0x0A) as control messages
occur at the start of a line.

The message will be followed by a single integer value that specifies
the count of data payload blocks the Mistral application has attempted
to send including this one. The count must match the last count value
seen in a corresponding [PGNDATASRT](#pgndatasrt) message (see section
[5.1.3](#pgndatasrt)). If the count does not match or a
[PGNDATASRT](#pgndatasrt) message has not been seen this must be treated
as an error in the data payload.

### PGNSHUTDWN

|                   |                |
| :---------------- | :------------- |
| Sent to Plug-in   | OUTPUT, UPDATE |
| Response Required | None           |
| Message Format    | :PGNSHUTDWN:   |

This message will be sent by the Mistral application when it is shutting
down. The plug-in must immediately perform a clean exit. The Mistral
application will not exit until the plug-in has terminated to avoid data
loss. Any further messages sent by the plug-in will be ignored.

## Control Messages Sent by Plug-ins

All the following messages must be written to `stdout` by a plug-in.
Trailing new lines are omitted from the message formats below for
readability.

### PGNVERSION

|                   |                          |
| :---------------- | :----------------------- |
| Sent by Plug-in   | OUTPUT, UPDATE           |
| Response Required | None                     |
| Message Format    | :PGNVERSION:\<version\>: |

This message must be sent immediately after receipt of a
[PGNSUPVRSN](#pgnsupvrsn) message (see section [5.1.1](#pgnsupvrsn)).
The message must be followed by a single integer value that lies between
the minimum and maximum API versions received indicating the version of
the API the plug-in wishes to use.

If the plug-in is not able to use any of the advertised API versions it
must send a [PGNSHUTDWN](#pgnshutdwn-1) message instead (see section
[5.2.4](#pgnshutdwn-1)).

### PGNDATASRT

|                   |                        |
| :---------------- | :--------------------- |
| Sent by Plug-in   | UPDATE                 |
| Response Required | None                   |
| Message Format    | :PGNDATASRT:\<count\>: |

This message can be sent after receipt of a [PGNDATAEND](#pgndataend)
message (see section [5.1.4](#pgndataend)) but is not mandatory. The
message indicates the start of a data payload block and must be followed
by a single integer value that matches the data payload block count
received in the last [PGNDATAEND](#pgndataend) message seen.

Once this message has been sent all subsequent data written to `stdout`
will be considered as input data i.e. contract information.

The only valid control messages that can be sent during transmission of
a data block are [PGNDATAEND](#pgndataend-1) and
[PGNSHUTDWN](#pgnshutdwn-1) (see sections [5.2.3](#pgndataend-1) and
[5.2.4](#pgnshutdwn-1) respectively). Any other control message will be
treated as an error in the data payload.

### PGNDATAEND

|                   |                        |
| :---------------- | :--------------------- |
| Sent by Plug-in   | UPDATE                 |
| Response Required | None                   |
| Message Format    | :PGNDATAEND:\<count\>: |

This message must be sent after sending a data payload block is
complete. The data payload must be terminated with a single new line
character (“\\n” - ASCII 0x0A) as control messages occur at the start of
a line.

The message must be followed by a single integer value that matches the
count sent in the corresponding [PGNDATASRT](#pgndatasrt-1) message (see
section [5.2.2](#pgndatasrt-1)). If the count does not match or the
Mistral application has sent a further data payload block, Mistral will
treat this as an error in the data payload.

### PGNSHUTDWN

|                   |                |
| :---------------- | :------------- |
| Sent by Plug-in   | OUTPUT, UPDATE |
| Response Required | None           |
| Message Format    | :PGNSHUTDWN:   |

This message must be sent by a plug-in before a clean exit that is not a
response to a [PGNSHUTDWN](#pgnshutdwn) message sent by Mistral to the
plug-in (see section [5.1.5](#pgnshutdwn)).

If this message is sent during a data payload block (i.e. after
transmission of a [PGNDATASRT](#pgndatasrt-1) message but before
transmission of the corresponding [PGNDATAEND](#pgndataend-1) message –
see sections [5.2.2](#pgndatasrt-1) and [5.2.3](#pgndataend-1)
respectively) Mistral will discard any data received in the payload
block in progress.

## Data Payload Blocks

### Output Plug-in

The data payload block sent to the output plug-in will consist of log
messages in the following format.

\<SCOPE\>\#\<CONTRACT-TYPE\>\#\<LOG-MESSAGE\>

Where:

`<SCOPE>`is either “`local`” or “`global`” identifying the scope of the
contract that defined the logged rule violation.

`<CONTRACT-TYPE>`is either “`monitor`” or “`throttle`” identifying the
type of the contract that defined the logged rule violation.

`<LOG-MESSAGE>`is the log message in the format defined in the Mistral
User Guide for the corresponding `CONTRACT-TYPE`. Version 5 of the API
will send log messages in the format introduced in Mistral version
2.11.2.

Note that, unlike control messages, data block lines do not start with a
single colon (“:” - ASCII 0x3A). A single hash (“\#” - ASCII 0x23) is
used to separate each of the fields. Care should be taken when
processing log lines as a hash is a valid character in both paths and
commands and hence may also appear in the `LOG-MESSAGE` field.

### Update Plug-ins

The data payload block sent to and by the update plug-in will consist of
valid version 2 format Mistral contracts, introduced in release 2.9.1,
as defined in the Mistral User Guide.

Mistral will send both the local monitoring and throttle contracts in
the same data payload block. Contracts will have no identifying markers
other than a valid monitoring or throttling contract header line. It is
the plug-in's responsibility to identify the start of each contract.

If one or both contract's headers are missing in a data payload block
sent by Mistral then it can be assumed that a local contract of this
type is not currently defined. This does not prevent the plug-in from
defining such a contract.

If either contract's header is missing in a data payload block sent by
the plug-in Mistral will assume that no updates are required to any
existing contract of the missing type e.g. if the throttling contract
header is missing any existing local throttling contract will not be
updated.

The contracts sent by the plug-in will replace any existing contract in
its entirety.

## Plug-in Communication Examples

### Update Plug-in

The following table contains a simple example of communication between
the Mistral application and an update plug-in.

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;">:PGNSUPVRSN:2:3:</td>
<td style="text-align: left;"></td>
<td style="text-align: left;">Mistral advertises that the API versions 2 and 3 are supported.</td>
</tr>
<tr class="even">
<td style="text-align: left;"></td>
<td style="text-align: left;">:PGNVERSION:2:</td>
<td style="text-align: left;">The update plug-in responds indicating that it will use API version 2.</td>
</tr>
<tr class="odd">
<td style="text-align: left;">:PGNINTRVAL:600:</td>
<td style="text-align: left;"></td>
<td style="text-align: left;">Mistral informs the plug-in that it will be passed the current contract set every 10 minutes.</td>
</tr>
<tr class="even">
<td style="text-align: left;">:PGNDATASRT:1:</td>
<td style="text-align: left;"></td>
<td style="text-align: left;">Mistral starts sending data.</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><p>2,monitortimeframe,2s</p>
<p>MonRule1,…</p>
<p>2,throttletimeframe,1s</p>
<p>ThrRule1,...</p></td>
<td style="text-align: left;"></td>
<td style="text-align: left;">Mistral sends the current contract set.</td>
</tr>
<tr class="even">
<td style="text-align: left;">:PGNDATAEND:1:</td>
<td style="text-align: left;"></td>
<td style="text-align: left;">Mistral has finished sending data.</td>
</tr>
<tr class="odd">
<td style="text-align: left;"></td>
<td style="text-align: left;">:PGNDATASRT:1:</td>
<td style="text-align: left;">The update plug-in chooses to respond with an updated set of contracts and starts sending data.</td>
</tr>
<tr class="even">
<td style="text-align: left;"></td>
<td style="text-align: left;"><p>2,monitortimeframe,1s</p>
<p>NewMonRule1,…</p>
<p>2,throttletimeframe,1s</p>
<p>NewThrRule1,...</p></td>
<td style="text-align: left;">The update plug-in sends back the altered contracts.</td>
</tr>
<tr class="odd">
<td style="text-align: left;"></td>
<td style="text-align: left;">:PGNDATAEND:1:</td>
<td style="text-align: left;">The update plug-in has finished sending data.</td>
</tr>
<tr class="even">
<td style="text-align: left;"></td>
<td style="text-align: left;">:PGNSHUTDWN:</td>
<td style="text-align: left;">The update plug-in is exiting.</td>
</tr>
</tbody>
</table>

### Output Plug-in

The following table contains a simple example of communication between
the Mistral application and an output plug-in.

|                  |                |                                                                                                     |
| :--------------- | :------------- | :-------------------------------------------------------------------------------------------------- |
| :PGNSUPVRSN:2:3: |                | Mistral advertises that the API versions 2 and 3 are supported.                                     |
|                  | :PGNVERSION:2: | The output plug-in responds indicating that it will use API version 3.                              |
| :PGNDATASRT:1:   |                | Mistral starts sending data.                                                                        |
| \<LOG MESSAGES\> |                | Mistral sends the current buffer of log alerts                                                      |
| :PGNDATAEND:1:   |                | Mistral has finished sending data.                                                                  |
|                  | \---           | The output plug-in chooses to remain active waiting for the next set of alerts. No message is sent. |
| :PGNDATASRT:2:   |                | Mistral starts sending data.                                                                        |
| \<LOG MESSAGES\> |                | Mistral sends the current buffer of log alerts                                                      |
| :PGNDATAEND:2:   |                | Mistral has finished sending data.                                                                  |
|                  | \---           | The output plug-in chooses to remain active waiting for the next set of alerts. No message is sent. |
| :PGNDATASRT:3:   |                | Mistral starts sending data.                                                                        |
| \<LOG MESSAGES\> |                | Mistral sends the current buffer of log alerts                                                      |
| :PGNDATAEND:3:   |                | Mistral has finished sending data.                                                                  |
| :PGNSHUTDWN:     |                | Mistral is exiting, the plug-in must finish any processing in progress and exit.                    |
