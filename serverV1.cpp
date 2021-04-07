
/*
server.c  gll  2021/4/7
实现方式：epoll模型 RAII机制
功能：从客户端接收数据，并将其中小写字符转化为大写，重新发给客户端
新：
    redis：
        key-value::sessionid name，并设置生存时间为300s
        即保存的是用户的登陆状态。根据sessionid查找到对应消息时，可以直接登陆；若未保存或过期，则告诉客户端需要重新登录
    布隆过滤器：
        将用户名字映射到二进制位图上，登陆时如果位图上没有则一定不存在该用户，借此来解决穿透问题
    cookie：
        1.接收cookie文件，取出其中的sessionid，并在redis中进行查找
        2.若成功找到对应的一条信息，并且该用户之前未登录，则用户可以利用redis直接登陆，记得登陆要修改name到socket的哈希映射；否则需要重新登录。
    注册：
        1.接收客户端发来的用户名和密码，并在数据库中进行查找，若用户名位存在，则注册成功；否则不成功
        2.若成功注册，需要将对应的name, pass, online(登陆状体)插入到数据库中
    登陆：
        1.接收客户端发来的用户名和密码，并首先利用布隆过滤器判断该用户是否在数据库存在，若不存在，则直接反馈给客户端
        2.若存在，则需要在数据库中进一步判断该用户是否已经登陆，若已经登陆则反馈给客户端不能重复登陆
        2.若之前未登录，则说明此次可以登陆,此时首先修改数据库中登陆状态；然后需要随即生成的sessionid反馈给客户端
    退出：
        1.修改数据库中登陆状态
        2.反馈给客户端
    注销：
        1.修改数据库中登陆状态
        2.反馈给客户端
    添加好友：
        1.接收好友名字，并判断该名字是否存在，若不存在直接返回
        2.若存在则需要将对应的名字加入当前用户的好友链表中，并反馈给客户端
    私聊建立：
        1.接收私聊对象的名字，首先根据哈希映射判断该用户是否存在或者登陆，不存在则直接反馈给客户端；
        2.若存在，还需要判断该对象是否是当前用户的好友，若不是，也需要反馈给客户端
        3.若是当前用户的好友，则可以建立对应的发送用户和接收用户之间的哈希映射，方便之后数据传送哦
    内容转发：
        1.之前已经建立了私聊，这里首先需要根据转发的用户，找到对应的转发的对象，若未找到，则需要重新查找
        2.若找到，便将数据发送给对象
    群聊建立：
        1.将当前用户绑定到对应的群聊号中的set中
    广播群消息：
        1.根据当前的socket_fd找到对应的用户，以及用户所在的群set
        2.将所要转发的消息发送给set中除当前用户之外的所有用户
*/

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
//#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <resolv.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <ctype.h>
#include <hiredis/hiredis.h>
#include <iostream>
#include "RAII.h"
#include "threadpool_new.h"
#include "global.h"
#include "myDB.h"
//int num = 0;
extern unordered_map<string,int> name_sock_map;//名字和套接字描述符
extern unordered_map<int,set<int>> group_map;//记录群号和套接字描述符集合
extern unordered_map<string,string> from_to_map;//记录用户xx要向用户yy发送信息
extern int Bloom_Filter_bitmap[1000000];//布隆过滤器所用的bitmap
extern pthread_mutex_t name_mutex;//互斥锁，锁住需要修改name_sock_map的临界区
extern pthread_mutex_t group_mutex;//互斥锁，锁住修改group_map的临界区
extern pthread_mutex_t from_mutex;//互斥锁，锁住修改from_to_map的临界区
extern unordered_map<string, list<string>> friends;
extern string friend_name;
using namespace std;
#define MAX_EVENT 1000//有上限
#define USER_BUFF_SIZE 30

