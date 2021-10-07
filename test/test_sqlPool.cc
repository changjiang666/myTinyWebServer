#include "mysql/sqlConnectionPool.hpp"
#include <iostream>

int main() {
    ConnectionPool* connpool = ConnectionPool::getInstance();
    std::string user = "root";
    std::string passwd = "123456";
    std::string databasename = "webServerDB";

    connpool->init("localhost", user, passwd, databasename, 0, 8, 0);
    MYSQL* conn = nullptr;
    ConnectionPoolRAII mysqlconn(&conn, connpool);


    if (mysql_query(conn, "SELECT username,passwd FROM user"))
    {
        return 0;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        std::cout << row[0] << " " << row[1] << std::endl;
    }
    return 0;
}
