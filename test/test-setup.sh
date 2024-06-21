#!/bin/bash

testdir="$(readlink -f $(dirname "${0}"))"
genimage="$(pwd)/genimage"

PATH="$PATH:/sbin:/usr/sbin"

set -- -v "$@"


. "${testdir}/sharness.sh"

filelist_orig="$(pwd)/file-list.orig"
filelist_test="$(pwd)/file-list.test"
root_orig="$(pwd)/root.orig"
root_test="$(pwd)/root.test"

setup_data() {
	umask 0022
	mkdir -p "${root_orig}"/{foo,bar,baz,"with spaces"}/{1,2,3}
	touch "${root_orig}"/{foo,bar,baz,"with spaces"}/{1,2,3}/{one,two}
	find "${root_orig}" -print0 | xargs -0 touch -c -d "2011-11-11 UTC"
	find "${root_orig}"/ -mindepth 1 -printf "%P\n" | sort  > "${filelist_orig}"

	cp "${testdir}"/*.conf* "${testdir}"/*.sh .
}

run_genimage_impl() {
	if [ ! -e "${1}" ]; then
		echo "ERROR: genimage config file '${1}' missing!"
		return 130
	fi
	if [ "$verbose" = "t" ]; then
		vargs="--loglevel=3"
	fi
	rm -rf tmp images "${root_test}"
	mkdir "${root_test}" images
	if [ -n "${2}" ]; then
		# create a larger output image to make sure it is recreated properly
		dd if=/dev/zero of="images/${2}" bs=1M seek=30 count=1
	fi
	"${genimage}" \
		${vargs} \
		--outputpath=images \
		--inputpath=input \
		--rootpath="${root}" \
		--tmppath=tmp \
		${extra_opts} \
		--config "${1}"
}

run_genimage_root() {
	root="root.orig" run_genimage_impl "${@}"
}

run_genimage() {
	root="/this/directory/does/not/exist" run_genimage_impl "${@}"
}

get_size() {
	local file="${1}"
	if [ ! -f "${file}" ]; then
		echo "Failed to check file size: '${file}' does not exist!"
		return 1
	fi
	set -- $(du -b "${file}")
	size="${1}"
}

check_size_range() {
	local size
	get_size "${1}" || return
	if [ "${size}" -lt "${2}" -o "${size}" -gt "${3}" ]; then
		echo "Incorrect file size for '${1}': expected min: ${2} max: ${3} found: ${size}"
		return 1
	fi
}

check_size() {
	local size
	get_size "${1}" || return
	if [ "${size}" -ne "${2}" ]; then
		echo "Incorrect file size for '${1}': expected: ${2} found: ${size}"
		return 1
	fi
}

sfdisk_validate() {
	if [ -n "$(sfdisk -q -V "${1}" 2>&1 | grep -v unallocated)" ]; then
		echo "'sfdisk -V' failed with:"
		sfdisk -V "${1}" 2>&1
		return 1
	fi
}

setup_test_images() {
	rm -rf input &&
	mkdir input &&
	dd if=/dev/zero of=input/part1.img bs=512 count=7 &&
	dd if=/dev/zero of=input/part2.img bs=512 count=11 &&
	touch input/part3.img
}

sanitized_fdisk_sfdisk() {
	# check the disk identifier
	fdisk -l "${1}" | grep identifier: &&
	# check partitions; filter output to handle different sfdisk versions
	sfdisk -d "${1}" 2>/dev/null | grep '^images/' | \
		sed -e 's/  *//g' -e 's;Id=;type=;'
}

exec_test_set_prereq() {
	command -v "${1}" > /dev/null && test_set_prereq "${1/./_}"
}

set -o pipefail

setup_data

sfdisk -h | grep -q gpt && test_set_prereq sfdisk-gpt
fdisk -h | grep -q gpt && test_set_prereq fdisk-gpt
