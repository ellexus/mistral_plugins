#!/bin/bash

# Test funcionality of the mistral output mysql plugin.

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
    if [ "${OUTPUT_OPTS:-_UnSeT_}" != "_UnSeT_" ]; then
        OUTPUT_OPTS="${OUTPUT_OPTS} --output ${PLUGIN_CONFIG}_out"
    fi

    eval "export ${BRANDING}_PLUGIN_CONFIG=\$PLUGIN_CONFIG"

    func_create_plugin_conf MYSQL INT${MYSQL_INT} ${OUTPUT_OPTS} END

    if [ ! -d "./mistral_test" ]; then
        echo "Mistral directory does not exists."
        exit
    elif [ ! -e "./file_io" ]; then
        echo "file_io application does not exist"
        exit
    fi

    # Infinite loop of file_io calls
    ${PWD}/mistral_test/mistral ${PWD}/test_always_on.sh

}

export RESULTS_DIR=$HOME
export BRANDING=MISTRAL

CUSTOM_LABEL=$(echo TEST_SQL_$$_$HOSTNAME_$(date +"%m_%d_%yT%H_%M_%S"))

# Set up local/global contracts
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


# the licence should be where mistral is extracted
export ${BRANDING}_RLM_LICENSE="./mistral_test"

# Value of INTERVAL in the plugin configuration file
MYSQL_INT=1

PLUGIN_CONFIG=${RESULTS_DIR}/plugin.conf
export TEST_PATH
export PLUGIN_EXIT=no
OUTPUT_OPTS=
OUTPUT_DIR=$RESULTS_DIR/mysql_out_plugin

func_run_config_test