struct Fds{//文件描述符结构体，用作传递给子线程的参数
	int	epoll_fd;
	int	sock_fd;
};
int set_noblock(int socket_fd) {
    int flag = fcntl(socket_fd, F_GETFL);//返回阻塞类型
    flag |= O_NONBLOCK;
    fcntl(socket_fd, F_SETFL, flag);
    return socket_fd;
}
void reset_oneshot(int socket_fd, int epoll_fd){//重置事件
	struct epoll_event	event;
	event.data.fd = socket_fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, socket_fd, &event) < 0) {
        perror("epoll_ctl error:");
        exit(1);
    }
}

string noblockRead(int socket_fd, int event_fd) {
    char buf[5];
    //必须通过循环将stdin中的所有数据全部写入文件中
    int size;
    string ret;
    while(1) {
        memset(buf, 0, sizeof(buf)); 
        size = read(socket_fd, buf, sizeof(buf));
        if(size < 0) {
            //判断是否是读完
            if(errno == EAGAIN) {
                //printf("read over!\n");
                reset_oneshot(socket_fd, event_fd);
                break;
            }
            //否则出错
            printf("read failed!errno code is %d,errno message is '%s'\n",errno, strerror(errno));
            break;
        }
        if(size == 0) {//!!!
            //printf("client disconnect whose ip is %s and port is %d!\n", client_addr, client_port);
            break;
            //return ret;
            //exit(1);
        }

        for(int i = 0; i < size; i++) {
            ret += buf[i];
            //cout<<ret[i]<<endl;
        }
    }
    return ret;
}

void noblockWrite(string ret, int socket_fd) {
    int size = ret.size();
    char *ans = new char[size];
    strcpy(ans, ret.c_str());
    int tmp;//记录每次写入的长度
    const char* p = ans;
    while(1) {
        tmp = write(socket_fd, p, size);
        if(tmp < 0) {
            if(errno != EAGAIN) {
                printf("write failed!\n");
                exit(1);
            }
            sleep(10);//过一段时间尝试再次写入
            continue;
        }
        //已经写完
        if (tmp == size) {
            break;
        }
        //没写完
        size = size - tmp;
        p = p + tmp;//移动指针，指向下一个该写入的数据
    }
}

