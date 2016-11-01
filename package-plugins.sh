#!/bin/bash

source ./tools/package-tools.sh

remote_compile "make" ellexus@10.33.0.102 . ./static output/mistral_influxdb/mistral_influxdb.x86_64 output/mistral_mysql/mistral_mysql.x86_64

remote_compile "make" ellexus@10.33.0.101 . ./static output/mistral_influxdb/mistral_influxdb.i386 output/mistral_mysql/mistral_mysql.i386
