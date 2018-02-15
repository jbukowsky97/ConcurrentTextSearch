#!/bin/bash

if [ $# -lt 1 ]
then
  # echo to stderr
  echo Usage: $0 search_string 1>&2
  exit 0
fi

# All *.txt files in the current directory
for f in *.txt
do
  echo $f
  # place each word into a separate line, sort, and count
  cat $f | tr ' ' '\n' | sort | uniq -c | grep -w $1
done