#!/bin/bash

. ../../plugin_test_utilities.sh

search_result=
sid=

function create_search() {
    local search=$1
    local test_det=$2
    sid=$(curl -k -s -u $splunk_auth --fail \
          "$splunk_protocol://$splunk_host:$splunkd_port/services/search/jobs?output_mode=json" \
          -d search="$search" | sed 's/{"sid":"\(.*\)"}/\1/')
    ret=$?
    if [[ $ret -ne 0 ]]; then
        errmsg="Unable to create search $test_det - curl error: $ret"
        if [[ -e "$summary_file" ]]; then
            logerr "$errmsg"
        else
            >&2 echo "$errmsg"
        fi
    fi
    return $ret
}

function get_search_results() {
    local mode=$1
    local test_det=$2
    search_result=$(curl -k -s -u $splunk_auth --fail \
                    $splunk_protocol://$splunk_host:$splunkd_port/services/search/jobs/$sid/results/ \
                    --get -d output_mode=$mode)
    ret=$?
    if [[ $ret -ne 0 ]]; then
        errmsg="Unable to run search $test_det - curl error: $ret"
        if [[ -e "$summary_file" ]]; then
            logerr "$errmsg"
        else
            >&2 echo "$errmsg"
        fi
    fi
    return $ret
}

# Set up splunk defaults but allow them to be overridden

splunk_protocol=${splunk_protocol:-https}
splunk_host=${splunk_host:-SQL.camb.ellexus.com}
splunk_port=${splunk_port:-8765} # For default installs this will be 8088 but
                                 # this conflicts with InfluxDB
splunkd_port=${splunkd_port:-8089}
splunk_index=${splunk_index:-main}
splunk_token=${splunk_token:-file:$PWD/hec_token}
splunk_admuser=${splunk_admuser:-admin}
splunk_admpass=${splunk_admpass:-password}

splunk_auth=$splunk_admuser:$splunk_admpass

curl_cmd=$(which curl 2>/dev/null)
if [ -z "$curl_cmd" ]; then
    logerr "curl not found"
    exit 1
fi

# Check the specified index is available
create_search "| eventcount summarize=false index=* | dedup index | fields index" \
              "to check if index '$splunk_index' exists"
if [[ $ret -ne 0 ]]; then
    exit 2
fi

get_search_results "csv" "to check if index '$splunk_index' exists"
ret=$?
if [[ $ret -ne 0 ]]; then
    exit 3
fi

if [[ 1 != $(echo "$search_result" | grep -c ^${splunk_index}$) ]]; then
    # Strictly speaking we should differentiate between missing and multiple
    # values but I believe that given the original search and the grep
    # construction >1 is not possible
    logerr "Index '$splunk_index' does not exist."
    >&2 echo "Valid indexes are:"
    >&2 echo "$search_result"
    exit 4
fi

# Set a custom value to be included in the output
export _test_var=MISTRAL_TEST_$test_timestamp

# Check that there are no records with matching test values already in the index
create_search "search environment._test_var=$_test_var" \
              "to check if matching test records already exist"
if [[ $ret -ne 0 ]]; then
    exit 5
fi

get_search_results "csv" "to check if matching test records already exist"
ret=$?
if [[ $ret -ne 0 ]]; then
    exit 6
elif [[ -n $search_result ]]; then
    logerr "Found records with environment._test_var=$_test_var"
    >&2 echo "$search_result"
    exit 7
fi

# Run a standard test
run_test -i "$splunk_index" -h "$splunk_host" -P "$splunk_port" \
         -t "$splunk_token" -v _test_var

# Get the results - it can take a little while for the data to sync so sleep for
# 10 seconds to ensure we get all results returned.
sleep 10

create_search "search environment._test_var=$_test_var | sort _time" \
              "to check if records were inserted correctly"
if [[ $ret -ne 0 ]]; then
    exit 8
fi

get_search_results "csv" "to check if records were inserted correctly"
ret=$?
if [[ $ret -ne 0 ]]; then
    exit 9
elif [[ -z $search_result ]]; then
    logerr "Found no records with environment._test_var=$_test_var"
    exit 10
else
    echo "$search_result" > "$results_dir/raw_results.txt"
fi

# There are some unique fields within the returned data we cannot exclude using
# a query option. Strip this data out using a sed script.
sed -e "s/^.*\"{\"\"rule/\"{\"\"rule/;s/$splunk_host/SPLUNKHOST/g" \
    $results_dir/raw_results.txt > $results_dir/results.txt

# curl does not always append a newline to the end of the output, do it
# manually if needed.

if [ $(tail -c1 "$results_dir/results.txt" | wc -l) -eq 0 ]; then
    echo >> $results_dir/results.txt
fi

check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # We are not explicitly keeping test data so delete the stored data records
    # regardless of test status
    create_search "search environment._test_var=$_test_var | delete" \
                  "to delete test records"
    if [[ $ret -ne 0 ]]; then
        exit 11
    fi

    create_search "search environment._test_var=$_test_var | sort _time" \
                  "to check if records were deleted correctly"
    if [[ $ret -ne 0 ]]; then
        exit 12
    fi

    get_search_results "csv" "to check if records were deleted correctly"
    ret=$?
    if [[ $ret -ne 0 ]]; then
        exit 13
    elif [[ -n $search_result ]]; then
        errmsg="Delete of records with environment._test_var=$_test_var failed"
        if [[ -e $summary_file ]]; then
            logerr "$errmsg"
        else
            # Test passed, logs have already been cleaned up
            >&2 echo "$errmsg"
        fi
        exit 14
    fi
fi
