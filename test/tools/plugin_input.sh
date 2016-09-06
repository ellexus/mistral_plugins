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
# Set up some substitution variables
datetime1=$(date --date="$refdate" +"%F %T")
datetime2=$(date --date="$refdate -1 day" +"%F %T")
datetime3=$(date --date="$refdate -2 days" +"%F %T")
datetime4=$(date --date="$refdate -7 days" +"%F %T")
datetime5=$(date --date="$refdate -8 days" +"%F %T")
datetime6=$(date --date="$refdate -30 days" +"%F %T")
datetime7=$(date --date="$refdate -45 days" +"%F %T")
datetime8=$(date --date="$refdate -60 days" +"%F %T")
dateday1=${datetime1% *}
dateday2=${datetime2% *}
dateday3=${datetime3% *}
dateday4=${datetime4% *}
dateday5=${datetime5% *}
dateday6=${datetime6% *}
dateday7=${datetime7% *}
dateday8=${datetime8% *}
datetime1=${datetime1#* }
datetime2=${datetime2#* }
datetime3=${datetime3#* }
datetime4=${datetime4#* }
datetime5=${datetime5#* }
datetime6=${datetime6#* }
datetime7=${datetime7#* }
datetime8=${datetime8#* }

utcdatetime1=$(date -u --date="$refdate" +"%F %T")
utcdatetime2=$(date -u --date="$refdate -1 day" +"%F %T")
utcdatetime3=$(date -u --date="$refdate -2 days" +"%F %T")
utcdatetime4=$(date -u --date="$refdate -7 days" +"%F %T")
utcdatetime5=$(date -u --date="$refdate -8 days" +"%F %T")
utcdatetime6=$(date -u --date="$refdate -30 days" +"%F %T")
utcdatetime7=$(date -u --date="$refdate -45 days" +"%F %T")
utcdatetime8=$(date -u --date="$refdate -60 days" +"%F %T")
utcdateday1=${utcdatetime1% *}
utcdateday2=${utcdatetime2% *}
utcdateday3=${utcdatetime3% *}
utcdateday4=${utcdatetime4% *}
utcdateday5=${utcdatetime5% *}
utcdateday6=${utcdatetime6% *}
utcdateday7=${utcdatetime7% *}
utcdateday8=${utcdatetime8% *}
utcdatetime1=${utcdatetime1#* }
utcdatetime2=${utcdatetime2#* }
utcdatetime3=${utcdatetime3#* }
utcdatetime4=${utcdatetime4#* }
utcdatetime5=${utcdatetime5#* }
utcdatetime6=${utcdatetime6#* }
utcdatetime7=${utcdatetime7#* }
utcdatetime8=${utcdatetime8#* }

time1=$(date --date="$refdate" +"%F %T")
time2=$(date --date="$refdate -1 minute" +"%F %T")
time3=$(date --date="$refdate -2 minutes" +"%F %T")
time4=$(date --date="$refdate -3 minutes" +"%F %T")
time5=$(date --date="$refdate -4 minutes" +"%F %T")
time6=$(date --date="$refdate -5 minutes" +"%F %T")
time7=$(date --date="$refdate -15 minutes" +"%F %T")
time8=$(date --date="$refdate -60 minutes" +"%F %T")
tsday1=${time1% *}
tsday2=${time2% *}
tsday3=${time3% *}
tsday4=${time4% *}
tsday5=${time5% *}
tsday6=${time6% *}
tsday7=${time7% *}
tsday8=${time8% *}
tstime1=${time1#* }
tstime2=${time2#* }
tstime3=${time3#* }
tstime4=${time4#* }
tstime5=${time5#* }
tstime6=${time6#* }
tstime7=${time7#* }
tstime8=${time8#* }

utctime1=$(date -u --date="$refdate" +"%F %T")
utctime2=$(date -u --date="$refdate -1 minute" +"%F %T")
utctime3=$(date -u --date="$refdate -2 minutes" +"%F %T")
utctime4=$(date -u --date="$refdate -3 minutes" +"%F %T")
utctime5=$(date -u --date="$refdate -4 minutes" +"%F %T")
utctime6=$(date -u --date="$refdate -5 minutes" +"%F %T")
utctime7=$(date -u --date="$refdate -15 minutes" +"%F %T")
utctime8=$(date -u --date="$refdate -60 minutes" +"%F %T")
utctsday1=${utctime1% *}
utctsday2=${utctime2% *}
utctsday3=${utctime3% *}
utctsday4=${utctime4% *}
utctsday5=${utctime5% *}
utctsday6=${utctime6% *}
utctsday7=${utctime7% *}
utctsday8=${utctime8% *}
utctstime1=${utctime1#* }
utctstime2=${utctime2#* }
utctstime3=${utctime3#* }
utctstime4=${utctime4#* }
utctstime5=${utctime5#* }
utctstime6=${utctime6#* }
utctstime7=${utctime7#* }
utctstime8=${utctime8#* }

fullhostname=$(uname -n)
hostname=${fullhostname%%.*}

echo "s/UTCDATE1/$utcdateday1/g" > "$output"
echo "s/UTCDATE2/$utcdateday2/g" >> "$output"
echo "s/UTCDATE3/$utcdateday3/g" >> "$output"
echo "s/UTCDATE4/$utcdateday4/g" >> "$output"
echo "s/UTCDATE5/$utcdateday5/g" >> "$output"
echo "s/UTCDATE6/$utcdateday6/g" >> "$output"
echo "s/UTCDATE7/$utcdateday7/g" >> "$output"
echo "s/UTCDATE8/$utcdateday8/g" >> "$output"
echo "s/UTCTIME1/$utcdatetime1/g" >> "$output"
echo "s/UTCTIME2/$utcdatetime2/g" >> "$output"
echo "s/UTCTIME3/$utcdatetime3/g" >> "$output"
echo "s/UTCTIME4/$utcdatetime4/g" >> "$output"
echo "s/UTCTIME5/$utcdatetime5/g" >> "$output"
echo "s/UTCTIME6/$utcdatetime6/g" >> "$output"
echo "s/UTCTIME7/$utcdatetime7/g" >> "$output"
echo "s/UTCTIME8/$utcdatetime8/g" >> "$output"
echo "s/UTCTSDATE1/$utctsday1/g" >> "$output"
echo "s/UTCTSDATE2/$utctsday2/g" >> "$output"
echo "s/UTCTSDATE3/$utctsday3/g" >> "$output"
echo "s/UTCTSDATE4/$utctsday4/g" >> "$output"
echo "s/UTCTSDATE5/$utctsday5/g" >> "$output"
echo "s/UTCTSDATE6/$utctsday6/g" >> "$output"
echo "s/UTCTSDATE7/$utctsday7/g" >> "$output"
echo "s/UTCTSDATE8/$utctsday8/g" >> "$output"
echo "s/UTCTSTIME1/$utctstime1/g" >> "$output"
echo "s/UTCTSTIME2/$utctstime2/g" >> "$output"
echo "s/UTCTSTIME3/$utctstime3/g" >> "$output"
echo "s/UTCTSTIME4/$utctstime4/g" >> "$output"
echo "s/UTCTSTIME5/$utctstime5/g" >> "$output"
echo "s/UTCTSTIME6/$utctstime6/g" >> "$output"
echo "s/UTCTSTIME7/$utctstime7/g" >> "$output"
echo "s/UTCTSTIME8/$utctstime8/g" >> "$output"
echo "s/TSDATE1/$tsday1/g" >> "$output"
echo "s/TSDATE2/$tsday2/g" >> "$output"
echo "s/TSDATE3/$tsday3/g" >> "$output"
echo "s/TSDATE4/$tsday4/g" >> "$output"
echo "s/TSDATE5/$tsday5/g" >> "$output"
echo "s/TSDATE6/$tsday6/g" >> "$output"
echo "s/TSDATE7/$tsday7/g" >> "$output"
echo "s/TSDATE8/$tsday8/g" >> "$output"
echo "s/TSTIME1/$tstime1/g" >> "$output"
echo "s/TSTIME2/$tstime2/g" >> "$output"
echo "s/TSTIME3/$tstime3/g" >> "$output"
echo "s/TSTIME4/$tstime4/g" >> "$output"
echo "s/TSTIME5/$tstime5/g" >> "$output"
echo "s/TSTIME6/$tstime6/g" >> "$output"
echo "s/TSTIME7/$tstime7/g" >> "$output"
echo "s/TSTIME8/$tstime8/g" >> "$output"
echo "s/DATE1/$dateday1/g" >> "$output"
echo "s/DATE2/$dateday2/g" >> "$output"
echo "s/DATE3/$dateday3/g" >> "$output"
echo "s/DATE4/$dateday4/g" >> "$output"
echo "s/DATE5/$dateday5/g" >> "$output"
echo "s/DATE6/$dateday6/g" >> "$output"
echo "s/DATE7/$dateday7/g" >> "$output"
echo "s/DATE8/$dateday8/g" >> "$output"
echo "s/TIME1/$datetime1/g" >> "$output"
echo "s/TIME2/$datetime2/g" >> "$output"
echo "s/TIME3/$datetime3/g" >> "$output"
echo "s/TIME4/$datetime4/g" >> "$output"
echo "s/TIME5/$datetime5/g" >> "$output"
echo "s/TIME6/$datetime6/g" >> "$output"
echo "s/TIME7/$datetime7/g" >> "$output"
echo "s/TIME8/$datetime8/g" >> "$output"
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
