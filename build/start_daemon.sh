#!/bin/bash

PROJECT_NAME="easy_chat_server"
ABS_PATH=`pwd`

ProcessCount=`ps -ef | grep $PROJECT_NAME | wc -l`

if [ X"$1" == X"-v" ];then
    ps -ef | grep $PROJECT_NAME | sed '/grep '"$PROJECT_NAME"'/d'
    exit 0
fi

if [ X"$1" == X"-k" ];then
    if [ X"3" != X"$ProcessCount" ];then
        echo -e "\033[01;33m$PROJECT_NAME does not run\033[01;0m"
        exit -1
    fi

    ps -ef | grep $PROJECT_NAME | sed '/grep '"$PROJECT_NAME"'/d'

    ParentProcessID=`ps -ef | grep $PROJECT_NAME | awk 'NR==1{print $2}'`
    if [ ! -z $ParentProcessID ];then
        kill 9 $ParentProcessID
        echo "Success kill $ParentProcessID"
    fi

    ChildProcessID=`ps -ef | grep $PROJECT_NAME | awk 'NR==1{print $2}'`
    if [ ! -z $ChildProcessID ];then
        kill 9 $ChildProcessID
        echo "Success kill $ChildProcessID"
    fi

    exit 0
fi

if [ X"1" != X"$ProcessCount" ];then
    echo -e "\033[01;31mFail to start, $PROJECT_NAME process has existed\033[01;33m"
    ps -ef | grep $PROJECT_NAME | sed '/grep '"$PROJECT_NAME"'/d'
    exit -1
fi


if [ ! -f $PROJECT_NAME ];then
    echo -e "\033[01;31mNo such file: $PROJECT_NAME\033[0m"
    exit -1
fi

$ABS_PATH/$PROJECT_NAME
exit 0
