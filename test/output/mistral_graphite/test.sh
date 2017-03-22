#!/bin/bash

. ../../plugin_test_utilities.sh

# Set up Graphite defaults but allow them to be overridden

graphite_protocol=${graphite_protocol:-http}
graphite_host=${graphite_host:-graphite.camb.ellexus.com}
graphite_port=${graphite_port:-2003}
graphite_db=${graphite_db:-mistral.$(date +%Y%m%d%H%M%S)}

graphite_user=${graphite_user-ellexus}
graphite_pass=${graphite_pass-ellexus}
graphite_auth=${graphite_user}:${graphite_pass}

nc_cmd=$(which nc 2>/dev/null)
if [ -z "$nc_cmd" ]; then
    logerr "nc not found"
    exit 1
fi


# Run a standard test
run_test -i "$graphite_db" -h "$graphite_host" -p "$graphite_port"

# Get the results

#check_results

if [ -z "$KEEP_TEST_OUTPUT" ];then
    # Delete the test database regardless of test status
    echo
fi
