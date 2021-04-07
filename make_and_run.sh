#!/bin/bash
echo "开始编译"
make
echo "开始执行server"
./serverV1 192.168.128.128 4567
