/*
client.c  gll  2021/4/7
实现方式：epoll模型 RAII机制
功能：从标准输入接受数据，并发送给服务器端；从服务器端接收处理后的数据
新：
    cookie：
        1.判断是否存在cookie文件，若存在则可以将cookie发送给服务器，其中cookie中保存的是随即生成的sessionid
        2.得到服务器的反馈，若成功则用户便直接登陆，否则需要进一步登陆
    注册：
        1.输入注册的用户名和密码
        2.从服务器得到信息，据此判断是否注册成功。不成功的原因可能有用户名已存在
    登陆：
        1.输入登陆用户名和密码
        2.从服务器得到信息，据此判断是否登陆成功。不成功的原因可能有用户名或密码错误，或者该账户已经登陆，不可重复登陆
        3.若成功登陆，需要将接收到的sessionid保存下来，记录在cookie文件中，方便下次发送
    退出：
        1.需要将退出的消息告诉服务器，使其从数据库中将状态设置为未登录
        2.直接退出即可
    注销：
        1.需要将退出的消息告诉服务器，使其从数据库中将状态设置为未登录
        2.重新进入登陆和注册的界面
    添加好友：
        1.输入要添加的好友名字，并将名字发给服务器
        2.得到服务器反馈，据此判断是否添加成功。不成功的原因可能有要添加的用户不存在，
    私聊：
        1.输入私聊的对象，将对象发送给服务器，
        2.得到服务器反馈，据此判断是否成功建立私聊。不成功的原因有用户不存在或未登录，对象并不是该用户的好友
        3.若成功建立私聊，则创建发送线程和接收线程，分别进行信息的发送和接收工作
    群聊：
        1.输入群聊的群号，将群号发送给服务器
        2.创建发送线程和接收线程，分别进行信息的发送和接收工作
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
#include <iostream>
#include <utility>
#include <fstream>
#include "RAII.h"
#include "global.h"
#include"HandleClient.h"
using namespace std;
#define MAX_EVENT 100

void show_view() {
    cout<<" ------------------\n";
    cout<<"|                  |\n";
    cout<<"| 请输入你要的选项:|\n";
    cout<<"|    0:退出        |\n";
    cout<<"|    1:登录        |\n";
    cout<<"|    2:注册        |\n";
    cout<<"|                  |\n";
    cout<<" ------------------ \n\n";    
}

void show_log(string name, int socket_fd) {
    cout<<"        欢迎回来,"<<name<<endl;
    // if(friends.find(socket_fd) == friends.end()) {
    //     cout<<"您目前没有好友，快去添加好友叭！\n";
    // }
    // else {
    //     cout<<"以下为您的好友列表, 一共有"<<nums[friends[socket_fd]].size()<<"人\n";
    //         for(auto it = nums[friends[socket_fd]].begin(); it != nums[friends[socket_fd]].end(); it++) {
    //             cout<<*it<<endl;
    //         }    
    // }
    cout<<" -------------------------------------------\n";
    cout<<"|                                           |\n";
    cout<<"|          请选择你要的选项：               |\n";
    cout<<"|              0:退出                       |\n";
    cout<<"|              1:发起单独聊天               |\n";
    cout<<"|              2:发起群聊                   |\n";
    cout<<"|              3:添加好友                   |\n";
    cout<<"|              4:登陆其他帐号               |\n";
    cout<<"|                                           |\n";
    cout<<" ------------------------------------------- \n\n";    
}

string noblockRead(int socket_fd) {
    char buf[5];
    //必须通过循环将stdin中的所有数据全部写入文件中
    int size;
    string ret;
    while(1) {
        memset(buf, 0, sizeof(buf)); 
        size = read(socket_fd, buf, sizeof(buf)-1);
        if(size < 0) {
            //判断是否是读完
            if(errno == EAGAIN) {
                //printf("read over!\n");
                break;
            }
            //否则出错
            printf("read failed!errno code is %d,errno message is '%s'\n",errno, strerror(errno));
        }
        buf[size] = '\0'; 
        if(buf[1] == '\0') {
            printf("please enter message to send:\n"); 
            return ret;
        }
        for(int i = 0; i < size; i++) {
            ret += buf[i];
            //cout<<ret[i]<<endl;
        }
    }
    return ret;
}

void process_stdin(int client_fd) {
    string ret = noblockRead(STDIN_FILENO);//从标准输入读取数据
    if(ret.size()) {
        int _size = ret.size();
        char* ans = new char[_size];
        strcpy(ans, ret.c_str());
        write(client_fd, ans, _size);
        printf("Bytes: %d\n", _size);
        printf("write data over!\n");
        delete ans;
    }
}

void process_server(int client_fd, int socket_fd) {
    string ret = noblockRead(client_fd);
    int size = ret.size();

    //保存这个client对应的server的地址和端口
    struct sockaddr_in peer_addr;
    int peer_len = sizeof(struct sockaddr_in);//必须这样写
    getpeername(socket_fd, (struct sockaddr *)&peer_addr, (socklen_t*)&peer_len); //获取对应server相应信息
    //保存server的ip和port
    char temp[1024];
    memset(temp, 0, sizeof(temp));
    const char* server_addr = inet_ntop(AF_INET, &peer_addr.sin_addr, temp, 1024);
    int server_port = ntohs(peer_addr.sin_port);
    if(size > 0) {
        //涉及到string，用cout输出
        cout<<"message recv "<<size<<"Bytes from server whose ip is "<<server_addr<<" and port is "<<server_port<<endl;
        cout<<"Bytes: "<<ret<<endl;
        //printf("message recv %dByte from server whose ip is %s and port is %d.\nBytes: %s\n", size, server_addr, server_port, ret);
    }
    else if(size < 0) {
        printf("recv failed!errno code is %d,errno message is '%s'\n",errno, strerror(errno));
        exit(1);
    }
    else {
        printf("server disconnect 1!\n");
        exit(1);
    }	
}
int main(int argc, char* argv[]) {
    if(argc != 3) {
        printf("format error!\n");
		printf("usage: server <address> <port>\n");
		exit(1);
    }

    //2.接下来进行socket的创建和绑定及监听工作
    struct sockaddr_in client_addr;//地址
    client_addr.sin_family = AF_INET;//ipv4协议
    client_addr.sin_port = htons(atoi(argv[2]));//绑定端口
    if(!inet_aton(argv[1], (struct in_addr*)&client_addr.sin_addr.s_addr)) {//将点分十进制ip地址转换为对应形式
        perror("invalid ip addr:");
		exit(1); 
    }
    //1.创建套接字
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(client_fd < 0) {
        perror("socket create fail:");
        exit(1);
    }
    FileRAII _clit_fd(client_fd);
    int clit_fd = _clit_fd.get();
    //2.和server建立连接
	int size_sock = sizeof(client_addr);
	if(connect(clit_fd, (const struct sockaddr*)&client_addr, size_sock) < 0) {
		perror("connect error");
		//close(client_fd);
		exit(1); 
	}


    //3.接下来开始进行与用户交互
    int choice;//输入的操作选项
    string name, pass, again_pass;//注册时需要再次输入的密码
    bool if_login = false;//记录是否登录成功
    string login_name;//记录成功登录的用户名 

    //新增：发送本地cookie，并接收服务器答复，如果答复通过就不用登录
    //先检查是否存在cookie文件
    ifstream f("cookie.txt");
    string cookie_str;
    if(f.good()){
        f>>cookie_str;
        f.close();
        cookie_str = "cookie:" + cookie_str;
        //将cookie发送到服务器
        write(clit_fd, cookie_str.c_str(), cookie_str.length());
        //接收服务器答复
        char cookie_ans[100];
        memset(cookie_ans, 0, sizeof(cookie_ans));
        read(clit_fd, cookie_ans, sizeof(cookie_ans));
        //判断服务器答复是否通过
        string ans_str(cookie_ans);
        //该账户已经登陆
        if(ans_str.find("again:") != ans_str.npos) {
            name = ans_str.substr(6);
            cout<<"用户"<<name<<"已经登陆!"<<endl;
        }
        //该账户未登录过
        else if(ans_str != "NULL") {//redis查询到了cookie，通过
            if_login = true;
            login_name = ans_str;
        }
    }       
    //如果没有登陆，那就供用户选择
    while(1) {
        if(!if_login) {
            show_view();//展示客户端界面
        }
        while(1) {//循环的目的是用户输入错误则重复输入
            if(if_login)
                break;
            cin>>choice;
            if(choice == 0) {//退出
                break;
            }
            //登录
            else if(choice == 1) {
                while(1) {
                    //输入用户名和密码
                    cout<<"用户名:";
                    cin>>name;
                    cout<<"密码:";
                    cin>>pass;
                    string str = "login:" + name;//加一个标记登陆的字符串
                    str += "pass:";
                    str += pass;
                    //将用户名和密码发送给服务器
                    write(clit_fd, str.c_str(), str.length());//发送登录信息
                    //接收服务器的反馈
                    char buf[1000];
                    memset(buf, 0, sizeof(buf));
                    read(clit_fd, buf, sizeof(buf));//接收响应
                    //extern int num;
                    //cout<<"num:"<<num<<endl;
                    string ans_str(buf);
                    //登陆成功
                    if(ans_str.substr(0,2) == "ok") {
                        if_login = true;
                        login_name = name;

                        //新增：本地建立cookie文件保存sessionid
                        string tmpstr = ans_str.substr(2);
                        //创建新文件cookie.txt，并输入文件内容，遇到end则表示输入结束
                        tmpstr = "cat > cookie.txt <<end \n" + tmpstr + "\nend";
                        system(tmpstr.c_str());
                        cout<<"登陆成功\n\n";
                        break;
                    }
                    //登陆失败
                    else if(ans_str.find("wrong") != ans_str.npos) {
                        cout<<"用户名或密码错误！\n\n";
                    }
                    else {
                        cout<<"该账号已经登陆！\n\n";
                    }
                        
                }       
            }
            //注册
            else if(choice == 2) {
                while(1) {
                    cout<<"注册的用户名:";
                    cin>>name;
                    while(1) {//循环是为了解决密码输入错误的情况
                        cout<<"密码:";
                        cin>>pass;
                        cout<<"确认密码:";
                        cin>>again_pass;
                        if(pass == again_pass)
                            break;
                        else
                            cout<<"两次密码不一致!\n\n";
                    }   
                    name = "signup:" + name;//加一个标记注册的字符串
                    pass = "pass:" + pass;
                    string str = name + pass;
                    write(clit_fd, str.c_str(), str.length());
                    //接收服务器，判断是否注册成功
                    char buf[1000];
                    memset(buf, 0, sizeof(buf));
                    read(clit_fd, buf, sizeof(buf));//接收响应
                    string ans_str(buf);
                    //注册成功
                    if(ans_str.substr(0,2) == "ok") {            
                        cout<<"注册成功！\n";
                        cout<<"\n继续输入你要的选项:";
                        break;
                    }     
                    //注册失败
                    else {
                        cout<<"该用户名已经存在！\n";
                    }
                }     
            }
            if(if_login)//此时可能已经登陆成功 
                break;
        }
        //接下来可能登陆成功，可能选择退出
        while(if_login) {
            if(if_login) {
                //system("clear");
                //登陆成功，展示登录界面
                show_log(login_name, clit_fd);
            }
            //再次判断操作
            cin>>choice;
            pthread_t send_t, recv_t;//线程ID
            void *thread_return;
            //更换账号登陆
            if(choice == 4) {
                //告诉服务器要下线
                string str = "logout:" + name;
                cout<<"内容："<<str<<endl;
                if(write(clit_fd, str.c_str(), str.length()) < 0) {
                    cout<<"write error\n";
                }
                char temp[10];
                read(clit_fd, temp, sizeof(temp));
                if_login = false;//重新进入未登陆状态
            }
            if(choice == 0) {
                //告诉服务器要下线
                string str = "logout:" + name;
                cout<<"内容："<<str<<endl;
                if(write(clit_fd, str.c_str(), str.length()) < 0) {
                    cout<<"write error\n";
                }
                char temp[10];
                read(clit_fd, temp, sizeof(temp));
                return 0;//直接退出即可
            }
            //私聊
            if(choice == 1) {
                cout<<"请输入您想聊天的好友的用户名：";
                string target_name, content;//用户名和聊天内容
                cin>>target_name;
                string send_str("target:" + target_name + "from:" + login_name);//标识目标用户+源用户
                write(clit_fd, send_str.c_str(), send_str.length());//先向服务器发送目标用户、源用户
                char temp[10];
                memset(temp, 0, sizeof(temp));
                read(clit_fd, temp, sizeof(temp));
                string ans(temp);
                if(ans.find("wrong") != ans.npos) {
                    cout<<"目标用户"<<target_name<<"仍未登陆，私聊建立失败\n";
                    continue;
                }
                if(ans.find("not_friend") != ans.npos) {
                    cout<<"目标用户"<<target_name<<"不是您的好友，私聊建立失败\n";
                    continue;
                }
                cout<<"私聊建立成功，请输入你想说的话(输入exit退出)：\n";
                //创建多线程去进行私聊工作
                auto send_thread = pthread_create(&send_t, NULL, handle_send, (void *)&clit_fd);//创建发送线程
                auto recv_thread = pthread_create(&recv_t, NULL, handle_recv, (void *)&clit_fd);//创建接收线程
                pthread_join(send_t,&thread_return);
                //pthread_join(recv_t,&thread_return);
                pthread_cancel(recv_t);
            }
            //群聊
            if(choice == 2) {
                cout<<"请输入群号:";
                int num;
                cin>>num;
                string sendstr("group:" + to_string(num));
                write(clit_fd, sendstr.c_str(), sendstr.length());
                cout<<"请输入你想说的话(输入exit退出)：\n";
                int clit_fd1 = -clit_fd;//特殊标记需要群发的消息，方便发送的线程进行区分
                auto send_thread = pthread_create(&send_t,NULL,handle_send,(void *)&clit_fd1);//创建发送线程
                auto recv_thread = pthread_create(&recv_t,NULL,handle_recv,(void *)&clit_fd);//创建接收线程
                pthread_join(send_t, &thread_return);
                pthread_cancel(recv_t);

            }
            //添加好友
            if(choice == 3) {
                cout<<"请输入您想添加的好友的用户名: ";
                // string friend_str;
                // string friend_name;
                cin>>friend_name;
                string friend_str = "add:" + friend_name;//标记为添加好友
                //让服务器去判断是否存在这个用户
                write(clit_fd, friend_str.c_str(), friend_str.length());
                char buf[1000];
                memset(buf, 0, sizeof(buf));
                read(clit_fd, buf, sizeof(buf));//接收响应
                string ans_str(buf);
                //存在该用户
                if(ans_str.substr(0,2) == "ok") {
                    // if(friends.find(login_name) == friends.end()) {
                    //     //list<string> friends_list;
                    //     //friends_list.push_front(friend_str);
                    //     //friends.insert(make_pair(login_name, friends_list));
                    //     //friends[login_name].emplace_front(friend_name);
                    // }
                    // else {
                    //     // friends[login_name].emplace_front(friend_name);
                    // }
                    cout<<"成功添加好友"<<friend_name<<"，快去找他聊天叭！\n";
                }
                else {
                    cout<<"该用户不存在！\n";
                }
            }
        }


    }
 
    
    return 0;
}