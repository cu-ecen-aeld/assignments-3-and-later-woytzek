#!/bin/sh

print_usage() {
  echo Usage: finder.sh dir str
  echo        where: dir - path to directory to look into
  echo               str - string to search in files from 'dir' directory
}

number_of_files() {
  find $directory -type f | wc -l
}

number_of_matched_lines() {
  grep -r $string $directory 2>/dev/null | wc -l
}

if [ $# -eq 2 ]; then
  directory=$1
  string=$2
  if [ ! -d $directory ]; then
    echo $directory is not a directory
    print_usage
    exit 1
  fi
else
  echo Wrong number of input parameters
  print_usage
  exit 1
fi

fnumber=$(number_of_files)
mnumber=$(number_of_matched_lines)
echo The number of files are $fnumber and the number of matching lines are $mnumber

