#!/bin/bash
scriptname=$(basename "$0")

# usage
#
# Output a usage message on stderr
#
# Parameters:
#  $1 An additional message to output prior to the usage message.
#
# Returns:
#  Nothing, the function exits the script
function usage() {
    >&2 echo $1
    >&2 cat << EOF 
Usage: $scriptname [OPTION...]

Return a list of unique job ids and rule labels that were violated.

By default $scriptname will connect to http://localhost:8086/ using database
"mistral" and look for violations of bandwidth rules.

OPTIONS:
  -?, --help        Show this help page.
  -c, --call-type   Limit the query to the provided call type. Must be one of 
                    accept, access, connect, create, delete, fschange, glob,
                    open, read, seek or write.
  -d, --database    Name of the database to use.
  -f  --filename    Limit the query to the provided filename. Used as a regular
                    expression in a sub-string match.
  -g, --group       Limit the query to the provided job group ID.
  -h, --host        Hostname to use for connection.
  -i, --pid         Limit the query to the provided PID.
  -j, --job         Limit the query to the provided job ID.
  -m, --measurement Type of rule to check. Must be one of bandwidth, count,
                    seek-distance, min-latency, max-latency, mean-latency or
                    total-latency.
  -n, --port        Port number to use for connection.
  -o, --command     Limit the query to the provided command. Used as a regular
                    expression in a sub-string match.
  -p, --password    Filename that contains the connection password. Implies -u.
  -q, --quiet       Suppress informative messages.
  -r, --rule        Limit the query to the provided rule label.
  -s, --ssl         Connect via https.
  -t, --time        Limit the time period to check for rule violations. Must be
                    specified in the format n[m|h] where 'n' is an integer which
                    must be immediately followed by one of 'm' or 'h' indicating
                    'minutes' or 'hours' respectively. For example five minutes
                    would be specified as "5m".
  -u, --user        Username to use for connection. If -p is not specified the
                    user will be prompted for the password.
EOF
    exit -1
}

