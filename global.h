/*************************************************************************
	> File Name: global.h
	> Author: fuyinglong
	> Mail: 838106527@qq.com
	> Created Time: Wed Oct 21 16:41:24 2020
 ************************************************************************/

#ifndef _GLOBAL_H
#define _GLOBAL_H

#include<set>
#include<iostream>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<vector>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<mysql/mysql.h>
#include<unordered_map>
#include<pthread.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<unistd.h>
#include<queue>
#include<chrono>
#include<boost/bind.hpp>
#include<boost/asio.hpp>
#include<errno.h>
#include<list>
using namespace std;
using namespace chrono;

extern unordered_map<string, list<string>> friends;//用户和好友的映射
// extern list<string> nums[10000];
// extern int num;
// extern string friend_str;
extern string friend_name;
extern unordered_map<string,int> name_sock_map;//记录名字和文件描述符
extern unordered_map<int,set<int>> group_map;//记录群号和对应的文件描述符集合
extern unordered_map<string,string> from_to_map;//key:用户名 value:key的用户想私聊的用户
extern int Bloom_Filter_bitmap[1000000];//布隆过滤器所用的bitmap
extern pthread_mutex_t name_mutex;//互斥锁，锁住需要修改name_sock_map的临界区
extern pthread_mutex_t from_mutex;//互斥锁，锁住修改from_to_map的临界区
extern pthread_mutex_t group_mutex;//互斥锁，锁住修改group_map的临界区



#endif