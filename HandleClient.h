/*************************************************************************
	> File Name: HandleClient.h
	> Author: gll
	> Mail: 2786768371@qq.com
	> Created Time: sat 4.3 15:20:29 2021
 ************************************************************************/

#ifndef _HANDLECLIENT_H
#define _HANDLECLIENT_H

#include<iostream>
#include<pthread.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<mysql/mysql.h>
#include<netinet/in.h>
using namespace std;

//线程执行此函数来发送消息
void *handle_send(void *arg);

//线程执行此函数来接收消息
void *handle_recv(void *arg);


#endif