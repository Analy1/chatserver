#define _CRT_SECURE_NO_WARNINGS
#include"public.h"
#include"Connection.h"
#include"Logger.hpp"
#include<iostream>
using namespace std;

Connection::Connection()
{
	//初始化数据库连接
	_conn = mysql_init(nullptr);//只分配了内存，还没连接数据库
}
//释放数据库连接资源
Connection::~Connection()
{
	if (_conn != nullptr)
	{
		LOG_DEBUG << "MySQL连接关闭";
		mysql_close(_conn);
	}
}
//连接数据库

bool Connection::connect(string ip, unsigned short port, string user, string password, string dbname)
{
	MYSQL* p = mysql_real_connect(_conn, ip.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, 0);
	if (p != nullptr)
	{
		LOG_INFO << "MySQL连接成功: " << ip << ":" << port << ", db=" << dbname;
	}
	else
	{
		LOG_ERROR << "MySQL连接失败: " << ip << ":" << port << ", db=" << dbname;
	}
	return p != nullptr;
}
//更新操作 insert、delete、update
//执行成功返回 0，失败返回 非0
bool Connection::update(string sql)
{
	if (mysql_query(_conn, sql.c_str()))
	{
		LOG_ERROR << "MySQL更新失败, sql=" << sql;
		return false;
	}
	return true;
}
//查询操作 select
MYSQL_RES* Connection::query(string sql)
{
	if (mysql_query(_conn, sql.c_str()))
	{
		LOG_ERROR << "MySQL查询失败, sql=" << sql;
		return nullptr;
	}
	return mysql_use_result(_conn);
}

// 获取连接
MYSQL *Connection::getConnection()
{
    return _conn;
}