# main
#
# Main script body. Parses options, connects to InfluxDB server and formats the
# output
#
# Parameters:
#  Command line parameters
#
# Returns:
#  0 on success
#  Non-zero on error
function main() {
    local auth=
    local database=mistral
    local host=localhost
    local measurement=bandwidth
    local calltype=
    local cmd=
    local filename=
    local num_time=
    local password=
    local password_set=0
    local pid=
    local port=8086
    local protocol=http
    local query_time=
    local quiet=0
    local timeunit=minutes
    local username=

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -\? | --help)
                usage ""
                ;;
            -c | --call-type)
                calltype=$2
                case "$calltype" in
                    accept | access | connect | create | delete | fschange | \
                    glob | open | read | seek | write)
                        ;;
                    *)
                        usage "Invalid argument \"$2\" for option $1"
                        ;;
                esac
                shift
                ;;
            --call-type=*)
                calltype=${1#--call-type=}
                case "$calltype" in
                    accept | access | connect | create | delete | fschange | \
                    glob | open | read | seek | write)
                        ;;
                    *)
                        usage "Invalid argument \"${1#--call-type=}\" for option ${1%%=*}"
                        ;;
                esac
                ;;
            -o | --command)
                cmd=$2
                shift
                ;;
            --command=*)
                cmd=${1#--command=}
                ;;
            -d | --database)
                database=$2
                shift
                ;;
            --database=*)
                database=${1#--database=}
                ;;
            -f | --filename)
                filename=$2
                shift
                ;;
            --filename=*)
                filename=${1#--filename=}
                ;;
            -g | --group)
                group_id=$2
                shift
                ;;
            --group=*)
                group_id=${1#--group=}
                ;;
            -h | --host)
                host=$2
                shift
                ;;
            --host=*)
                host=${1#--host=}
                ;;
            -j | --job)
                job_id=$2
                shift
                ;;
            --job=*)
                job_id=${1#--job=}
                ;;
            -m | --measurement)
                measurement=$2
                case "$measurement" in
                    bandwidth | count | seek-distance | min-latency | \
                    max-latency | mean-latency | total-latency)
                        ;;
                    *)
                        usage "Invalid argument \"$2\" for option $1"
                        ;;
                esac
                shift
                ;;
            --measurement=*)
                measurement=${1#--measurement=}
                case "$measurement" in
                    bandwidth | count | seek-distance | min-latency | \
                    max-latency | mean-latency | total-latency)
                        ;;
                    *)
                        usage "Invalid argument \"${1#--measurement=}\" for option ${1%%=*}"
                        ;;
                esac
                ;;
            -p | --password)
                password=$(cat "$2" 2>&1)

                if [[ "$?" -ne 0 ]]; then
                    usage "Error reading password file \"$2\" - ${password#cat: }"
                fi
                password_set=1
                shift
                ;;
            --password=*)
                password=$(cat "${1#--password=}" 2>&1)

                if [[ "$?" -ne 0 ]]; then
                    usage "Error reading password file \"${1#--password=}\" - ${password#cat: }"
                fi
                password_set=1
                ;;
            -i | --pid)
                pid=$2
                if [[ "${pid//[0-9]/}" != "" ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi
                shift
                ;;
            --pid=*)
                pid=${1#--pid=}
                if [[ "${pid//[0-9]/}" != "" ]]; then
                    usage "Invalid argument \"${1#--pid=}\" for option ${1%%=*}"
                fi
                ;;
            -n | --port)
                port=$2

                if [[ "$port" -ne "${port%%[^0-9]*}" && "$port" -ne "${port##[^0-9]}" ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi
                shift
                ;;
            --port=*)
                port=${1#--port=}

                if [[ "$port" -ne "${port%%[^0-9]*}" && "$port" -ne "${port##[^0-9]}" ]]; then
                    usage "Invalid argument \"${1#--port=}\" for option ${1%%=*}"
                fi
                ;;
            -q | --quiet)
                quiet=1
                ;;
            -r | --rule)
                rule_name=$2
                # Check the rule name does not contain invalid characters (which
                # also helps protect against SQL injection as we don't allow
                # anything special)
                if [[ "${rule_name//[0-9a-zA-Z_-]/}" != "" ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi
                shift
                ;;
            --rule=*)
                rule_name=${1#--rule=}
                if [[ "${rule_name//[0-9a-zA-Z_-]/}" != "" ]]; then
                    usage "Invalid argument \"${1#--rule=}\" for option ${1%%=*}"
                fi
                ;;
            -s | --ssl)
                protocol=https
                ;;
            -t | --time)
                str_time=$2
                num_time=${str_time%%[^0-9]*}
                num_time=${num_time:-0}
                suffix=${str_time##$num_time}

                if [[ $num_time -eq 0 ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi

                if [[ "$suffix" != "h" && "$suffix" != "m" ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi

                if [[ "$suffix" = "h" ]]; then
                    query_time=$((num_time * 60))
                    timeunit=hours
                else
                    query_time=$num_time
                fi
                shift
                ;;
            --time=*)
                str_time=${1#--time=}
                num_time=${str_time%%[^0-9]*}
                num_time=${num_time:-0}
                suffix=${str_time##$num_time}

                if [[ $num_time -eq 0 ]]; then
                    usage "Invalid argument \"${1#--time=}\" for option ${1%%=*}"
                fi

                if [[ "$suffix" != "h" && "$suffix" != "m" ]]; then
                    usage "Invalid argument \"${1#--time=}\" for option ${1%%=*}"
                fi

                if [[ "$suffix" = "h" ]]; then
                    query_time=$((num_time * 60))
                    timeunit=hours
                else
                    query_time=$num_time
                fi
                ;;
            -u | --user)
                username="$2"
                shift
                ;;
            --user=*)
                username="${1#--user=}"
                ;;
            *)
                usage "Invalid option ${1}"
                ;;
        esac
        shift
    done

    if [[ "$username" != "" ]]; then
        auth=$username
    fi

    if [[ $password_set -eq 1 ]]; then
        auth=$auth:$password
    fi

    if [[ "$auth" != "" ]]; then
        auth="-u $auth"
    fi

    local where=
    local and=
    # Build up where clause
    if [[ "$rule_name" != "" ]]; then
        where="\"label\" = '$rule_name'"
        and="AND"
    fi

    if [[ "$job_id" != "" ]]; then
        # Mistral logging currently has a bug where LSF job id's always have an
        # array index appended so we need to explicitly check for [0]
        where="$where $and (\"job-id\" = '${job_id//\'/\\\'}' \
               OR \"job-id\" = '${job_id//\'/\\\'}[0]')"
        and="AND"
    fi

    if [[ "$group_id" != "" ]]; then
        # As above we need to work around the logging bug
        where="$where $and (\"job-group\" = '${group_id//\'/\\\'}' \
               OR \"job-group\" = '${group_id//\'/\\\'}[0]')"
        and="AND"
    fi

    if [[ "$pid" -gt 0 ]]; then
        where="$where $and \"pid\" = '${pid}'"
        and="AND"
    fi

    if [[ "$query_time" -gt 0 ]]; then
        where="$where $and \"time\" > now() - ${query_time}m"
        and="AND"
    fi

    if [[ "$calltype" != "" ]]; then
        where="$where $and \"calltype\" = '${calltype//\'/\\\'}'"
        and="AND"
    fi

    if [[ "$cmd" != "" ]]; then
        where="$where $and \"command\" =~ /${cmd//\//\\\/}/"
        and="AND"
    fi

    if [[ "$filename" != "" ]]; then
        where="$where $and \"file\" =~ /${filename//\//\\\/}/"
        and="AND"
    fi

    if [[ "$where" = "" ]]; then
        usage "At least one look up value must be provided"
    fi

    # Unfortunately InfluxDB's implementation of DISTINCT seems unreliable. We
    # have to select everything and run the output through "sort -u".
    outval=$(curl -s $auth -GET $protocol://$host:$port/query --data-urlencode \
        "db=${database//\"/\\\"}" --data-urlencode \
        "q=SELECT \"job-id\", \"label\", \"value\" \
        FROM \"$measurement\" \
        WHERE $where")
    retval=$?

    if [[ "$retval" -ne 0 ]]; then
        >&2 echo Error, curl exited with error code $retval
        exit $retval
    elif [[ "$outval" = "" ]]; then
        >&2 echo Error, no data returned for query
        exit -2
    elif [[ "${outval:0:9}" = '{"error":' || 
            "${outval:0:21}" = '{"results":[{"error":' ]]; then
        >&2 echo Error, InfluxDB query failed:
        echo "${outval}" | >&2 sed -e 's/.*error":\([^}]*\)}.*/  \1/'
        exit -3
    elif [[ "$outval" = '{"results":[{}]}' ]]; then
        if [[ "$quiet" -ne 1 ]]; then
            echo "No rules found that were violated which match the specified" \
                 "criteria."
        fi
        exit 0
    fi

    if [[ "$quiet" -ne 1 ]]; then
        echo "Rules that were violated that match the specified criteria:"
        echo
    fi
    echo $outval | sed -e 's/\],\[/\n/g; s/^.*values//' | cut -s -d, -f 2,3 | \
        sed -e 's/\"//g;s/\[0\]//' | sort -u

    exit 0
}

main "${@}"
