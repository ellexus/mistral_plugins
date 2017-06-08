#!/bin/bash

. ../../plugin_test_utilities.sh

mysql_admin_parameters=$script_dir/admin_login.cnf
mysql_parameters=$script_dir/plugin_login.cnf

mysql_cmd=$(which mysql 2>/dev/null)
if [ -z "$mysql_cmd" ]; then
    logerr "mysql not found"
    exit 1
fi

# Create the test database
"$mysql_cmd" --defaults-file="$mysql_admin_parameters" \
    < "$plugin_dir"/sql/create_mistral.sql

if [ $? -ne 0 ]; then
    logerr "Error creating test database"
    exit 2
fi

# Set up the SQL command to fetch the results
sql_cmd="SELECT scope, type, time_stamp, label, violation_path, call_type,"
sql_cmd="$sql_cmd measurement, size_range, threshold, observed, host, pid, cpu,"
sql_cmd="$sql_cmd command, file_name, group_id, id, mpi_rank, env_name,"
sql_cmd="$sql_cmd env_value FROM log_01 a, rule_details b, env_01 c WHERE "
sql_cmd="$sql_cmd b.rule_id = a.rule_id AND a.plugin_run_id = c.plugin_run_id"
sql_cmd="$sql_cmd ORDER BY log_id ASC, env_name ASC"

# Set a custom value to be included in the output
export _test_var=MISTRAL

run_test --defaults-file="$mysql_parameters" --var=_test_var

# Get the results
"$mysql_cmd" --defaults-file="$mysql_parameters" -ss -e "$sql_cmd" \
    >> $results_dir/results.txt

check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # Delete the test database regardless of test status
    "$mysql_cmd" --defaults-file="$mysql_parameters" -ss -e \
        "DROP DATABASE IF EXISTS mistral_log;" >/dev/null 2>&1

    if [ $? -ne 0 ]; then
        logerr "Unable to remove test database"
        exit 3
    fi
fi
