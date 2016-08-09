#!/bin/bash

# Force glibc to output any error messages it produces (e.g. stackdumps) on
# stderr rather than writing to /dev/tty so we can capture them
export LIBC_FATAL_STDERR_=1

# Set up some directories based on a standard layout
script_path=$(readlink -f "$0")
script_dir=$(dirname "$script_path")
test_root=$(readlink -f "$script_dir/../..")
test_tools="$test_root/tools"
repo_root=$(readlink -f "$test_root/..")
plugin_dir="$repo_root${script_dir#$test_root}"
plugin_name=$(basename "$plugin_dir")
plugin_path="$plugin_dir/$plugin_name"

# Create an output directory for files created by the test
if [ $# -eq 0 ]; then
    results_dir=$(mktemp -d)
elif [ $# -eq 1 ]; then
    # Make the directory before canonicalizing the path, so that this works on
    # CentOS 4, where readlink does not canonicalize nonexistent paths.
    mkdir -p -- "$1"
    results_dir=$(readlink -f "$1")
else
    echo "Usage: $0 [DIRECTORY]"
    exit 1
fi

summary_file=$results_dir/summary.txt
touch "$summary_file"

function logerr() {
    2>&1 echo "ERROR: $@" | tee -a "$summary_file"
}

function run_test() {
    # Currently no parameters are expected by the function itself but if needed
    # later these must come first and be "shift"ed away so all the remaining
    # parameters can be passed unchanged to the plug-in

    $plugin_path $@ < <("$test_tools/plugin_input.sh" "$script_dir/input.dat" \
                        $results_dir/input.dat.sed) \
        >"$results_dir/plugin.out" 2>"$results_dir/plugin.err"

}

function check_results() {
    if [ -r "$script_dir/plugin.out" ];then
        result=$(diff "$script_dir/plugin.out" "$results_dir/plugin.out" \
                 2>&1)

        if [ "${#result}" -ne 0 ]; then
            logerr "Results do not match expected $results_dir/plugin.out"
            echo "$result" >> "$summary_file"
        fi
    fi

    if [ -r "$script_dir/plugin.err" ];then
        result=$(diff "$script_dir/plugin.err" "$results_dir/plugin.err" \
                 2>&1)

        if [ "${#result}" -ne 0 ]; then
            logerr "Results do not match expected $results_dir/plugin.err"
            echo "$result" >> "$summary_file"
        fi
    fi

    if [ ! -r $script_dir/expected.txt ]; then
        logerr "Expected results file does not exist or is unreadable - $script_dir/expected.txt"
    else

        if [ ! -e $results_dir/input.dat.sed ]; then
            touch "$results_dir/input.dat.sed"
        fi

        result=$(diff $results_dir/results.txt - \
                 < <(sed -f $results_dir/input.dat.sed $script_dir/expected.txt) \
                 2>&1)

        if [ "${#result}" -ne 0 ]; then
            logerr "Results do not match expected"
            echo "$result" >> "$summary_file"
        fi
    fi

    if [ $(grep -c ERROR: "$summary_file") -ne 0 ]; then
        echo "FAILURE: see '$summary_file' for details"
    else
        if [ -n "$KEEP_TEST_OUTPUT" ];then
            echo "SUCCESS: See '$summary_file' for details"
        else
            rm -rf "$results_dir"
            echo "SUCCESS"
        fi
    fi
}
