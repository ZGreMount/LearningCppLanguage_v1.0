#include "DBpool.h"
#include <string.h>

#define log_error printf
#define log_warn printf
#define lon_info printf
#define MIN_DB_CONN_CNT 2
#define MAX_DB_CONN_FAIL_NUM 10

typedef bool my_bool;

// todo
CResultSet::CResultSet(MYSQL_RES *res)
{
    m_res = res;

    // map table field key to index in thr result array
    int num_fields = mysql_num_fields(m_res);
    MYSQL_FIELD *fields = mysql_fetch_fields(m_res);
    for (int i = 0; i < num_fields; i++)
    {
        m_key_map.insert(make_pair(fields[i].name, i));
    }
}

CResultSet::~CResultSet()
{
    if (m_res)
    {
        mysql_free_result(m_res);
        m_res = NULL;
    }
}

bool CResultSet::Next()
{
    m_row = mysql_fetch_row(m_res);
    if (m_row)
    {
        return true;
    }
    else
    {
        return false;
    }
}

int CResultSet::_GetIndex(const char* key)
{
    map<string, int>::iterator it = m_key_map.find(key);
    if (it == m_key_map.end())
    {
        return -1;
    }
    else
    {
        return it->second;
    }
}

int CResultSet::GetInt(const char* key)
{
    int idx = _GetIndex(key);
    if (idx == -1)
    {
        return 0;
    }
    else
    {
        return atoi(m_row[idx]);
    }
}

char* CResultSet::GetString(const char* key)
{
    int idx = _GetIndex(key);
    if (idx == -1)
    {
        return NULL;
    }
    else
    {
        return m_row[idx];
    }
}


/////////////////////////////////////////
// todo
CPrepareStatement::CPrepareStatement()
{
	m_stmt = NULL;
	m_param_bind = NULL;
	m_param_cnt = 0;
}

CPrepareStatement::~CPrepareStatement()
{
	if (m_stmt)
	{
		mysql_stmt_close(m_stmt);
		m_stmt = NULL;
	}

	if (m_param_bind)
	{
		delete[] m_param_bind;
		m_param_bind = NULL;
	}
}

bool CPrepareStatement::Init(MYSQL *mysql, string &sql)
{
	mysql_ping(mysql);	// 当mysql连接丢失的时候，使用mysql_ping能够自动重连数据库

	//g_master_conn_fail_num ++;
	m_stmt = mysql_stmt_init(mysql);
	if (!m_stmt)
	{
		log_error("mysql_stmt_init failed\n");
		return false;
	}

	if (mysql_stmt_prepare(m_stmt, sql.c_str(), sql.size()))
	{
		log_error("mysql_stmt_prepare failed: %s\n", mysql_stmt_error(m_stmt));
		return false;
	}

	m_param_cnt = mysql_stmt_param_count(m_stmt);
	if (m_param_cnt > 0)
	{
		m_param_bind = new MYSQL_BIND[m_param_cnt];
		if (!m_param_bind)
		{
			log_error("new failed\n");
			return false;
		}

		memset(m_param_bind, 0, sizeof(MYSQL_BIND) * m_param_cnt);
	}

	return true;
}

void CPrepareStatement::SetParam(uint32_t index, int &value)
{
	if (index >= m_param_cnt)
	{
		log_error("index too large: %d\n", index);
		return;
	}

	m_param_bind[index].buffer_type = MYSQL_TYPE_LONG;
	m_param_bind[index].buffer = &value;
}

void CPrepareStatement::SetParam(uint32_t index, uint32_t &value)
{
	if (index >= m_param_cnt)
	{
		log_error("index too large: %d\n", index);
		return;
	}

	m_param_bind[index].buffer_type = MYSQL_TYPE_LONG;
	m_param_bind[index].buffer = &value;
}

void CPrepareStatement::SetParam(uint32_t index, string &value)
{
	if (index >= m_param_cnt)
	{
		log_error("index too large: %d\n", index);
		return;
	}

	m_param_bind[index].buffer_type = MYSQL_TYPE_STRING;
	m_param_bind[index].buffer = (char *)value.c_str();
	m_param_bind[index].buffer_length = value.size();
}

