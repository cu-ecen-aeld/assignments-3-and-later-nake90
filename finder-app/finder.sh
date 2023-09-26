#!/bin/sh
# Author: Alfonso Arbona Gimeno
#
# Assignment1.9
# 2023-09-20
#
# OBJECTIVES:
# - Accepts the following runtime arguments: the first argument is a path to a directory on the filesystem, referred to
#   below as filesdir; the second argument is a text string which will be searched within these files, referred to below
#   as searchstr
# - Exits with return value 1 error and print statements if any of the parameters above were not specified
# - Exits with return value 1 error and print statements if filesdir does not represent a directory on the filesystem
# - Prints a message "The number of files are X and the number of matching lines are Y" where X is the number of files
#   in the directory and all subdirectories and Y is the number of matching lines found in respective files, where a
#   matching line refers to a line which contains searchstr (and may also contain additional content).

# Example: finder.sh /tmp/aesd/assignment1 linux

set -e

print_help () {
	echo "USAGE: $0 <search-path> <search-string>"
	echo "Returns: 1 on error, 0 on success"
	echo "Prints to stdout the number of files in the directory and subdirectories, and the number of lines in those files that match the search string"
	echo "Example: $0 /tmp/aesd/assignment1 linux"
}

if [ $# -ne 2 ]
then
	print_help
	exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]
then
	echo "$filesdir is not a directory"
	echo
	print_help
	exit 1
fi

# First just count the number of results from find
filecount=$(find "$filesdir" -type f | wc -l)

# Could this be integrated in the previous command? Likely yes
# Should I use time to optimize something I will only run once? No
# What I do here: find files and exec grep. I use + instead of ; so all results are concat as parameters to grep
# This will not work for too many files tho as it might go above the max parameter count
# The result is one line (file:line) per match, so I just count how many lines there are
linecount=$(find "$filesdir" -type f -exec grep "$searchstr" {} + | wc -l)

echo "The number of files are ${filecount} and the number of matching lines are ${linecount}"

exit 0
