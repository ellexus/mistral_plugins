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
Usage: $scriptname [OPTION...] job-id

Return a list of unique rules that were violated by the job "job-id".

By default $scriptname will connect to http://localhost:8086/ using database
"mistral" and look for violations of bandwidth rules named "label" in the last
five minutes.

OPTIONS:
  -?, --help        Show this help page.
  -d, --database    Name of the database to use.
  -h, --host        Hostname to use for connection.
  -j, --job         Alternative method to set the job id to test.
  -m, --measurement Type of rule to check. Must be one of bandwidth, count,
                    seek-distance, min-latency, max-latency, mean-latency or
                    total-latency.
  -p, --password    Filename that contains the connection password. Implies -u.
  -n, --port        Port number to use for connection.
  -q, --quiet       Suppress informative messages.
  -s, --ssl         Connect via https.
  -t, --time        Time period to check for rule violations. Must be specified
                    in the format n[m|h] where 'n' is an integer which must be
                    immediately followed by one of 'm' or 'h' indicating
                    'minutes' or 'hours' respectively. For example the default
                    period of five minutes would be specified as "5m".
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
    local num_time=5
    local password=
    local password_set=0
    local port=8086
    local protocol=http
    local query_time=$num_time
    local quiet=0
    local timeunit=minutes
    local username=

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -\? | --help)
                usage ""
                ;;
            -d | --database)
                database=$2
                shift
                ;;
            --database=*)
                database=${1#--database=}
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
                ;;
            --password=*)
                password=$(cat "${1#--password=}" 2>&1)

                if [[ "$?" -ne 0 ]]; then
                    usage "Error reading password file \"${1#--password=}\" - ${password#cat: }"
                fi
                password_set=1
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
                job_id=$1
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

    # Unfortunately InfluxDB's implementation of DISTINCT seems unreliable. We
    # have to select everything and run the output through "sort -u".
    outval=$(curl -s $auth -GET $protocol://$host:$port/query --data-urlencode \
        "db=${database//\"/\\\"}" --data-urlencode \
        "q=SELECT \"label\", \"value\" \
        FROM \"$measurement\" \
        WHERE (\"job-id\" = '${job_id//\'/\\\'}' \
               OR \"job-id\" = '${job_id//\'/\\\'}[0]') \
        AND  \"time\" > now() - ${query_time}m")
    retval=$?

    if [[ "$retval" -ne 0 ]]; then
        >&2 echo Error, curl exited with error code $retval
        exit $retval
    elif [[ "$outval" = "" ]]; then
        >&2 echo Error, no data returned for query
        exit -2
    elif [[ "${outval:0:9}" = '{"error":' ]]; then
        >&2 echo Error, InfluxDB query failed:
        >&2 echo "  ${outval}"
        exit -3
    elif [[ "$outval" = '{"results":[{}]}' ]]; then
        if [[ "$quiet" -ne 1 ]]; then
            echo "No rules found that were violated by the job $job_id" \
                 "in the last $num_time $timeunit"
        fi
        exit 0
    fi

    if [[ "$quiet" -ne 1 ]]; then
        echo "Rules that were violated by the job \"$job_id\" in the last" \
            "$num_time $timeunit:"
        echo
    fi
    echo $outval | sed -e 's/\],\[/\n/g; s/^.*values//' | cut -d, -f 2 | \
        sed -e 's/\"//g;s/\[0\]//' | sort -u

    exit 0
}

main "${@}"
