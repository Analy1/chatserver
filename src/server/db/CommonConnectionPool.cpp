#define _CRT_SECURE_NO_WARNINGS
#include"CommonConnectionPool.h"

// 全局默认：开启连接池（你现在项目正常运行的状态）
bool ConnectionPool::pool_enable = false;

//线程安全的懒汉单例函数接口
ConnectionPool* ConnectionPool::getConnectionPool()
{
	static ConnectionPool pool;// C++11保证线程安全的局部静态变量
	return &pool;
}

bool ConnectionPool::loadConfigFile()
{
	FILE* pf = fopen("../mysql.cnf", "r");
	if (pf == nullptr)
	{
		LOG("mysql.init file is not exist!");
		return false;
	}

	while (!feof(pf))//检查是否到达文件末尾
	{
		char line[1024] = { 0 };
		fgets(line, 1024, pf);
		string str = line;
		int idx = str.find('=', 0);//找到等号位置
		if (idx == -1)//无效的配置项
		{
			continue;
		}

		//password=123456\n
		int endidx = str.find('\n', idx);//找到换行符位置
		string key = str.substr(0, idx);//提取开始到等号前的位置（也就是命名）
		string value = str.substr(idx + 1, endidx - idx - 1);//提取从等号后面到换行符前面的字符

		if (key == "ip")
		{
			_ip = value;
		}
		else if (key == "port")
		{
			_port = atoi(value.c_str());//将字符串转换为整数
		}
		else if (key == "username")
		{
			_username = value;
		}
		else if (key == "password")
		{
			_password = value;
		}
		else if (key == "dbname")
		{
			_dbname = value;
		}
		else if (key == "initSize")
		{
			_initSize = atoi(value.c_str());
		}
		else if (key == "maxSize")
		{
			_maxSize = atoi(value.c_str());
		}
		else if (key == "maxIdleTime")
		{
			_maxIdleTime = atoi(value.c_str());
		}
		else if (key == "connectionTimeOut")
		{
			_connectionTimeOut = atoi(value.c_str());
		}

		std::cout << std::endl;
	}
	return true;
}


//连接池的构造
ConnectionPool::ConnectionPool()
{
	//加载配置项
	if (!loadConfigFile())
	{
		return;
	}

	//创建初始数量的连接
	for (int i = 0;i < _initSize;i++)
	{
		Connection* p = new Connection();
		p->connect(_ip,_port,_username,_password,_dbname);
		p->refAliveTime();//刷新一下开始空闲的起始时间
		_connectionQue.push(p);
		_connectionCnt++;
	}

	//启动一个新的线程，作为连接的生产者
	thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
	produce.detach();

	//启动一个新的定时线程，扫描超过maxIdleTIme时间的空闲连接，进行对于的连接回收
	thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
	scanner.detach();
}

//运行在独立的线程中，专门负责生产新连接
void ConnectionPool::produceConnectionTask()
{
	for (;;)
	{
		unique_lock<mutex> lock(_queueMutex);
        // 队列不空 → 不需要生产，等待
		while (!_connectionQue.empty())
		{
			cv.wait(lock);//队列不空，此生产线程进入等待状态
		}

		//连接数量没有到达上限，继续创建新的连接
		if (_connectionCnt < _maxSize)
		{
			Connection* p = new Connection();
			p->connect(_ip, _port, _username, _password, _dbname);
			p->refAliveTime();//刷新一下开始空闲的起始时间
			_connectionQue.push(p);
			_connectionCnt++;
		}
		//通知消费者线程，可以消费连接了
		cv.notify_all();
	}
}


//给外部提供接口，从连接池中获取一个可用的空闲连接
shared_ptr<Connection> ConnectionPool::getConnection()
{

	// ===================== 【新增：连接池开关】 =====================
    // 如果关闭连接池 → 直接新建短连接，不走池子
    if (!pool_enable)
    {
        shared_ptr<Connection> conn = make_shared<Connection>();
        conn->connect("127.0.0.1", 3306, "root", "111111", "chat");  // 建立独立连接
        return conn;
    }


	unique_lock<mutex> lock(_queueMutex);
	while (_connectionQue.empty())
	{
		if (cv_status::timeout == cv.wait_for(lock, chrono::microseconds(_connectionTimeOut)))
		{
			//超时醒来的，而非被唤醒
			if (_connectionQue.empty())
			{
				LOG("获取空闲连接超时了...获取连接失败！");
				return nullptr;
			}
		}
		
	}
	/*
	shared_ptr智能指针析构时，会把connection资源直接delete掉，相当于
	调用connection的析构函数，connection就被close掉了
	这里需要自定义shared_ptr资源释放的方式，把connection直接归还到queue当中
	*/
	
    //取出队首连接
	shared_ptr<Connection> sp(_connectionQue.front(),
		[&](Connection* pcon)
		{
			//这里实在服务器应用线程中调用的，所以一定要考虑队列的线程安全操作
			unique_lock<mutex> lock(_queueMutex);
			pcon->refAliveTime();//刷新一下开始空闲的起始时间
			_connectionQue.push(pcon);
			
		});
	_connectionQue.pop();
	
	cv.notify_all();//消费完连接以后，通知生产者线程检查一下，如果队列为空了，赶紧生产连接
	return sp;
}

//启动一个新的定时线程，扫描超过maxIdleTIme时间的空闲连接，进行对于的连接回收
void ConnectionPool::scannerConnectionTask()
{
	for (;;)
	{
		//通过sleep模拟定时效果
		this_thread::sleep_for(chrono::seconds(_maxIdleTime));

		//扫描整个队列,释放多余连接
		unique_lock<mutex> lock(_queueMutex);
        // 只回收超出初始连接数的部分
		if (_connectionCnt > _initSize)
		{
			Connection* p = _connectionQue.front();
			if (p->getAliveTime() >= _maxIdleTime * 1000)
			{
				_connectionQue.pop();
				_connectionCnt--;
				delete p;//调用~Connection()释放连接 真正释放连接
			}
			else
			{
				break;//对头的连接没有超过_maxIdleTime，其它连接肯定没有
			}
		}
	}
}