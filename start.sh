#!/bin/bash

LOGSERVER=http://ernelli.se:10091/store

nohup sudo ./led-trigger $LOGSERVER > /dev/null 2>> powerlogger.error.log &
