#!/bin/bash

while true
do
    ./file_io  -l 10 -b 10 -o 100 ./output_file_io
    rand=$(( ( RANDOM % 60 )  + 1 + 120))
    sleep $rand
done
