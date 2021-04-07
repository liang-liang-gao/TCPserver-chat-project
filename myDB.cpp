/*************************************************************************                      
  > File Name: myDB.cpp
  > Author: gll
  > Mail: 2786768371@qq.com
  > Created Time: 2021年04月04日 星期日 22时27分18秒
************************************************************************/

#include <iostream>
#include <string>
#include <stack>
#include <algorithm>   
#include <mysql/mysql.h> 
#include "myDB.h"
 
using namespace std;
 
MyDB::MyDB() {
    mysql = mysql_init(NULL);
    if(mysql == NULL) {
        cout << "Error: " << mysql_error(mysql);
        exit(-1);
    }      
}

MyDB::~MyDB() {                                                           
    if(!mysql) {
        mysql_close(mysql);
    }
}
 
bool MyDB::InitDB(string host,string user,string pwd,string dbname) {
  /*连接数据库*/
    if(!mysql_real_connect(mysql,host.c_str(),user.c_str(),pwd.c_str(),dbname.c_str(),6789,NULL,0)) {
	    cout << "connect fial: " << mysql_error(mysql);
        exit(-1);
    }
    return true;
}
bool MyDB::insert(string sql) {
    if(mysql_query(mysql,sql.c_str())) {
        cout << "query fail: " << mysql_error(mysql);
        return false;                                                  
    }
    return true;
}
bool MyDB::search(string sql) {
    if(mysql_query(mysql,sql.c_str())) {//执行成功返回0，不成功则不为0
        cout << "query fail: " << mysql_error(mysql);
        return false;                                                  
    }
    result = mysql_store_result(mysql);
    if(!result) {
        cout<<"查询失败！\n";
        return false;
    }
    rows = mysql_num_rows(result);
    if(rows == 0) {
        cout<<"查询失败！\n";
        return false;
    }
    // row = mysql_fetch_row(result);//获取对应一行数据
    //mysql_free_result(result);
    return true;
}
bool MyDB::ExeSQL(string sql) {
    /*执行失败*/
    if(mysql_query(mysql,sql.c_str())) {
        cout << "query fail: " << mysql_error(mysql);
        exit(1);                                                   
    }
    else {
        /*获取结果集*/
        result = mysql_store_result(mysql);
        /*获取结果的总行数*/
        rows = mysql_num_rows(result);
        cout<<rows<<endl;
        /*获取结果的总列数*/
        int fieldnum = mysql_num_fields(result);
        cout<<fieldnum<<endl;
        for(int i=0; i<rows; i++) {
            row = mysql_fetch_row(result);//获取对应一行数据
            if(row <= 0) //查询到最后一行则结束
                break;
            for(int j=0; j<fieldnum; j++) {
                cout << row[j] << "\t\t";
            }
            cout << endl;   
        }
        mysql_free_result(result);
        return true;
    }
}
 
/*************************************************************************                      
  > File Name: main.cpp
  > Author: Tanswer_
  > Mail: 98duxm@gmail.com
  > Created Time: 2017年05月28日 星期日 22时53分43秒
************************************************************************/
/*    
#include <iostream>
#include <string>
#include <stack>
#include <algorithm>
#include <mysql/mysql.h>
#include "myDB.h"
    
using namespace std;
*/
    
// int main()
// {   
//   MyDB db;
//   db.InitDB("localhost","root","gll061200","test_connect");
//   db.ExeSQL("SELECT * FROM user;");
//   return 0;
// } 