bool is_login(string name, int socket_fd, MyDB &sqlDB) {
    string search_sql = "select * from user where name=\"";
    search_sql += name;
    search_sql += "\";";
    cout<<"sql语句："<<search_sql<<endl;
    //在数据库中执行,若不存在则直接退出
    if(!sqlDB.search(search_sql)) {
        cout<<"用户名不存在\n";
        string str1 = "wrong";
        write(socket_fd, str1.c_str(), str1.length());
        return false;
    }
    auto row = mysql_fetch_row(sqlDB.result);//获取一行的信息
    cout<<"查询到用户名"<<row[0]<<" 密码："<<row[1]<<endl;
    //密码正确，进一步判断是否已经登陆
    cout<<"online:"<<(int)row[2][0]-48<<endl;
    if(((int)row[2][0]-48) == 0) {
        return false;
    }
    else return true;
}
void process_data(void *arg) {
    int target_conn=-1;
    char buffer[1000];
    string name, pass;//用户名和密码
    bool if_login = false;//记录当前服务对象是否成功登录
    string login_name;//记录当前服务对象的名字
    string target_name;//记录发送信息时目标用户的名字
    int group_num;//记录群号
    //连接mysql数据库
    MyDB sqlDB;
    if(!sqlDB.InitDB("localhost","root","gll061200","test_connect")) {
        return ;
    }
    cout<<"连接MySQL数据库成功！\n";
    
    //连接redis数据库
    redisContext *redis_target = redisConnect("127.0.0.1", 6379);
    if(redis_target->err){
        redisFree(redis_target);
        cout<<"连接redis失败"<<endl;     
    }
    cout<<"-----------------------------\n";


    int	socket_fd = ((struct Fds *)arg)->sock_fd;
    int event_fd = ((struct Fds *)arg)->epoll_fd;      
    //处理数据时先循环读取，再挨个处理，最后循环输出
    string ret_str = noblockRead(socket_fd, event_fd);      
    cout<<"接收到的完整内容为"<<ret_str<<endl;
    //sleep(5);    
    int size = ret_str.size();
    cout<<size<<endl;

    //新增：先接收cookie看看redis是否保存该用户的登录状态
    if(ret_str.find("cookie:") != ret_str.npos){
        string cookie = ret_str.substr(7);
        cout<<"cookie"<<cookie<<endl;
        //sessionid + name

        string redis_str = "hget "+cookie+" name";
        cout<<redis_str<<endl;
        if(redis_target == nullptr) {
            cout<<"redis error\n";
            return ;
        }
        redisReply *r = (redisReply*)redisCommand(redis_target, redis_str.c_str());
        if(r == nullptr) return ;

        string send_res;//回馈的消息

        if(r->str) {
            cout<<"查询redis结果："<<r->str<<endl;
            name = r->str;//查询到的name
            //接下来判断该账户是否已经登陆
            if(is_login(name, socket_fd, sqlDB)) {
                cout<<"该账号已经登陆\n";
                send_res = "again:" + name;
                //write(socket_fd, str1.c_str(), str1.length());
            }
            //登陆成功
            else {
                send_res = name;
                pthread_mutex_lock(&name_mutex); //上锁
                name_sock_map[send_res] = socket_fd;//记录下名字和文件描述符的对应关系
                pthread_mutex_unlock(&name_mutex); //解锁
                //更新数据库中登陆状态字段
                string update_sql = "update user set online=1 where name=\"";
                update_sql += name;
                update_sql += "\";";
                cout<<"sql语句："<<update_sql<<endl;
                if(!sqlDB.insert(update_sql)) {
                    cout<<"更新失败\n";
                    return ;
                }
            }
        }
        else
            send_res = "NULL";
        write(socket_fd, send_res.c_str(), send_res.length());
        //if(r->str==)
    }
    //1.接受到的是登陆信息
    if(ret_str.find("login") != ret_str.npos) {
        //从客户端数据中找到用户名和密码
        int p1 = ret_str.find("login:");
        int p2 = ret_str.find("pass:");
        int flag = 0;
        name = ret_str.substr(p1+6, p2-6);
        cout<<"name:"<<name<<endl;
        pass = ret_str.substr(p2+5, ret_str.length()-p2-5);
        cout<<"pass:"<<pass<<endl;
                
        //新增布隆过滤器
        //对字符串使用哈希函数
        int hash = 0;
        for(auto ch:name) {
            hash = hash * 131 + ch;
            if(hash >= 10000000)
                hash %= 10000000;                            
        }
        int index = hash/32, pos = hash%32;
        if((Bloom_Filter_bitmap[index] & (1<<pos)) == 0){
            cout<<"布隆过滤器查询为0，登录用户名必然不存在数据库中\n";
            char str1[100]="wrong";
            write(socket_fd, str1, strlen(str1));
            flag = 1;
        }

        //布隆过滤器无法判断才要查数据库
        if(!flag) {
            
            string search_sql = "select * from user where name=\"";
            search_sql += name;
            search_sql += "\";";
            cout<<"sql语句："<<search_sql<<endl;
            //在数据库中执行,若不存在则直接退出
            if(!sqlDB.search(search_sql)) {
                cout<<"用户名不存在\n";
                string str1 = "wrong";
                write(socket_fd, str1.c_str(), str1.length());
                return ;
            }
            auto row = mysql_fetch_row(sqlDB.result);//获取一行的信息
            cout<<"查询到用户名"<<row[0]<<" 密码："<<row[1]<<endl;
            //密码正确，进一步判断是否已经登陆
            cout<<"online:"<<(int)row[2][0]-48<<endl;
            if(row[1] == pass && ((int)row[2][0]-48) == 0) {
                cout<<"登录成功!\n";
                //1.将登陆成功的消息发送给用户
                string str1 = "ok";
                if_login = true;
                login_name = name;

                //新添加：随机生成sessionid并发送到客户端
                srand(time(NULL));//初始化随机数种子
                for(int i = 0; i < 10; i++) {
                    int type = rand()%3;//type为0代表数字，为1代表小写字母，为2代表大写字母
                    if(type == 0)
                        str1 += '0'+rand()%9;
                    else if(type == 1)
                        str1 += 'a'+rand()%26;
                    else if(type == 2)
                        str1 += 'A'+rand()%26;
                }
                //将sessionid存入redis
                string redis_str = "hset " + str1.substr(2) + " name " + login_name;
                cout<<redis_str<<endl;
                redisReply *r = (redisReply*)redisCommand(redis_target, redis_str.c_str());
                //设置生存时间,默认300秒
                redis_str = "expire " + str1.substr(2) + " 300";
                r = (redisReply*)redisCommand(redis_target,redis_str.c_str());

                cout<<"随机生成的sessionid为："<<str1.substr(2)<<endl;
                //cout<<"redis指令:"<<r->str<<endl;

                
                //2.将用户和对应的描述符绑定起来
                pthread_mutex_lock(&name_mutex); //上锁
                name_sock_map[name] = socket_fd;//记录下名字和文件描述符的对应关系
                //friends[socket_fd] = num++;
                pthread_mutex_unlock(&name_mutex); //解锁
                //cout<<"num:"<<num<<endl;
                //friends[socket_fd] = num++;
                // friend_name = name;
                // nums[friends[socket_fd]].push_front(friend_name);
                write(socket_fd, str1.c_str(), str1.length());
                //3.更新数据库中登陆状态字段
                string update_sql = "update user set online=1 where name=\"";
                update_sql += name;
                update_sql += "\";";
                cout<<"sql语句："<<update_sql<<endl;
                if(!sqlDB.insert(update_sql)) {
                    cout<<"更新失败\n";
                    return ;
                }

                //4.将该用户已登陆的消息发送给所有好友列表!!!!!!!!!!!
                //if()

            }
            //密码错误，登陆失败;或者该用户已经登陆
            else {
                if(row[1] == pass) {
                    cout<<"该账号已经登陆\n";
                    string str1 = "again";
                    write(socket_fd, str1.c_str(), str1.length());
                }
                else {
                    cout<<"登录密码错误\n";
                    string str1 = "wrong";
                    write(socket_fd, str1.c_str(), str1.length());
                }

            }
            
        }
    }
    //2.接收到的是注册信息
    else if(ret_str.find("signup:") != ret_str.npos) {
        int p1 = ret_str.find("signup:");
        int p2 = ret_str.find("pass:");
        name = ret_str.substr(p1+7, p2-7);
        pass = ret_str.substr(p2+5, ret_str.length()-p2-5);
        //判断该用户名是否已经存在
        //不能使用布隆过滤器

        string search_sql = "select * from user where name=\"";
        search_sql += name;
        search_sql += "\";";
        cout<<"sql语句："<<search_sql<<endl;
        //用户名已经在数据库中存在
        if(sqlDB.search(search_sql)) {
            auto row = mysql_fetch_row(sqlDB.result);//获取一行的信息
            cout<<"查询到用户名"<<row[0]<<" 密码："<<row[1]<<endl;
            string reply_str = "wrong";
            write(socket_fd, reply_str.c_str(), reply_str.length());
        }
        //用户名不存在,注册成功
        else {
            //list<string> *ll = new list<string>;
            //friend_name = name;
            // friends[name] = num++;
            // nums[friends[name]].push_front(friend_name);
            //friends[name] = *ll;
            string reply_str = "ok";
            write(socket_fd, reply_str.c_str(), reply_str.length());
            string insert_sql="INSERT INTO user VALUES (\"";
            insert_sql += name;
            insert_sql += "\",\"";
            insert_sql += pass;
            //insert_sql += "\",;
            insert_sql += "\",0);";
            //insert_sql += "\");";
            cout<<endl<<"sql语句:"<<insert_sql<<endl; 
            //插入语句
            sqlDB.insert(insert_sql);           
        }
    }
    //用户要退出,将数据库登陆状态修改为0
    else if(ret_str.find("logout:") != ret_str.npos) {
        name = ret_str.substr(7, ret_str.length()-7);
        string update_sql = "update user set online=0 where name=\"";
        update_sql += name;
        update_sql += "\";";
        cout<<"sql语句："<<update_sql<<endl;
        if(!sqlDB.insert(update_sql)) {
            cout<<"更新失败\n";
            return ;
        }   
        string str = "ok";
        write(socket_fd, str.c_str(), str.length());     
    }
    //接收到的是添加好友的信息
    else if(ret_str.find("add:") != ret_str.npos) {
        //查找对应用户是否存在
        int p1 = ret_str.find("add:");
        friend_name = ret_str.substr(p1+4, ret_str.length()-4);
        //pass = ret_str.substr(p2+5, ret_str.length()-p2-5);
        //判断该用户名是否存在
        string search_sql = "select * from user where name=\"";
        search_sql += friend_name;
        search_sql += "\";";
        cout<<"sql语句："<<search_sql<<endl;
        //用户名在数据库中存在
        if(sqlDB.search(search_sql)) {
            auto row = mysql_fetch_row(sqlDB.result);//获取一行的信息
            cout<<"查询到用户名"<<row[0]<<" 密码："<<row[1]<<endl;
            
            //list<string> *ll = new list<string>;
            //ll->push_front(friend_name);
            friends[login_name].emplace_front(friend_name);
            //nums[friends[socket_fd]].push_front(friend_name);
            for(auto it = friends[login_name].begin(); it != friends[login_name].end(); it++) {
                cout<<*it<<endl;
            }            
            string reply_str = "ok";
            write(socket_fd, reply_str.c_str(), reply_str.length());
        }
        //数据库中不存在该用户
        else {
            string reply_str = "wrong";
            write(socket_fd, reply_str.c_str(), reply_str.length());
        }
    }
    //接收到的是私聊的信息，设定目标的文件描述符
    else if(ret_str.find("target:") != ret_str.npos) {
        int pos1 = ret_str.find("from");
        string target = ret_str.substr(7, pos1-7);
        cout<<"target:"<<target<<endl;
        string from = ret_str.substr(pos1+5);
        cout<<"from:"<<from<<endl;
        
        target_name = target;
        if(name_sock_map.find(target) == name_sock_map.end()) {
            cout<<"源用户为"<<from<<",目标用户"<<target_name<<"仍未登陆，无法发起私聊\n";
            string send_str = "wrong";
            write(socket_fd, send_str.c_str(), send_str.length());
        }
            
        else {
            //判断是否为好友
            auto it = friends[login_name].begin();
            for(; it != friends[login_name].end(); it++) {
                cout<<"*it:"<<*it<<endl;
                if(*it == target_name) {
                    cout<<"target_num"<<*it<<endl;
                    break;
                }
            }
            if(it == friends[login_name].end()) {
                cout<<"目标用户"<<target_name<<"不是该用户的好友，无法发起私聊\n";
                string send_str = "not_friend";
                write(socket_fd, send_str.c_str(), send_str.length());
            }
            else {
                pthread_mutex_lock(&from_mutex);
                from_to_map[from] = target;
                pthread_mutex_unlock(&from_mutex);
                login_name = from;
                cout<<"源用户"<<login_name<<"向目标用户"<<target_name<<"发起的私聊即将建立";
                cout<<",目标用户的套接字描述符为"<<name_sock_map[target]<<endl;
                target_conn = name_sock_map[target];
                string send_str = "ok";
                write(socket_fd, send_str.c_str(), send_str.length());
            }
        }
    
    }
    //接收到的是待转发的信息
    else if(ret_str.find("content:") != ret_str.npos) {
        //根据两个map找出当前用户和目标用户
        for(auto i:name_sock_map) {
            if(i.second == socket_fd){
                login_name = i.first;
                target_name = from_to_map[i.first];
                target_conn = name_sock_map[target_name];
                break;
            }       
        }
        if(target_conn == -1) {
            cout<<"找不到目标用户"<<target_name<<"的套接字，将尝试重新寻找目标用户的套接字\n";
            if(name_sock_map.find(target_name) != name_sock_map.end()){
                target_conn = name_sock_map[target_name];
                cout<<"重新查找目标用户套接字成功\n";
            }
            else{
                cout<<"查找仍然失败，转发失败！\n";
            }
        }
        string recv_str(ret_str);
        string send_str = recv_str.substr(8);
        cout<<"用户"<<login_name<<"向"<<target_name<<"发送:"<<send_str<<endl;
        send_str="["+login_name+"]:"+send_str;
        write(target_conn, send_str.c_str(), send_str.length());
    }
    
    //绑定群聊号
    else if(ret_str.find("group:") != ret_str.npos){
        string recv_str(ret_str);
        string num_str = recv_str.substr(6);
        group_num = stoi(num_str);
        //找出当前用户
        for(auto i:name_sock_map)
            if(i.second == socket_fd){
                login_name = i.first;
                break;
            }
        cout<<"用户"<<login_name<<"绑定群聊号为："<<num_str<<endl;
        pthread_mutex_lock(&group_mutex);//上锁
        //group_map[group_num].push_back(conn);
        group_map[group_num].insert(socket_fd);
        pthread_mutex_unlock(&group_mutex);//解锁
    }

    //广播群聊信息
    else if(ret_str.find("gr_message:") != ret_str.npos) {
        //找出当前用户
        for(auto i:name_sock_map)
            if(i.second == socket_fd) {
                login_name = i.first;
                break;
            }
        //找出群号
        for(auto i:group_map) {
            if(i.second.find(socket_fd) != i.second.end()){
                group_num = i.first;
                break;
            }
        }
        string send_str(ret_str);
        send_str = send_str.substr(11);
        send_str="[" + login_name + "]:" + send_str;
        cout<<"群聊信息："<<send_str<<endl;
        for(auto i:group_map[group_num]) {
            //向群聊中其他所有用户发送信息
            if(i != socket_fd)
                send(i,send_str.c_str(),send_str.length(),0);
        }       
    }  
    cout<<"---------------------"<<endl;
    if(!redis_target->err)
        redisFree(redis_target);


    // //保存这个socket对应的客户端的地址和端口
    // struct sockaddr_in peer_addr;
    // int peer_len = sizeof(struct sockaddr_in);//必须这样写
    // getpeername(socket_fd, (struct sockaddr *)&peer_addr, (socklen_t *)&peer_len); //获取对应客户端相应信息
    // char temp[1024];
    // memset(temp, 0, sizeof(temp));
    // const char* client_addr = inet_ntop(AF_INET, &peer_addr.sin_addr, temp, 1024);
    // int client_port = ntohs(peer_addr.sin_port);
    // if(size > 0) {
    //     //涉及到string，用cout输出
    //     cout<<"message recv "<<size<<"Bytes from client whose ip is "<<client_addr<<" and port is "<<client_port<<endl;
    //     cout<<"Bytes: "<<ret<<endl;
    //     //printf("message recv %dByte from server whose ip is %s and port is %d.\nBytes: %s\n", size, server_addr, server_port, ret);
    // }
    // else if(size < 0) {
    //     printf("recv failed!errno code is %d,errno message is '%s'\n",errno, strerror(errno));
    //     exit(1);
    // }
    // else {
    //     printf("client disconnect whose ip is %s and port is %d!\n", client_addr, client_port);
    //     exit(1);
    // }	    
    // for(int i = 0; i < size; i++) {
    //     if(ret[i] >= 'a' && ret[i] <= 'z') {
    //         ret[i] = toupper(ret[i]);
    //     }
    // }
    // noblockWrite(ret, socket_fd);
    // printf("process data over!\n");
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        printf("format error!\n");
		printf("usage: server <address> <port>\n");
		exit(1);
    }
    //1.首先需要建立一个eventpool，当然，需要用文件描述符标识
    int eventpool_fd = epoll_create(MAX_EVENT+10); //这个参数就是出初始化红黑树节点的个数，必须大于max_event
    if(eventpool_fd < 0) {
        perror("epoll create error:");
        exit(1);
    }
    FileRAII _event_fd(eventpool_fd);//RTII
    int event_fd = _event_fd.get();
    //2.接下来进行socket的创建和绑定及监听工作
    //int server_fd = init_server(argv);
    struct sockaddr_in server_addr;//地址
    server_addr.sin_family = AF_INET;//ipv4协议
    server_addr.sin_port = htons(atoi(argv[2]));//绑定端口
    if(!inet_aton(argv[1], (struct in_addr*)&server_addr.sin_addr.s_addr)) {//将点分十进制ip地址转换为对应形式
        perror("invalid ip addr:");
		exit(1); 
    }
    //2.1.创建套接字
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0) {
        perror("socket create fail:");
        exit(1);
    }
    FileRAII _ser_fd(server_fd);//！！！利用RAII
    int ser_fd = _ser_fd.get();
    //2.2.端口重用，如果不这样设置的话，建立TCP连接后，将无法再监听其他的客户端
	int reuse = 0x01;
	if(setsockopt(ser_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(int)) < 0) {
		perror("setsockopt error");
		// close(server_fd);
		exit(1); 
	}
    //2.3.绑定套接字(ip,端口)
    if(bind(ser_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind error:");
        // close(server_fd);
        exit(1);
    }
    //2.4.开始监听
    if(listen(ser_fd, 127) < 0) {
        perror("listen error");
        //close(server_fd);
        exit(1);
    }    

    struct epoll_event event;//定义插入时的epoll事件
    struct epoll_event wait_event[MAX_EVENT];//定义最终返回的触发的事件描述符及其他信息
    //int flag;//阻塞非阻塞标志
    event.data.fd = ser_fd;
    event.events = EPOLLIN | EPOLLET;//建立的连接请求对应的server_fd只能读，不能修改，必须为边沿触发，为了一次性处理全部连接请求，以及防止客户端建立连接过程中发送RST
    set_noblock(ser_fd);
    //3.将server_fd以阻塞形式放入eventpool的就绪队列，等待请求连接
    if(epoll_ctl(event_fd, EPOLL_CTL_ADD, ser_fd, &event) < 0) {
        perror("epoll_ctl error:");
        //close(event_fd);

        exit(1);
    }
    //连接mysql数据库
    MyDB sqlDB;
    if(!sqlDB.InitDB("localhost","root","gll061200","test_connect")) {
        return 0;
    }
    string search_str = "SELECT * FROM user;";
    if(!sqlDB.search(search_str.c_str())) {
        cout<<"连接数据库失败！\n";
    }
    //auto search_res=mysql_query(sqlDB.,search.c_str());
    //auto result=mysql_store_result(con);
    //int row;
    // if(sqlDB.result)
    cout<<"连接数据库成功！\n准备初始化布隆过滤器\n";
    //读取数据并完成布隆过滤器初始化
    memset(Bloom_Filter_bitmap, 0, sizeof(Bloom_Filter_bitmap));
    for(int i=0;i<sqlDB.rows;i++){
        auto info = mysql_fetch_row(sqlDB.result);//获取一行的信息
        string read_name = info[0];
        //对字符串使用哈希函数
        int hash = 0;
        //cout<<"字符串："<<read_name;
        for(auto ch:read_name) {
            hash = hash * 131 + ch;
            if(hash >= 10000000)
                hash %= 10000000;
        }
        hash %= 32000000;
        //cout<<",hash值为："<<hash;
        //调整bitmap
        int index = hash/32, pos = hash%32;
        //cout<<index<<" "<<pos<<endl;
        Bloom_Filter_bitmap[index] |= (1<<pos);
        //cout<<",调整后的："<<Bloom_Filter_bitmap[index]<<endl;
    }
    int one = 0, zero = 0;
    for(auto i:Bloom_Filter_bitmap) {
        int b=1;
        for(int j = 1; j <= 32; j++) {
            if(b&i)
                one++;
            else
                zero++;
            b = (b<<1);
        }
    }
    cout<<"布隆过滤器中共有"<<one<<"位被置为1，其余"<<zero<<"位仍为0"<<endl;


    //建立线程池
    threadPool pool(2, 5, 100);
    //threadPool_create(100, 100);
    //客户端的ip地址和端口
    struct sockaddr_in client;
    int size_sock = sizeof(struct sockaddr_in);//地址长度
    char buf[1024];//保存接受数据和发送数据，使用前记得清空

    FileRAII _sock_fd[MAX_EVENT+10];
    int num = 0;//文件数目
    while(1) {
        cout<<"--------------------------"<<endl;
        cout<<"epoll_wait阻塞中"<<endl;
        //4.等待事件触发。这里有一个问题，虽然连接socket最好使用水平触发，但他没法和其他socket区分开，因此只能都使用边沿触发 XXX理解错了！！！
        int fd_size = epoll_wait(event_fd, wait_event, MAX_EVENT, -1);
        if(fd_size < 0) {
            perror("perror wait error:");
            exit(1);
        }
        cout<<"epoll_wait返回，有事件发生"<<endl;
        for(int i = 0; i < fd_size; i++) {
            //5.1.事件触发是连接
            if(wait_event[i].data.fd == ser_fd) {//说明是建立连接的请求  
                //得到发送文件对应的描述符和其中的ip和端口
                int socket_fd;
                //5.1.1循环处理所有连接，并将得到的文件描述符加入pool
                while((socket_fd = accept(ser_fd, (struct sockaddr*)&client, (socklen_t *)&size_sock)) > 0) {
                    _sock_fd[num].change(socket_fd);
                    int sock_fd = _sock_fd[num].get();
                    num++;
                    printf("connected with ip: %s and port: %d\n", inet_ntop(AF_INET, &client.sin_addr, buf, 1024), ntohs(client.sin_port));
                    set_noblock(sock_fd);
                    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;//此时必须设置为边沿触发，提高效率
                    //event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//此时必须设置为边沿触发，提高效率
                    event.data.fd = sock_fd;
                    if(epoll_ctl(event_fd, EPOLL_CTL_ADD, sock_fd, &event) < 0) {
                        perror("epoll_ctl error:");
                        break;
                    }
                }
                if(socket_fd < 0) {//判断出错原因是否为连接全部建完
                    if(errno != EAGAIN) {//不是读完，则出错
                        perror("accept error:");
                    }
                }
                continue;
            }
            //5.2客户端断开
            else if(wait_event[i].events & EPOLLRDHUP) {
                struct sockaddr_in peer_addr;
                int peer_len = sizeof(struct sockaddr_in);//必须这样写
                getpeername(wait_event[i].data.fd, (struct sockaddr *)&peer_addr, (socklen_t *)&peer_len); //获取对应客户端相应信息
                char temp[1024];
                memset(temp, 0, sizeof(temp));
                const char* client_addr = inet_ntop(AF_INET, &peer_addr.sin_addr, temp, 1024);//加了const
                int client_port = ntohs(peer_addr.sin_port);
            	printf("client disconnect whose ip is %s and port is %d!\n", client_addr, client_port);
            }
            //5.3 如果不是连接请求，那就是socket数据包的读写请求，或者断开连接的请求
            else {
                struct Fds	fds_for_new_worker;
				fds_for_new_worker.epoll_fd = event_fd;
				fds_for_new_worker.sock_fd = wait_event[i].data.fd;

                //pool.threadpool_add(process_data, wait_event[i].data.fd);
                pool.threadpool_add(process_data, (void *)&fds_for_new_worker);
                continue;
            }
        }
    }
    return 0;

}
