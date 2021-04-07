/*************************************************************************
  > File Name: myDB.h
  > Author: Tanswer_
  > Mail: 98duxm@gmail.com
  > Created Time: 2017年05月28日 星期日 22时26分22秒
************************************************************************/
  
#ifndef _MYDB_H
#define _MYDB_H
  
#include <string>
#include <iostream>
#include <mysql/mysql.h>
using namespace std;
  
class MyDB
{ 
  
public:
  MyDB();
  ~MyDB();
  bool InitDB(string host,string user,string pwd,string dbname);                          
  bool insert(string sql);//插入语句
  bool search(string sql);//查询语句
  bool ExeSQL(string sql);
  MYSQL_ROW row;
  int rows;
  MYSQL* mysql;
  //MYSQL* mysql;
  
  MYSQL_RES* result;
  MYSQL_FIELD* field;                                                
};
#endif
