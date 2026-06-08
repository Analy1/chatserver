#include "offlinemessagemodel.hpp"
//#include "db.h"
#include "server/db/Connection.h"
#include "server/db/CommonConnectionPool.h"
#include "Logger.hpp"
// 存储用户的离线消息
void OfflineMsgModel::insert(int userid, string msg)
{
    //组装sql语句
    char sql[1024] = {0};
    sprintf(sql,"insert into offlinemessage values('%d','%s')",userid,msg.c_str());


    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        conn->update(sql);
        LOG_INFO << "存储离线消息: userid=" << userid;
    }

}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    //组装sql语句
    char sql[1024] = {0};
    sprintf(sql,"delete from offlinemessage where userid=%d",userid);
    

    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        conn->update(sql);
        LOG_INFO << "清除用户离线消息: userid=" << userid;
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    //组装sql语句
    char sql[1024] = {0};
    sprintf(sql,"select message from offlinemessage where userid = %d",userid);
    
    
    vector<string> vec;
    auto conn = ConnectionPool::getConnectionPool()->getConnection();
    if(conn != nullptr)
    {
        MYSQL_RES *res = conn->query(sql);
        if(res != nullptr)
        {
           
           //把userid用户的所有离线消息放入vec中返回
           MYSQL_ROW row ;
           
           while((row = mysql_fetch_row(res)) != nullptr)
           {
                vec.push_back(row[0]);
           }

           mysql_free_result(res);
           LOG_DEBUG << "查询离线消息: userid=" << userid << ", 消息数=" << vec.size();
           return vec;
    }
    }
    LOG_DEBUG << "未找到离线消息: userid=" << userid;
    return vec;
    
}