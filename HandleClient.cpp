/*************************************************************************
	> File Name: HandleClient.h
	> Author: gll
	> Mail: 2786768371@qq.com
	> Created Time: sat 4.3 15:20:29 2021
 *************************************************************************/

#include"HandleClient.h"
using namespace std;

void *handle_recv(void *arg){
    int socket_fd = *(int *)arg;
    while(1) {
        char recv_buffer[1000];
        memset(recv_buffer, 0, sizeof(recv_buffer));
        int len = read(socket_fd, recv_buffer, sizeof(recv_buffer));
        if(len == 0)
            continue;
        if(len == -1)
            break;
        string str(recv_buffer);
        cout<<str<<endl;
    }
}

void *handle_send(void *arg){
    int socket_fd = *(int *)arg;
    while(1) {
        string str;
        cin>>str;
        if(str == "exit")
            break;
        if(socket_fd > 0){
            str = "content:" + str;
            write(socket_fd, str.c_str(), str.length());
        }
        else if(socket_fd < 0) {
            str = "gr_message:" + str;
            write(-socket_fd, str.c_str(), str.length());
        }   
    }   
}

