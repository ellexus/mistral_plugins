#!/bin/bash

# If given a dynamically linked progam, in this case a mistral plugin, returns a list of libraries
# that should be linked with, specified in the way that is used when linking a program. In other
# words, "-lfoo" rather than the name "libfoo".

function dynamiclibraries () {
    local program=$1
    ldd ${program} | grep '=> /' | awk '{print $1}' | cut -d. -f1 | sed -e "s/^lib/-l/"
}

# Construct the part of a command line to link a plugin using static libraries where possible.
#
# $1                    The path to a dynamically linked plugin
# Remaining parameters  The commands needed to dynamically link a plugin - e.g. $(mysql_config --libs)
#
# Returns the composite command line

function main () {
    local plugin=$1
    shift
    local config=$@

    local config_lib=""
    local extra_lib=""
    local dynamiclibs=""
    local staticlibs=""
    local req_k5crypto=false
    local req_crypto=false
    local req_ssl=false

    # Iterate over the command used to dynamically link the plugin.
    #
    # "-L" commands specify a directory to look in for a library and apply equally to both static
    # and dynamic libraries.
    #
    # There are some libraries, namely "-ldl", "-lrt", "-lm" and "-lpthread", which we will always
    # link dynamically.
    #
    # The remaining libraries are the ones that we want to statically link with.
    #
    # There are some static libraries, "-lss", "-lcrypto" and '-lk5crypto", that require a specific
    # order when linking that is not required when using dynamic versions so may have been provided
    # out of order. We handle these libraries separately, appending those we see to the end of the
    # static library list in the correct order once we have parsed all the provided options.

    for word in ${config} ; do
        case $word in
            -lk5crypto)
                req_k5crypto=true
                ;;
            -lcrypto)
                req_crypto=true
                ;;
            -lssl)
                req_ssl=true
                ;;
            -L*)
                staticlibs="${staticlibs} ${word}"
                dynamiclibs="${dynamiclibs} ${word}"
                ;;
            -ldl|-lrt|-lm|-lpthread)
                dynamiclibs="${dynamiclibs} ${word}"
                config_lib="${config_lib} ${word}"
                ;;
            -l*)
                staticlibs="${staticlibs} ${word}"
                config_lib="${config_lib} ${word}"
                ;;
        esac
    done

    # Append order dependent static libraries to the end of the static libraries list
    if [[ $req_ssl = true ]]; then
        staticlibs="${staticlibs} -lssl"
    fi
    if [[ $req_crypto = true ]]; then
        staticlibs="${staticlibs} -lcrypto"
    fi
    if [[ $req_k5crypto = true ]]; then
        staticlibs="${staticlibs} -lk5crypto"
    fi

    # The dynamically linked plugin will probably refer to other libraries which were not
    # explicitly stated when it was linked. We will dynamically link these libraries.

    for word in $(dynamiclibraries ${plugin}); do
        if echo $config_lib | grep -v -q -e ${word}; then
            extra_lib="${extra_lib} ${word}"
        fi
    done

    # Now piece together the command line using the libraries we have deduced, and adding
    # "-lgcc_s" which seems to always be needed.

    echo "-Wl,-Bstatic ${staticlibs} -Wl,-Bdynamic ${dynamiclibs} ${extra_lib} -lgcc_s"
}

# package.sh ./mistral_mysql.dynamic $(mysql_config --libs)

main $@
