#!/bin/bash

#Run Mistral with the file_io application to get some logs for testing

/home/lorenaq/Desktop/ellexus_repo/mistral_plugins/output/mistral_out_mysql/src/mistral_out_mysql_64 -c ../src/plugin_login.cnf -o err_log_test_2
day=$(date +'%d')
nrows=$(mysql -umistral -pmistral -h10.33.0.86 multiple_mistral_log -ss -e "SELECT COUNT(*) FROM log_${day} WHERE label LIKE 'TEST_%'")

echo $nrows >> ${PWD}/query_res.txt
