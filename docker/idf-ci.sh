#!/bin/bash

rpath="$( dirname "$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )" )"
cd $rpath

docker run --rm -v /dev:/dev --privileged \
    -e BOARD="${BOARD:-NERDQAXEPLUS2}" \
    -e BIGSCREEN="${BIGSCREEN:-1}" \
    -v "$rpath":/home/builder/project \
    "${IDF_IMAGE:-shufps/esp-idf-builder:5.3.6}" idf.py "$@"
