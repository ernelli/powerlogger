#!/bin/bash

if [ -e config.rc ]; then
. config.rc
else
  echo "config.rc not found, exit"
  exit 1
fi

nohup sudo ./powerlogger $LOGSERVER > /dev/null 2>> powerlogger.error.log &
