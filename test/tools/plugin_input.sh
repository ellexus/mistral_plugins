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

refdate=$(date +"%a %b %e %T %Z %Y")
echo "# Reference date: $refdate" > "$output"
# Set up some substitution variables

for day in 60 45 30 8 7 2 1 0; do
    for mins in 60 15 5 4 3 2 1 0; do
        eval "utcdate${day}time${mins}=\$(date -u --date=\"$refdate -$day days -$mins minutes\" +\"%F %T\")"
        eval "date${day}time${mins}=\$(date --date=\"$refdate -$day days -$mins minutes\" +\"%F %T\")"
        eval "echo s/UTCDATE_${day}_${mins}/\${utcdate${day}time${mins}% *}/g" >> "$output"
        eval "echo s/UTCTIME_${day}_${mins}/\${utcdate${day}time${mins}#* }/g" >> "$output"
        eval "echo s/UTCTS_${day}_${mins}/\${utcdate${day}time${mins}}/g" >> "$output"
        eval "echo s/DATE_${day}_${mins}/\${date${day}time${mins}% *}/g" >> "$output"
        eval "echo s/TIME_${day}_${mins}/\${date${day}time${mins}#* }/g" >> "$output"
        eval "echo s/TS_${day}_${mins}/\${date${day}time${mins}}/g" >> "$output"
    done
done

fullhostname=$(uname -n)
hostname=${fullhostname%%.*}
echo "s/FULLHOSTNAME/$fullhostname/g" >> "$output"
echo "s/HOSTNAME/$hostname/g" >> "$output"

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
