#include "global.h"

unordered_map<string, list<string>> friends;//用户和好友的映射
// list<string> nums[10000];
// int num = 0;
// string friend_str;
string friend_name;
unordered_map<string,int> name_sock_map;//记录名字和文件描述符
unordered_map<int,set<int>> group_map;//记录群号和对应的文件描述符集合
unordered_map<string,string> from_to_map;//key:用户名 value:key的用户想私聊的用户
int Bloom_Filter_bitmap[1000000];//布隆过滤器所用的bitmap
pthread_mutex_t name_mutex;//互斥锁，锁住需要修改name_sock_map的临界区
pthread_mutex_t from_mutex;//互斥锁，锁住修改from_to_map的临界区
pthread_mutex_t group_mutex;//互斥锁，锁住修改group_map的临界区