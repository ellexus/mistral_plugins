#!/bin/bash
scriptname=$(basename "$0")
scriptdir=$(readlink -f "$0" | xargs dirname)

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

Create an index template for indexes that will be created by the Mistal
Elasticsearch plug-in.

By default $scriptname will connect to http://localhost:9200/ using index
"mistral".

OPTIONS:
  -?, --help        Show this help page.
  -i, --index       Basename of the index for this template.
  -h, --host        Hostname to use for connection.
  -n, --port        Port number to use for connection.
  -p, --password    Filename that contains the connection password. Implies -u.
  -s, --ssl         Connect via https.
  -u, --user        Username to use for connection. If -p is not specified the
                    user will be prompted for the password.
EOF
    exit 1
}

# main
#
# Main script body. Parses options, connects to Elasticsearch server and creates
# the index mappings template.
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
    local password=
    local password_set=0
    local pid=
    local port=9200
    local protocol=http
    local username=

    curl_cmd=$(which curl 2>/dev/null)
    if [ -z "$curl_cmd" ]; then
        >&2 echo Error, could not find curl
        exit 1
    fi

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -\? | --help)
                usage ""
                ;;
            -i | --index)
                database=$2
                shift
                ;;
            --index=*)
                database=${1#--database=}
                ;;
            -h | --host)
                host=$2
                shift
                ;;
            --host=*)
                host=${1#--host=}
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
            -s | --ssl)
                protocol=https
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

    outval=$($curl_cmd -s $auth -XGET $protocol://$host:$port)
    retval=$?

    if [[ "$retval" -ne 0 ]]; then
        >&2 echo Error, could not get Elasticsearch version. Curl exited with error code $retval
        exit $retval
    elif [[ "${outval:0:9}" = '{"error":' ]]; then
        >&2 echo Error, could not get Elasticsearch version. ElasticSearch query failed:
        echo "$outval" | >&2 sed -e 's/.*reason":\([^}]*\)}.*/  \1/;s/,/\n  /g'
        exit 2
    else
        ver=$(echo "$outval" | grep number | sed -e 's/.*number" : "\([0-9]\+\).*/\1/g')
    fi

    outval=$($curl_cmd -s $auth -XPUT -H "Content-Type: application/json" \
        $protocol://$host:$port/_template/$database -d \
        "$(sed -e "s/mistral/$database/" $scriptdir/mappings_$ver.x.json)" \
        )
    retval=$?

    if [[ "$retval" -ne 0 ]]; then
        >&2 echo Error, curl exited with error code $retval
        exit $retval
    elif [[ "${outval:0:9}" = '{"error":' ]]; then
        >&2 echo Error, ElasticSearch query failed:
        echo "$outval" | >&2 sed -e 's/.*reason":\([^}]*\)}.*/  \1/;s/,/\n  /g'
        exit 2
    fi

    echo Index mappings for \"$database\" created successfully
    exit 0
}

main "${@}"
