#!/bin/bash

. ../../plugin_test_utilities.sh

function check_index() {
    local index_name=$1
    curl -s --head --fail "$es_protocol://$es_host:$es_port/$index_name/" \
        -u $es_auth >/dev/null 2>&1
    return $?
}
# Set up elasticsearch defaults but allow them to be overridden

es_protocol=${es_protocol:-http}
es_host=${es_host:-elastic.camb.ellexus.com}
es_port=${es_port:-9200}
es_index=${es_index:-mistral$(date +%Y%m%d%H%M%S)}

es_user=${es_user-ellexus}
es_pass=${es_pass-ellexus}
es_auth=${es_user}:${es_pass}

curl_cmd=$(which curl 2>/dev/null)
if [ -z "$curl_cmd" ]; then
    logerr "curl not found"
    exit 1
fi

# Check no indexes already exist
check_index "${es_index}"
ret=$?
if [[ $ret -eq 0 ]]; then
    logerr "At least one index already exists - '$es_index'"
    exit 2
elif [[ $ret -ge 1 && $ret -ne 22 ]]; then
    logerr "Unable to check if index '$es_index' exists - curl error: $ret"
    exit 3
fi

# Create the test database
if [[ "$es_protocol" = "https" ]]; then
    create_protocol_flag="-s"
else
    create_protocol_flag=
fi

echo "$es_pass" > "$results_dir/es_pass.txt"

outval=$($curl_cmd -s $es_auth -XGET $es_protocol://$es_host:$es_port)
retval=$?

if [[ "$retval" -ne 0 ]]; then
    >&2 echo Error, could not get Elasticsearch version. Curl exited with error code $retval
    exit 6
elif [[ "${outval:0:9}" = '{"error":' ]]; then
    >&2 echo Error, could not get Elasticsearch version. ElasticSearch query failed:
    echo "$outval" | >&2 sed -e 's/.*reason":\([^}]*\)}.*/  \1/;s/,/\n  /g'
    exit 7
else
    es_version=$(echo "$outval" | grep number | sed -e 's/.*number" : "\([0-9]\+\).*/\1/')
fi

$plugin_dir/schema/mistral_create_elastic_template.sh -i "$es_index" \
    -h "$es_host" -n "$es_port" $create_protocol_flag -u $es_user \
    -p "$results_dir/es_pass.txt" > $summary_file


check_index "_template/${es_index}"
ret=$?

if [[ $ret -ge 1 && $ret -ne 22 ]]; then
    logerr "Unable to check if index template '$es_index' was created - curl error: $ret"
    exit 4
elif [[ $ret -eq 22 ]]; then
    logerr "Unable to create test database template - '$es_index'"
    exit 5
fi


if [ "$es_protocol" = "https" ]; then
    secure="-s"
fi

# Set a custom value to be included in the output
export _test_var=MISTRAL

# Set up expected results
if [[ "$es_version" -eq 6 ]]; then
    sed -e 's/throttle/_doc/' expected_v5.txt > expected.txt
elif [[ "$es_version" -lt 6 ]]; then
    cp expected_v5.txt expected.txt
else
    sed -e 's/,"_type":"throttle"//g' expected_v5.txt > expected.txt
fi

# Run a standard test
run_test -i "$es_index" -h "$es_host" -P "$es_port" $secure -u \
    "$es_user" -p "$es_pass" -v _test_var -V "$es_version"

# Get the results - it can take a little while for the data to sync so sleep for
# 2 seconds to ensure we get all results returned.
sleep 2
curl -s --get -u $es_auth \
    "$es_protocol://$es_host:$es_port/${es_index}-*/_search?size=100&sort=@timestamp:asc" \
    > $results_dir/raw_results.txt

# There are some unique fields within the returned data we cannot exclude using
# a query option. Strip this data out using a sed script.
sed -e 's/took.*total/total/;s/,"_id":[^,]*//g;s/{"_index/\n{"_index/g' \
    $results_dir/raw_results.txt > $results_dir/results.txt

# curl does not always append a newline to the end of the output, do it
# manually if needed.

if [ $(tail -c1 "$results_dir/results.txt" | wc -l) -eq 0 ]; then
    echo >> $results_dir/results.txt
fi

check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # Delete the test index regardless of test status
    curl -s --fail -XDELETE "$es_protocol://$es_host:$es_port/$es_index*" \
        -u $es_auth >/dev/null 2>&1

    curl -s --fail -XDELETE -u $es_auth \
        "$es_protocol://$es_host:$es_port/_template/$es_index" >/dev/null 2>&1

    check_index "${es_index}"
    ret=$?

    if [[ $ret -ge 1 && $ret -ne 22 ]]; then
        errmsg="Unable to check if test indexes '${es_index}' were removed: $ret"
        if [[ -e $summary_file ]]; then
            logerr $errmsg
        else
            # Test passed, logs have already been cleaned up
            >&2 echo $errmsg
        fi
        exit 5
    elif [[ $ret -eq 0 ]]; then
        errmsg="Unable to remove test indexes - '${es_index}'"
        if [[ -e $summary_file ]]; then
            logerr $errmsg
        else
            # Test passed, logs have already been cleaned up
            >&2 echo $errmsg
        fi
        exit 6
    fi
fi
