#!/bin/bash

. ../../plugin_test_utilities.sh

# Set up influxdb defaults but allow them to be overridden

influx_protocol=${influx_protocol:-http}
influx_host=${influx_host:-localhost}
influx_port=${influx_port:-8086}
influx_db=${influx_db:-mistral$(date +%Y%m%d%H%M%S)}

influx_auth=${influx_user}:${influx_pass}

curl_cmd=$(which curl 2>/dev/null)
if [ -z "$curl_cmd" ]; then
    logerr "curl not found"
    exit 1
fi

# Check the database does not already exist
db_test=$(curl -s -GET "$influx_protocol://$influx_host:$influx_port/query" \
          -u $influx_auth --data-urlencode "db=_internal" --data-urlencode \
          "q=SHOW DATABASES")

if [ $(echo "$db_test" | grep -c "$influx_db") -ge 1 ]; then
    echo "Specified database already exists - '$influx_db'"
    exit 2
fi

# Create the test database
curl -s -POST "$influx_protocol://$influx_host:$influx_port/query" \
    $influx_auth --data-urlencode "db=_internal" --data-urlencode \
    "q=CREATE DATABASE $influx_db" >/dev/null 2>&1

db_test=$(curl -s -GET "$influx_protocol://$influx_host:$influx_port/query" \
          -u $influx_auth --data-urlencode "db=_internal" --data-urlencode \
          "q=SHOW DATABASES")

if [ $(echo "$db_test" | grep -c "$influx_db") -ne 1 ]; then
    echo "Unable to create test database - '$influx_db'"
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
    $influx_auth --data-urlencode "db=$influx_db" --data-urlencode \
    "q=SELECT * FROM bandwidth" > $results_dir/results.txt

# curl/influxdb do not append a newline to the end of the output, do it manually
echo >> $results_dir/results.txt

check_results

if [ "$KEEP_TEST_OUTPUT" = "" ];then
    # Delete the test database regardless of test status
    curl -s -POST "$influx_protocol://$influx_host:$influx_port/query" \
        $influx_auth --data-urlencode "db=_internal" --data-urlencode \
        "q=DROP DATABASE $influx_db" >/dev/null 2>&1

    db_test=$(curl -s -GET "$influx_protocol://$influx_host:$influx_port/query"\
              -u $influx_auth --data-urlencode "db=_internal" --data-urlencode \
              "q=SHOW DATABASES")

    if [ $(echo "$db_test" | grep -c "$influx_db") -ge 1 ]; then
        echo "Unable to remove test database - '$influx_db'"
        exit 4
    fi
fi