void CPrepareStatement::SetParam(uint32_t index, const string &value)
{
	if (index >= m_param_cnt)
	{
		log_error("index too large: %d\n", index);
		return;
	}

	m_param_bind[index].buffer_type = MYSQL_TYPE_STRING;
	m_param_bind[index].buffer = (char *)value.c_str();
	m_param_bind[index].buffer_length = value.size();
}

bool CPrepareStatement::ExecuteUpdate()
{
	if (!m_stmt)
	{
		log_error("no m_stmt\n");
		return false;
	}

	if (mysql_stmt_bind_param(m_stmt, m_param_bind))
	{
		log_error("mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(m_stmt));
		return false;
	}

	if (mysql_stmt_execute(m_stmt))
	{
		log_error("mysql_stmt_execute failed: %s\n", mysql_stmt_error(m_stmt));
		return false;
	}

	if (mysql_stmt_affected_rows(m_stmt) == 0)
	{
		log_error("ExecuteUpdate have no effect\n");
		return false;
	}

	return true;
}

uint32_t CPrepareStatement::GetInsertId()
{
	return mysql_stmt_insert_id(m_stmt);
}

/////////////////////
CDBConn::CDBConn(CDBPool *pPool)
{
    m_pDBPool = pPool;
    m_mysql = NULL;
}

CDBConn::~CDBConn()
{
    if (m_mysql)
    {
        mysql_close(m_mysql);
    }
}

int CDBConn::Init()
{
    m_mysql = mysql_init(NULL);
    if (!m_mysql)
    {
        log_error("mysql_init failed\n");
        return 1;
    }

    my_bool reconnect = true;
    mysql_options(m_mysql, MYSQL_OPT_RECONNECT, &reconnect);
    mysql_options(m_mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(m_mintysql, m_pDBPool->GetDBServerIP(), m_pDBPool->GetUsername(), m_pDBPool->GetPassword(),)
                            m_pDBPool->GetDBName, m_pDBPool->GetDBServerPort(), NULL, 0)
    {
        log_error("mysql_real_connect failed: %s\n", mysql_error(m_mysql));
        return 2;
    }

    return 0;
}

const char* CDBConn::GetPoolName()
{
    return m_pDBPool->GetPoolName();
}

bool CDBConn::ExecuteCreate(const char* sql_query)
{
    mysql_ping(m_mysql);
    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)));
    {
        log_error("mysql_real_query failed: %s, sql: start transaction\n", mysql_error(m_mysql))
        return false;
    }

    return true;
}

bool CDBConn::ExecuteDrop(const char* sql_query)
{
    mysql_ping(m_mysql);

    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        log_error("mysql_real_query failed: %s, sql: start transaction\n", mysql_error(m_mysql));
        return false;
    }

    return true;
}

CResultSet* CDBConn::ExecuteQuery(const char* sql_query)
{
    mysql_ping(m_mysql);

    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        log_error("mysql_real_query failed: %s, sql: %s\n", mysql_error(m_mysql), sql_query);
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(m_mysql);
    if (!res)
    {
        log_error("mysql_store_result failed: %s\n", mysql_error(m_mysql))
        return NULL;
    }

    CResultSet *result_set = new CResultSet(res);
    return result_set;
}

bool CDBConn::ExecuteUpdate(const char* sql_query, bool care_affected_rows)
{
    mysql_ping(m_mysql);

    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        log_error("mysql_real_query failed: %s, sql: %s\n", mysql_error(m_mysql), sql_query);
        return false;
    }

    if (mysql_affected_rows(m_mysql) > 0)
    {
        return true;
    }
    else
    {
        if (care_affected_rows)
        {
            log_error("mysql_real_query failed: %s, sql: %s\n\n", mysql_error(m_mysql), sql_query);
            return false;
        }
        else
        {
            log_warn("affected_rows=0, sql: %s\n", sql_query);
            return true;
        }
    }
}

