#!/bin/bash

# remote_compile MAKE MACHINE SOURCE_DIR RESULT_DIR COMPILED ...
#
# Copy SOURCE_DIR to a temporary directory on MACHINE and run MAKE
# remotely. The remaining arguments are the compiled files which are copied
# back from the remote machine to RESULT_DIR.

remote_compile() {
    local MAKE=$1
    local MACHINE=$2
    local SOURCE_DIR=$3
    local RESULT_DIR=$4
    shift 4

    echo "remote_compile - make: $MAKE"
    echo "remote_compile - machine: $MACHINE"
    echo "remote_compile - sources: $SOURCE_DIR"
    echo "remote_compile - results: $RESULT_DIR"

    local REMOTE_TMP=$(ssh "$MACHINE" mktemp -d)

    ( cd "$SOURCE_DIR" && tar cz ./ | ssh "$MACHINE" "cd $REMOTE_TMP && tar xz && $MAKE" )

    for COMPILED in "${@}"; do
        echo "remote_compile - getting compiled file: $COMPILED"
        scp "${MACHINE}:${REMOTE_TMP}/$COMPILED" "$RESULT_DIR"
    done

    ssh "$MACHINE" "rm -rf $REMOTE_TMP"
    echo "remote_compile - complete"
}
