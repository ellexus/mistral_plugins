#!/bin/bash

# Test funcionality of the mysql log plugin.

# Create the config file to be read by mistral
func_create_plugin_conf() {
    >$PLUGIN_CONFIG

    while [ "$#" -gt 0 ]; do
        case "$1" in
            OUTPUT)
                LC_PLUGIN=$(echo $1 | tr '[:upper:]' '[:lower:]')
                echo PLUGIN,$1 >>$PLUGIN_CONFIG
                echo PLUGIN_PATH,${PWD}/${LC_PLUGIN}.sh >>$PLUGIN_CONFIG
                shift
                ;;
            MYSQL)
                echo PLUGIN,OUTPUT >>$PLUGIN_CONFIG
                echo PLUGIN_PATH,${PWD}/mysql_log.sh >>$PLUGIN_CONFIG
                shift
                ;;
            END)
                echo END >>$PLUGIN_CONFIG
                shift
                ;;
            INT*)
                echo INTERVAL,${1#INT} >>$PLUGIN_CONFIG
                shift
                ;;
            *)
                echo PLUGIN_OPTION,$1 >>$PLUGIN_CONFIG
                shift
                ;;

        esac
    done
}

func_run_config_test() {
    if [ "${OUTPUT_OPTS-_UnSeT_}" != "_UnSeT_" ]; then
        OUTPUT_OPTS="${OUTPUT_OPTS} --output ${PLUGIN_CONFIG}_out"
    fi

    eval "export ${BRANDING}_PLUGIN_CONFIG=\$PLUGIN_CONFIG"
    # Use the following to get a file with logs to compare with the ones written in the DB
    #func_create_plugin_conf OUTPUT INT${OUT_INT} ${OUTPUT_OPTS} END
    #${PWD}/mistral_test/mistral_trunk_x86_64/mistral ./file_io -l 10 -b 10 -o 100 ./output_file_io

    func_create_plugin_conf MYSQL INT${MYSQL_INT} ${OUTPUT_OPTS} END

    if [ ! -d "./mistral_test" ]; then
        echo "Mistral directory does not exists."
        exit
    elif [ ! -e "./file_io" ]; then
        echo "file_io application does not exist"
        exit
    fi

    #${PWD}/mistral_trunk_x86_64/mistral ./file_io -l 10 -b 10 -o 100 ./output_file_io
    # Infinite loop of file_io calls
    ${PWD}/mistral_test/mistral_trunk_x86_64/mistral ${PWD}/test_always_on.sh

}

# Set up local and global contracts, only local contract information should be
# sent to the plugin.
export RESULTS_DIR=$HOME
export BRANDING=MISTRAL

CUSTOM_LABEL=$(echo TEST_SQL_$$_$HOSTNAME_$(date +"%m_%d_%yT%H_%M_%S") | sed 's/\./\_/g' )

export CONTRACT_MONITOR_GLOBAL=$RESULTS_DIR/global_monitor_contract
echo "monitortimeframe,50ms" > "$CONTRACT_MONITOR_GLOBAL"
echo "$CUSTOM_LABEL,/home,read,bandwidth,1B" >> "$CONTRACT_MONITOR_GLOBAL"
eval "export ${BRANDING}_CONTRACT_MONITOR_GLOBAL=\$CONTRACT_MONITOR_GLOBAL"
eval "export ${BRANDING}_LOG_MONITOR_GLOBAL=\$RESULTS_DIR/global_monitor.log"

#export CONTRACT_THROTTLE_GLOBAL=$RESULTS_DIR/global_throttle_contract
#echo "throttletimeframe,50ms" > "$CONTRACT_THROTTLE_GLOBAL"
#echo "$CUSTOM_LABEL,/home,read,bandwidth,300B" >> "$CONTRACT_THROTTLE_GLOBAL"
#eval "export ${BRANDING}_CONTRACT_THROTTLE_GLOBAL=\$CONTRACT_THROTTLE_GLOBAL"
#eval "export ${BRANDING}_LOG_THROTTLE_GLOBAL=\$RESULTS_DIR/global_throttle.log"



eval "export ${BRANDING}_RLM_LICENSE=/home/lorenaq/Desktop/ellexus_repo/breeze-repo/"

OUT_INT=300
MYSQL_INT=1

PLUGIN_CONFIG=${RESULTS_DIR}/plugin.conf
export TEST_PATH
export PLUGIN_EXIT=no
OUTPUT_OPTS=
OUTPUT_DIR=$RESULTS_DIR/log_plug

func_run_config_test

line=$(head -n 1  ${PWD}/query_res.txt)
num_logs_label=$(grep -c "TEST_" ${PLUGIN_CONFIG}_out)

#This comparison is valid only if both output and mysql_output are ran. 
#This way there is a way to compare the number oflogs from output plugin 
#and the ones written in the DB. 
if [ $line -ne $num_logs_label ]; then
    echo $line
    echo $num_logs_label
    echo FAILURE
else
    echo SUCCESS
fi

rm ${PWD}/query_res.txt
