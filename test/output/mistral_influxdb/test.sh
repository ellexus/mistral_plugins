#!/bin/bash

. ../../plugin_test_utilities.sh

function check_db() {
    local db_name=$1
    db_test=$(curl -s -GET "$influx_protocol://$influx_host:$influx_port/query" \
              -u $influx_auth --data-urlencode "db=_internal" --data-urlencode \
              "q=SHOW DATABASES")

    return $(echo "$db_test" | grep -c "$db_name")
}
# Set up influxdb defaults but allow them to be overridden

influx_protocol=${influx_protocol:-http}
influx_host=${influx_host:-sql.camb.ellexus.com}
influx_port=${influx_port:-8086}
influx_db=${influx_db:-mistral$(date +%Y%m%d%H%M%S)}

influx_user=${influx_user-ellexus}
influx_pass=${influx_pass-ellexus}
influx_auth=${influx_user}:${influx_pass}

curl_cmd=$(which curl 2>/dev/null)
if [ -z "$curl_cmd" ]; then
    logerr "curl not found"
    exit 1
fi

# Check the database does not already exist
check_db "$influx_db"

if [ $? -ge 1 ]; then
    logerr "Specified database already exists - '$influx_db'"
    exit 2
fi

# Create the test database
curl -s -POST "$influx_protocol://$influx_host:$influx_port/query" \
    -u $influx_auth --data-urlencode "db=_internal" --data-urlencode \
    "q=CREATE DATABASE $influx_db" >/dev/null 2>&1

check_db "$influx_db"

if [ $? -ne 1 ]; then
    logerr "Unable to create test database - '$influx_db'"
    exit 3
fi

# Run a standard test
if [ "$influx_protocol" = "https" ]; then
    secure="-s"
fi

run_test -d "$influx_db" -h "$influx_host" -P "$influx_port" $secure -u \
    "$influx_user" -p "$influx_pass"

# Get the results
curl -s -GET "$influx_protocol://$influx_host:$influx_port/query?pretty=true" \
    -u $influx_auth --data-urlencode "db=$influx_db" --data-urlencode \
    "q=SELECT * FROM bandwidth" > $results_dir/results.txt

# curl/influxdb do not always append a newline to the end of the output, do it
# manually if needed.

if [ $(tail -c1 "$results_dir/results.txt" | wc -l) -eq 0 ]; then
    echo >> $results_dir/results.txt
fi

check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # Delete the test database regardless of test status
    curl -s -POST "$influx_protocol://$influx_host:$influx_port/query" \
        -u $influx_auth --data-urlencode "db=_internal" --data-urlencode \
        "q=DROP DATABASE $influx_db" >/dev/null 2>&1

    check_db "$influx_db"

    if [ $? -ge 1 ]; then
        logerr "Unable to remove test database - '$influx_db'"
        exit 4
    fi
fi
