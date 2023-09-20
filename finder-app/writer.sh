#!/bin/bash
# Author: Alfonso Arbona Gimeno
#
# Assignment1.10
# 2023-09-20
#
# OBJECTIVES:
# - Accepts the following arguments: the first argument is a full path to a file (including filename) on the filesystem,
#   referred to below as writefile; the second argument is a text string which will be written within this file,
#   referred to below as writestr
# - Exits with value 1 error and print statements if any of the arguments above were not specified
# - Creates a new file with name and path writefile with content writestr, overwriting any existing file and creating
#   the path if it doesn’t exist. Exits with value 1 and error print statement if the file could not be created.

# Example: writer.sh /tmp/aesd/assignment1/sample.txt ios

print_help () {
	echo "USAGE: $0 <path-to-file> <text-to-write>"
	echo "Returns: 1 on error, 0 on success"
	echo "Creates the file (including directories) and sets the contents to that text (overwriting)"
	echo "Example: $0 /tmp/aesd/assignment1/sample.txt ios"
}

if [ $# -ne 2 ]
then
	print_help
	exit 1
fi

writefile=$1
writestr=$2

writepath=$(dirname "$writefile")

mkdir -p "$writepath" || exit 1

echo "$writestr" > "$writefile" || exit 1

exit 0
