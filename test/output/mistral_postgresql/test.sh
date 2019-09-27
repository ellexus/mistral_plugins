#!/bin/bash

. ../../plugin_test_utilities.sh

psql_cmd=$(which psql 2>/dev/null)
if [ -z "$psql_cmd" ]; then
    logerr "psql not found"
    exit 1
fi

pgres_options="-h sql -U mistral -d mistral_log"
export PGPASSWORD=ellexus

# Create the test database
"$psql_cmd" $pgres_options < "$plugin_dir"/sql/create_mistral.sql 2&> /dev/null
"$psql_cmd" $pgres_options < ./empty_database.sql > /dev/null

if [ $? -ne 0 ]; then
    logerr "Error creating test database"
    exit 2
fi

# Set up the SQL command to fetch the results
# TODO: Setup the actual SQL commands that need to be run
sql_cmd="\copy ("
sql_cmd="$sql_cmd SELECT measure, timeframe, host, pid, cpu, command, file_name,"
sql_cmd="$sql_cmd group_id, id, mpi_rank, env_name, env_value"
sql_cmd="$sql_cmd FROM bandwidth LEFT JOIN env ON bandwidth.plugin_run_id=env.plugin_run_id"
sql_cmd="$sql_cmd ) TO '"$results_dir"/results.txt' WITH DELIMITER ',' CSV HEADER;"

# Set a custom value to be included in the output
export _test_var=MISTRAL

run_test -h sql -u mistral -p ellexus -d mistral_log --var=_test_var

# Get the results
"$psql_cmd" $pgres_options -c "$sql_cmd" > /dev/null

check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # Delete the test database regardless of test status
    "$psql_cmd" $pgres_options < ./empty_database.sql > /dev/null

    if [ $? -ne 0 ]; then
        logerr "Unable to remove test database"
        exit 3
    fi
fi
