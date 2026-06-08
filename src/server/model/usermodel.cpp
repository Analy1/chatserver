#include "usermodel.hpp"
//#include "db.h"
#include<iostream>
#include "server/db/Connection.h"
#include "server/db/CommonConnectionPool.h"
#include "Logger.hpp"
using namespace std;

//User表的增加方法
bool UserModel::insert(User &user)
{
    //组装sql语句
    char sql[1024] = {0};
    sprintf(sql,"insert into user(name,password,state) values('%s','%s','%s')",
        user.getName().c_str(),user.getPwd().c_str(),user.getState().c_str());


    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        if(conn->update(sql))
        {
            user.setId(mysql_insert_id(conn->getConnection()));
            LOG_INFO << "用户注册成功: id=" << user.getId() << ", name=" << user.getName();
            return true;
        }
    }
    LOG_ERROR << "用户注册失败: name=" << user.getName();
    return false;
    
}

//根据用户号码查询用户信息
User UserModel::query(int id)
{
    //组装sql语句
    char sql[1024] = {0};
    sprintf(sql,"select * from user where id = %d",id);
    

    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        MYSQL_RES *res = conn->query(sql);
        if(res != nullptr)
        {
           MYSQL_ROW row = mysql_fetch_row(res);
           if(row != nullptr)
           {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                LOG_DEBUG << "查询用户: id=" << user.getId() << ", name=" << user.getName() << ", state=" << user.getState();
                return user;
           }
        }
    }

    LOG_DEBUG << "未找到用户: id=" << id;
    return User();
}

//更新用户的状态信息
bool UserModel::updateState(User user)
{
    //组装sql语句
    char sql[1024] = {0};
    sprintf(sql,"update user set state = '%s' where id = %d" ,user.getState().c_str(),user.getId());
    
    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        if(conn->update(sql))
        {
            LOG_DEBUG << "更新用户状态: id=" << user.getId() << ", state=" << user.getState();
            return true;
        }
    }
    LOG_ERROR << "更新用户状态失败: id=" << user.getId();
    return false;
    
}

//重置用户的状态信息
void UserModel::resetState()
{
    //组装sql语句
    char sql[1024] = {"update user set state = 'offline' where state = 'online'"};


    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        conn->update(sql);
        LOG_INFO << "重置所有在线用户为离线状态";
    }

}