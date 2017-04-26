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

By default $scriptname will connect to http://localhost:9200/ using database
"mistral" and look for violations of bandwidth rules.

OPTIONS:
  -?, --help        Show this help page.
  -c, --call-type   Limit the query to the provided call type. Must be one of 
                    accept, access, connect, create, delete, fschange, glob,
                    open, read, seek or write.
  -d, --database    Name of the database to use.
  -f  --filename    Limit the query to the provided filename. Used as a match
                    phrase on the file name field.
  -g, --group       Limit the query to the provided job group ID.
  -h, --host        Hostname to use for connection.
  -i, --pid         Limit the query to the provided PID.
  -j, --job         Limit the query to the provided job ID.
  -m, --measurement Type of rule to check. Must be one of bandwidth, count,
                    seek-distance, min-latency, max-latency, mean-latency or
                    total-latency.
  -n, --port        Port number to use for connection.
  -o, --command     Limit the query to the provided command. Used as a match
                    phrase on the command field.
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
# Main script body. Parses options, connects to Elasticsearch server and formats
# the output
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
    local port=9200
    local protocol=http
    local query_time=
    local quiet=0
    local timeunit=minutes
    local username=
    local calltypes=(accept access connect create delete fschange glob open read seek write)
    local validate=
    local ordered_calltype=
    local tempvar=

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -\? | --help)
                usage ""
                ;;
            -c | --call-type)
                calltype=$2
                validate=$calltype
                for ctype in ${calltypes[@]}; do
                    tempvar=$validate
                    validate=${validate//${ctype}/}
                    if [[ -n "$tempvar" && "$tempvar" != "$validate" ]]; then
                        ordered_calltype=${ordered_calltype}+${ctype}
                    fi
                done
                validate=${validate//+/}
                if [[ -n "$validate" ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi
                calltype=${ordered_calltype#+}
                shift
                ;;
            --call-type=*)
                calltype=${1#--call-type=}
                validate=$calltype
                for ctype in ${calltypes[@]}; do
                    tempvar=$validate
                    validate=${validate//${ctype}/}
                    if [[ -n "$tempvar" && "$tempvar" != "$validate" ]]; then
                        ordered_calltype=${ordered_calltype}+${ctype}
                    fi
                done
                validate=${validate//+/}
                if [[ -n "$validate" ]]; then
                    usage "Invalid argument \"$calltype\" for option ${1%%=*}"
                fi
                calltype=${ordered_calltype#+}
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
                time_suffix=${str_time##$num_time}

                if [[ $num_time -eq 0 ]]; then
                    usage "Invalid argument \"$2\" for option $1"
                fi

                case "$time_suffix" in
                    s | m | H | h | d | w | M | y)
                        ;;
                    *)
                        usage "Invalid argument \"$2\" for option $1"
                        ;;
                esac

                query_time=${num_time}${time_suffix}
                shift
                ;;
            --time=*)
                str_time=${1#--time=}
                num_time=${str_time%%[^0-9]*}
                num_time=${num_time:-0}
                time_suffix=${str_time##$num_time}

                if [[ $num_time -eq 0 ]]; then
                    usage "Invalid argument \"${1#--time=}\" for option ${1%%=*}"
                fi

                case "$time_suffix" in
                    s | m | H | h | d | w | M | y)
                        ;;
                    *)
                        usage "Invalid argument \"${1#--time=}\" for option ${1%%=*}"
                        ;;
                esac

                query_time=${num_time}${time_suffix}
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
    if [[ "$measurement" != "" ]]; then
        where='{"match":{"rule.measurement":"'$measurement'"}}'
        and=","
    fi

    if [[ "$rule_name" != "" ]]; then
        where="$where $and"'{"match":{"rule.label":"'$rule_name'"}}'
        and=","
    fi

    if [[ "$job_id" != "" ]]; then
        where="$where $and"'{"match":{"job.job-id":"'${job_id//\"/\\\"}'"}}'
        and=","
    fi

    if [[ "$group_id" != "" ]]; then
        where="$where $and"'{"match":{"job.job-group-id":"'${group_id//\"/\\\"}'"}}'
        and=","
    fi

    if [[ "$pid" -gt 0 ]]; then
        where="$where $and"'{"match":{"process.pid":"'${pid}'"}}'
        and=","
    fi

    if [[ "$num_time" -gt 0 ]]; then
        where="$where $and"'{"range":{"@timestamp":{"gte":"now-'${query_time}'"}}}'
        and=","
    fi

    if [[ "$calltype" != "" ]]; then
        where="$where $and"'{"match_phrase":{"rule.calltype":"'$calltype'"}}'
        and=","
    fi

    if [[ "$cmd" != "" ]]; then
        local escaped_cmd=${cmd//\\/\\\\}
        escaped_cmd=${escaped_cmd//\"/\\\"}
        where="$where $and"'{"match_phrase":{"process.command":"'$escaped_cmd'"}}'
        and=","
    fi

    if [[ "$filename" != "" ]]; then
        local escaped_file=${filename//\\/\\\\}
        escaped_file=${escaped_file//\"/\\\"}
        where="$where $and"'{"match_phrase":{"process.file":"'$escaped_file'"}}'
        and=","
    fi

    if [[ "$where" = "" ]]; then
        usage "At least one look up value must be provided"
    fi

    local query="{\"size\":0,\"query\":{\"bool\":{\"must\":[${where}]}}"
    query=$query",\"aggs\":{\"job-ids\":{\"terms\":{\"field\":"
    query=$query"\"job.job-group-id.keyword\"},\"aggs\":{\"rule\":{\"terms\":"
    query=$query"{\"field\":\"rule.label.keyword\"}}}}}}"

    outval=$(curl -s $auth $protocol://$host:$port/$database/_search \
             -H "Content-Type: application/json" --data "${query}")
    retval=$?

    if [[ "$retval" -ne 0 ]]; then
        >&2 echo Error, curl exited with error code $retval
        exit $retval
    elif [[ "$outval" = "" ]]; then
        >&2 echo Error, no data returned for query
        exit -2
    fi

    local timedout=$(echo $outval | sed -e 's/.*timed_out":\([^,]*\).*/\1/')
    local successful=$(echo $outval | sed -e 's/.*successful":\([0-9]*\).*/\1/')
    local failed=$(echo $outval | sed -e 's/.*failed":\([0-9]*\).*/\1/')
    local hits=$(echo $outval | sed -e 's/.*hits":{"total":\([0-9]*\).*/\1/')

    if [[ "$timedout" = "true" ]]; then
        >&2 echo Error, query timed out
        exit -3
    elif [[ "$successful" -gt 0 && "failed" -gt 0 ]]; then
        >&2 echo Warning, some shards failed - results may be incomplete
    elif [[ "$successful" -eq 0 && "failed" -gt 0 ]]; then
        >&2 echo Error, query failed on all shards
        exit -3
    fi

    if [[ "$hits" -eq 0 ]]; then
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
    echo $outval | sed -e 's/},{"key"/},\n{"key"/g' | \
        sed -e 's/.*{"key":"\([^"]*\).*{"key":"\([^"]*\).*/\1\t\2/'

    exit 0
}

main "${@}"
