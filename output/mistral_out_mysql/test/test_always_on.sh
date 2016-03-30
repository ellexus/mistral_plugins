#!/bin/bash

date
COUNT=1
while true
do
    #echo "Count " $COUNT >> ${PWD}/err_log_test
    ./file_io  -l 10 -b 10 -o 100 ./output_file_io
    #/bin/ls
    rand=$(( ( RANDOM % 60 )  + 1 + 120))
    #echo $rand
    COUNT=$((COUNT+1))
    sleep $rand
done
