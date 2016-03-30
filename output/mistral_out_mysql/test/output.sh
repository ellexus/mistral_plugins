#!/bin/bash

# Parameters always come as pairs either a traditional "--option value" or
# simply pairs of valid plugin message type and the response to use. If the
# response is set to "NO_RESPONSE" then we will silently read the message, all
# other text is just sent back to the monitor verbatim. If a particular message
# type is not seen on the command line a valid response is sent (ignoring the
# effects of any overridden message responses)

EXIT=1
IN_DATA=0

while [ "$#" -ge 2 ]; do
    case "$1" in
        :PGNSUPVRSN:*)
            MESS_PGNSUPVRSN=$2
            ;;
        :PGNINTRVAL:*)
            MESS_PGNINTRVAL=$2
            ;;
        :PGNDATASRT:*)
            MESS_PGNDATASRT=$2
            ;;
        :PGNDATALIN:*)
            MESS_PGNDATALIN=$2
            ;;
        :PGNDATAEND:*)
            MESS_PGNDATAEND=$2
            ;;
        :PGNSHUTDWN:*)
            MESS_PGNSHUTDWN=$2
            ;;
        --output)
            OUT_FILE=$2
            ;;
        --log)
            LOG_FILE=$2
            ;;
        --exit)
            if [ "$2" == "no" ]; then
                EXIT=0
            else
                EXIT=1
            fi
            ;;
        *)
            break
            ;;
    esac
    shift 2
done

if [ "$OUT_FILE" != "" ]; then
    > $OUT_FILE
fi

while read data
do
    if [ "$OUT_FILE" != "" ]; then
        echo "$data" >> $OUT_FILE
    fi
    case "$data" in
        :PGNSUPVRSN:*)
            if [ "${MESS_PGNSUPVRSN}" != "NO_RESPONSE" ]; then
                # Extract the min supported version and use that by default
                VER=$(echo $data | cut -d: -f 3)
                echo ${MESS_PGNSUPVRSN-:PGNVERSION:${VER}:}
            fi
            ;;
        :PGNINTRVAL:*)
            if [ "${MESS_PGNINTRVAL}" != "NO_RESPONSE" ]; then
                # Default is not to respond
                if [ "${MESS_PGNINTRVAL-_UnSeT_}" != "_UnSeT_" ]; then
                    echo ${MESS_PGNINTRVAL}
                fi
            fi
            ;;
        :PGNDATASRT:*)
            IN_DATA=1
            if [ "${MESS_PGNDATASRT}" != "NO_RESPONSE" ]; then
                # Default is not to respond
                if [ "${MESS_PGNDATASRT-_UnSeT_}" != "_UnSeT_" ]; then
                    echo ${MESS_PGNDATASRT}
                fi
            fi
            ;;
        :PGNDATALIN:*)
            # In the current version of the interface these messages are never
            # seen, data is sent in a raw format.
            if [ "${MESS_PGNDATALIN}" != "NO_RESPONSE" ]; then
                # Default is not to respond
                if [ "${MESS_PGNDATALIN-_UnSeT_}" != "_UnSeT_" ]; then
                    echo ${MESS_PGNDATALIN}
                fi
            fi
            if [ "$LOG_FILE" != "" -a $IN_DATA -eq 1 ]; then
                echo "$data" >> $LOG_FILE
            fi
            ;;
        :PGNDATAEND:*)
            IN_DATA=0
            if [ "${MESS_PGNDATAEND}" != "NO_RESPONSE" ]; then
                # Default is not to respond
                if [ "${MESS_PGNDATAEND-_UnSeT_}" != "_UnSeT_" ]; then
                    echo ${MESS_PGNDATAEND}
                fi
            fi

            # The IN_DATA test is always true at the moment
            if [ $EXIT -eq 1 ] && [ $IN_DATA -eq 0 ]; then
                echo :PGNSHUTDWN:
                break
            fi
            ;;
        :PGNSHUTDWN:*)
            if [ "${MESS_PGNSHUTDWN}" != "NO_RESPONSE" ]; then
                # Default is not to respond
                if [ "${MESS_PGNSHUTDWN-_UnSeT_}" != "_UnSeT_" ]; then
                    echo ${MESS_PGNSHUTDWN}
                fi
            fi
            # Always exit on receipt of a shutdown message, otherwise the
            # monitor will hang until this process is killed which is not a very
            # useful scenareo for these tests
            break
            ;;
        *)
            # This is where we expect to see output data with v1 of the
            # interface. Treat this like a :PGNDATALIN: message.
            if [ "${MESS_PGNDATALIN}" != "NO_RESPONSE" ]; then
                # Default is not to respond
                if [ "${MESS_PGNDATALIN-_UnSeT_}" != "_UnSeT_" ]; then
                    echo ${MESS_PGNDATALIN}
                fi
            fi
            if [ "$LOG_FILE" != "" -a $IN_DATA -eq 1 ]; then
                echo "$data" >> $LOG_FILE
            fi
            ;;
    esac
done

exit

