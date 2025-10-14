#!/bin/bash

rpath="$( dirname "$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )" )"
cd $rpath

docker run --rm -v /dev:/dev --privileged -e BOARD="${BOARD:-NERDQAXEPLUS2}" -v "$rpath":/home/builder/project esp-idf-builder idf.py "$@"