bool CDBConn::StartTransaction()
{
	mysql_ping(m_mysql);

	if (mysql_real_query(m_mysql, "start transaction\n", 17))
	{
		log_error("mysql_real_query failed: %s, sql: start transaction\n", mysql_error(m_mysql));
		return false;
	}

	return true;
}

bool CDBConn::Rollback()
{
	mysql_ping(m_mysql);

	if (mysql_real_query(m_mysql, "rollback\n", 8))
	{
		log_error("mysql_real_query failed: %s, sql: rollback\n", mysql_error(m_mysql));
		return false;
	}

	return true;
}

bool CDBConn::Commit()
{
	mysql_ping(m_mysql);

	if (mysql_real_query(m_mysql, "commit\n", 6))
	{
		log_error("mysql_real_query failed: %s, sql: commit\n", mysql_error(m_mysql));
		return false;
	}

	return true;
}

uint32_t CDBConn::GetInsertId()
{
    return (uint32_t)mysql_insert_id(m_mysql);
}


/////////////////////////////////////////
CDBPool::CDBPool(const char* pool_name, const char* db_server_ip, uint16_t db_server_port,
                const char* username, const char* password, const char* db_name, int max_conn_cnt)
{
    m_pool_name = pool_name;
    m_db_server_ip = db_server_ip;
    m_db_server_port = db_server_port;
    m_username = username;
    m_password = password;
    m_db_name = db_name;
    m_db_max_conn_cnt = max_conn_cnt;
    m_db_cur_conn_cnt = MIN_DB_CONN_CNT;
}

CDBPool::~CDBPool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_abort_request = true;
    m_cond_var.notify_all();

    for (auto it = m_free_list.begin(); it != m_free_list.end(); it++)
    {
        CDBConn *pConn = *it;
        delete pConn;
    }

    m_free_list.clear();
}

int CDBPool::Init()
{
    for (int i = 0; i < m_db_cur_conn_cnt; i++)
    {
        CDBConn *pDBConn = new CDBConn(this);
        int ret = pDBConn->Init();
        if (ret) 
        {
            delete pDBConn;
            return ret;
        }

        m_free_list.push_back(pDBConn);
    }

    return 0;
}

int wait_cout = 0;
CDBConn* CDBPool::GetDBConn(const int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_abort_request)
    {
        log_warn("have abort\n");
        return NULL;
    }

    if (m_free_list.empty())
    {
        if (m_db_cur_conn_cnt >= m_db_cur_conn_cnt)
        {
            if (timeout_ms < 0)
            {
                log_info("wait ms: %d\n", timeout_ms);
                m_cond_var.wait(lock, [this]
                {   
                    return (!m_free_list.empty()) | m_abort_request;
                });
            } else {
                m_cond_var.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]{
                    return (!m_free_list.empty()) | m_abort_request;
                });

                if (m_free_list.empty())
                {
                    return NULL;
                }
            }

            if (m_abort_request)
            {
                log_warn("have abort\n");
                return NULL;
            }
        }
        else
        {
            CDBConn* pDBConn = new CDBConn(this);
            int ret = pDBConn->Init();
            if (ret)
            {
                log_error("Init DBConnection failed\n\n");
                delete pDBConn;
                return NULL;
            }
            else 
            {
                m_free_list.push_back(pDBConn);
                m_db_cur_conn_cnt++;
                log_info("new db connection: %s, conn_cnt: %s\n", m_pool_name.c_str(), m_db_cur_conn_cnt);
            }
        }
    }

    CDBConn *pConn = m_free_list.front();
    m_free_list.pop_front();

    return pConn;
}

void CDBPool::RelDBConn(CDBConn* pConn)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_free_list.begin();
    for (; it != m_free_list.end() it++)
    {
        if (*it == pConn)
        {
            break;
        }
    }

    if (it == m_free_list.end())
    {
        m_free_list.push_back(pConn);
        m_cond_var.notify_one();
    } else {
        log_error("RelDBConn failed\n");
    }

}