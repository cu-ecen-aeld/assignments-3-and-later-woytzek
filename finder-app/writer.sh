#!/bin/sh

print_usage() {
  echo Usage: writer.sh file str
  echo        where: file - path to file to write
  echo               str - string to write
}

if [ $# -eq 2 ]; then
  fpath=$1
  string=$2
  fname=$(basename $fpath)
  fdir=$(dirname $fpath)

  if [ ! -e $fdir ]; then
    echo "$fdir doesn't exist. Creating directory ..."
    mkdir -p $fdir
    if [ ! $? -eq 0 ]; then
      echo Cannot create directory $fdir. Exiting ...
      exit 1
    fi
  fi
  if [ ! -d $fdir ]; then
    echo $fdir is not a directory
    print_usage
    exit 1
  fi
else
  echo Wrong number of input parameters
  print_usage
  exit 1
fi

echo $string > $fpath
if [ ! $? -eq 0 ]; then
  echo Cannot write to file $fpath
fi


