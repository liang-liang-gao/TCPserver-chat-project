#include <iostream>
#include <string>
#include <stack>
#include <algorithm>
#include <mysql/mysql.h>
#include "myDB.h"
    
using namespace std;
    
    
int main() {   
    MyDB db;
    db.InitDB("localhost","root","gll061200","test_connect");
    db.ExeSQL("SELECT * FROM user;");
    return 0;
}   
