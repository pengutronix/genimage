#!/bin/bash

exec >&2

name="${1}"
empty="${2}"

set -ex

: OUTPUTPATH
test "${OUTPUTPATH}" = "${PWD}/images"
: INPUTPATH
test "${INPUTPATH}" = "${PWD}/input"
: ROOTPATH
test "${ROOTPATH}" = "${PWD}/root.orig"
: TMPPATH
test "${TMPPATH}" = "${PWD}/tmp"
: IMAGE
test "${IMAGE}" = "${name}"
: IMAGEOUTFILE
test "${IMAGEOUTFILE}" = "${PWD}/images/${name}"
: IMAGENAME
test "${IMAGENAME}" = "exec-test"
: IMAGESIZE
test "${IMAGESIZE}" = "3584"
: IMAGEMOUNTPOINT
test "${IMAGEMOUNTPOINT}" = ""
: IMAGEMOUNTPATH
if [ "${empty}" = "empty" ]; then
	test "${IMAGEMOUNTPATH}" = ""
else
	test "${IMAGEMOUNTPATH}" = "${PWD}/tmp/root"
fi
