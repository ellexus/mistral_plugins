#!/bin/bash

. ../../plugin_test_utilities.sh

psql_cmd=$(which psql 2>/dev/null)
if [ -z "$psql_cmd" ]; then
    logerr "psql not found"
    exit 1
fi

pgres_options="-h 10.33.100.84 -U mistral -d mistral_log"
export PGPASSWORD=ellexus

# Create the test database
echo "$psql_cmd" $pgres_options < "$plugin_dir"/sql/create_mistral.sql

if [ $? -ne 0 ]; then
    logerr "Error creating test database"
    exit 2
fi

# Set up the SQL command to fetch the results
# TODO: Setup the actual SQL commands that need to be run
sql_cmd="\copy ("
sql_cmd="$sql_cmd SELECT * FROM counts"
sql_cmd="$sql_cmd ) TO '"$results_dir"/results.txt' WITH DELIMITER ',' CSV HEADER;"

# Set a custom value to be included in the output
export _test_var=MISTRAL

run_test -h localhost -u mistral -p ellexus -d mistral_log --var=_test_var

# Get the results
"$psql_cmd" $pgres_options -c "$sql_cmd"

check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # Delete the test database regardless of test status
    # TODO: Remove the echo on thi
    echo "$psql_cmd" $pgres_options -c \
        "DROP DATABASE IF EXISTS mistral_log;" >/dev/null 2>&1

    if [ $? -ne 0 ]; then
        logerr "Unable to remove test database"
        exit 3
    fi
fi
