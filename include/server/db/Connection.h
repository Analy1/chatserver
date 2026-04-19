#pragma once
#include <mysql/mysql.h>
#include<string>
#include<ctime>
#include"public.h"
using namespace std;
/*
实现MySQL数据库的操作
*/

//数据库操作类
class Connection
{

public:
	//初始化数据库连接
	Connection();
	//释放数据库连接资源
	~Connection();
	//连接数据库
	bool connect(string ip, 
		unsigned short port,
		string user, 
		string password,
		string dbname);
	//更新操作 insert、delete、update
	bool update(string sql);
	//查询操作 select
	MYSQL_RES* query(string sql);

	//刷新一下连接的起始空闲时间点
	void refAliveTime() { _alivetime == clock(); }// 每次使用连接时调用，重置"空闲计时器"
	//返回存活时间
	clock_t getAliveTime() const{ return clock() - _alivetime; }

    MYSQL *getConnection();
private:
	MYSQL* _conn;//表示和MySQL Server的一条连接
	clock_t _alivetime;//记录进入空闲状态后的起始存活时间  当前时间 - 上次使用时间
};
