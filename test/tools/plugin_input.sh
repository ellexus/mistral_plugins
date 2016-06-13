#!/bin/bash

function usage() {
    >&2 echo "  usage: $0 <input file> [output file]"
}

input=$1
output=$2

if [ -z "$input" ]; then
    >&2 echo "ERROR: missing input file name"
    usage
    exit 1
elif [ ! -r "$input" ]; then
    >&2 echo "ERROR: input file does not exist or is not readable"
    usage
    exit 2
fi

# set up the file name that will contain the variable substitutions set up below
if [ -z "$output" ]; then
    output="$(readlink -f "$input").sed"
elif [ -d "$output" ]; then
    output="$output/plugin_input.sed"
fi

output_dir=$(dirname "$output")
mkdir -p "$output_dir"

if [ -e "$output" ] && [ ! -w "$output" ]; then
    >&2 echo "ERROR: unable to write to file '$output'"
    usage
    exit 3
elif [ ! -w "$output_dir" ]; then
    >&2 echo "ERROR: directory does not exist or is not writable '$output_dir'"
    usage
    exit 4
fi

# Set up some substitution variables
date1=$(date +%F)
date2=$(date --date="yesterday" +%F)
date3=$(date --date="2 days ago" +%F)
date4=$(date --date="7 days ago" +%F)
date5=$(date --date="8 days ago" +%F)
date6=$(date --date="30 days ago" +%F)
date7=$(date --date="45 days ago" +%F)
date8=$(date --date="60 days ago" +%F)

time1=$(date +%R)
time2=$(date --date="1 minute ago" +%R)
time3=$(date --date="2 minutes ago" +%R)
time4=$(date --date="3 minutes ago" +%R)
time5=$(date --date="4 minutes ago" +%R)
time6=$(date --date="5 minutes ago" +%R)
time7=$(date --date="15 minutes ago" +%R)
time8=$(date --date="60 minutes ago" +%R)

echo "s/DATE1/$date1/g" > "$output"
echo "s/DATE2/$date2/g" >> "$output"
echo "s/DATE3/$date3/g" >> "$output"
echo "s/DATE4/$date4/g" >> "$output"
echo "s/DATE5/$date5/g" >> "$output"
echo "s/DATE6/$date6/g" >> "$output"
echo "s/DATE7/$date7/g" >> "$output"
echo "s/DATE8/$date8/g" >> "$output"
echo "s/TIME1/$time1/g" >> "$output"
echo "s/TIME2/$time2/g" >> "$output"
echo "s/TIME3/$time3/g" >> "$output"
echo "s/TIME4/$time4/g" >> "$output"
echo "s/TIME5/$time5/g" >> "$output"
echo "s/TIME6/$time6/g" >> "$output"
echo "s/TIME7/$time7/g" >> "$output"
echo "s/TIME8/$time8/g" >> "$output"

# The IFS='' prevents leading and trailing whitespace from being trimed, and the
# -n test deals with a missing trailing newline on the last line of input
while IFS='' read -r line || [ -n "$line" ]; do
    if [ -z "$line" ]; then
        # Ignore blank lines
        continue;
    elif [ "${line:0:1}" = "#" ]; then
        # Comment
        case "$line" in
        \#BLANK)
            # Used if we actually want to print a blank line
            echo
            continue;
            ;;
        \#SLEEP*)
            # Used if we want to wait before outputing the next line
            sleeptime=${line#\#SLEEP}
            sleep $sleeptime
            continue;
            ;;
        \#\#*)
            # Used to output a line that starts with a #
            comment=${line#\#}
            echo "$comment"
            ;;
        *)
            # Ignore anything else as a comment
            continue;
            ;;
        esac
    else
        echo "$line" | sed -f "$output"
    fi
done < "$input"
