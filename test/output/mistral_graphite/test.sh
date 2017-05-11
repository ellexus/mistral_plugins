#!/bin/bash

. ../../plugin_test_utilities.sh

# Set up Graphite defaults but allow them to be overridden

graphite_protocol=${graphite_protocol:-http}
graphite_host=${graphite_host:-graphite.camb.ellexus.com}
graphite_port=${graphite_port:-2003}
graphite_db=${graphite_db:-mistral.${test_timestamp}}

graphite_user=${graphite_user-ellexus}
graphite_pass=${graphite_pass-ellexus}
graphite_auth=${graphite_user}:${graphite_pass}

# Run a standard test
run_test -i "$graphite_db" -h "$graphite_host" -p "$graphite_port"

# Get the results
refdate=$(grep "Reference date" $results_dir/input.dat.sed | cut -d ' ' -f 4-)
fromdate=$(date --date="$refdate -0 days -15 mins -1 seconds" +"%s")
todate=$(date --date="$refdate -1 seconds" +"%s")

# The server can be a bit slow to sync so sleep for 2 seconds before requesting the data
sleep 2
curl -s -GET "$graphite_protocol://$graphite_host/render?format=raw" \
    --data-urlencode "from=$fromdate" \
    --data-urlencode "until=$todate" \
    --data-urlencode "target=${graphite_db}.*.*.*.*.*.*.*.*.*.*.*.*" \
    > $results_dir/results.txt

check_results

