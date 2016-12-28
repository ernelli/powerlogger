#!/bin/bash

nohup sudo ./powerlogger $LOGSERVER > /dev/null 2>> powerlogger.error.log &
