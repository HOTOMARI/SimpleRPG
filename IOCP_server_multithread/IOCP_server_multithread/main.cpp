#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <fstream>

#include <thread>
#include <vector>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <chrono>
#include <sqlext.h>
#include <string>

#include "protocol.h"
extern "C" {
#include "include/lua.h"
#include "include/lauxlib.h"
#include "include/lualib.h"
}

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")

using namespace std;

enum EVENT_TYPE { EV_MOVE, EV_HEAL, EV_ATTACK, EV_RESPAWN, EV_BUF };
struct TIMER_EVENT {
	int object_id;
	EVENT_TYPE ev;
	chrono::system_clock::time_point act_time;
	int target_id;

	constexpr bool operator< (const TIMER_EVENT& _Left)const {
		return(act_time > _Left.act_time);
	}
};

struct ASTARNODE {
	int x, y;
	int g, h, f;
	int parent_x, parent_y;

	constexpr bool operator< (const ASTARNODE _Left)const {
		return(f > _Left.f);
	}
};

priority_queue<TIMER_EVENT> timer_queue;
mutex timer_l;

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_PLAYER_MOVE, OP_AUTOHEAL, OP_RESPAWN, OP_BUF};
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	int target_id;
	OVER_EXP()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}
	OVER_EXP(char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]);
	}
};

enum SESSION_STATE { ST_FREE, ST_ACCEPTED, ST_INGAME, ST_DEAD};

class SESSION {
	OVER_EXP _recv_over;

public:
	mutex	_sl;
	SESSION_STATE _s_state;
	int _id;

	int npc_type;
	int attack_target_id;

	char _accountId[11];
	SOCKET _socket;
	int level;
	int hp;
	int hpmax;
	bool regen;
	bool buf_on;
	int exp;
	short	spawn_x, spawn_y;
	short	x, y;
	short last_move_dir;
	int race;
	char	_name[NAME_SIZE];
	int		_prev_remain;
	unordered_set<int> view_list;
	mutex vl;
	lua_State* L;
	mutex vm_l;

	pair<int, int> sectornum;
	pair<int, int> next_move_position;

	chrono::system_clock::time_point next_move_time;
	chrono::system_clock::time_point next_attack_time;
	chrono::system_clock::time_point next_buf_time;
public:
	SESSION()
	{
		_id = -1;
		attack_target_id = -1;
		_socket = 0;
		x = rand() % W_WIDTH;
		y = rand() % W_HEIGHT;
		regen = false;
		_name[0] = 0;
		_s_state = ST_FREE;
		_prev_remain = 0;
		next_move_time = chrono::system_clock::now() + chrono::seconds(1);
		next_attack_time = chrono::system_clock::now() + chrono::seconds(1);
	}

	~SESSION() {}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
		WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
			&_recv_over._over, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
		WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
	}
	void send_login_ok_packet()
	{
		SC_LOGIN_OK_PACKET p;
		p.id = _id;
		p.size = sizeof(SC_LOGIN_OK_PACKET);
		p.type = SC_LOGIN_OK;
		p.x = x;
		p.y = y;
		p.level = level;
		p.hp = hp;
		p.hpmax = hpmax;
		p.exp = exp;
		p.race = race;
		do_send(&p);
	}
	void send_login_fail_packet(int ERR) {
		SC_LOGIN_FAIL_PACKET p;
		p.reason = ERR;
		p.size = sizeof(SC_LOGIN_FAIL_PACKET);
		p.type = SC_LOGIN_FAIL;
		do_send(&p);
	}
	void send_respawn_packet() {
		SC_RESPAWN_PACKET p;
		p.size = sizeof(SC_RESPAWN_PACKET);
		p.type = SC_RESPAWN;
		do_send(&p);
	}
	void send_move_packet(int c_id, int client_time);
	void send_add_object(int c_id);
	void send_remove_object(int c_id);
	void send_chat_packet(const char* name, const wchar_t* mess, int chat_type);
	void send_auth_packet(const char* name, bool result);
	void send_stat_packet(int c_id);
	void send_attack_info_packet(int target, int damage, int atk_type);
	void send_exp_info_packet(int target, int exp);
};

array<SESSION, MAX_USER + NUM_NPC> clients;
HANDLE g_h_iocp;
SOCKET g_s_socket;

mutex g_viewlock;

char WorldSpace[2000][2000];
unordered_set<int> sector[40][40];

void db_show_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

int db_make_account(CS_LOGIN_PACKET* authData) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	int retval = LF_NONE;

	wstring query = L"EXEC make_account ";
	wstring w_accountid(authData->id, &authData->id[strlen(authData->id)]);
	query.append(L"\"");
	query.append(w_accountid);
	query.append(L"\"");

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2017182012_2022_gameserver", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						if (retcode == SQL_ERROR)
							db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
						{

						}
					}
					else {
						db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
	return retval;
}

int db_check_id_exist(CS_LOGIN_PACKET* authData) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLINTEGER szCount;
	SQLLEN cbCount = 0;
	int retval = LF_NONE;

	wstring query = L"EXEC check_account_exist ";
	wstring w_accountid(authData->id, &authData->id[strlen(authData->id)]);
	query.append(L"\"");
	query.append(w_accountid);
	query.append(L"\"");

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2017182012_2022_gameserver", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &szCount, 10, &cbCount);

						// Fetch and print each row of data. On an error, display a message and exit.  
						retcode = SQLFetch(hstmt);

						if (retcode == SQL_ERROR)
							db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
						{
							if (szCount != 0) {
								retval = LF_NONE;
							}
							// id 없을때 처리
							else {
								db_make_account(authData);
								retval = LF_NONE;
							}
						}
					}
					else {
						db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
	return retval;
}

void db_load_user_info(SESSION* cli) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;

	SQLWCHAR szName[NAME_SIZE];
	SQLLEN cbName = 0;
	SQLINTEGER szLevel, szHP, szmaxHP, szEXP, szRace;
	SQLLEN cbLevel = 0, cbHP = 0, cbmaxHP = 0, cbEXP = 0, cbRace = 0;
	SQLINTEGER szPosX, szPosY;
	SQLLEN cbPosX = 0, cbPosY = 0;

	wstring query = L"EXEC load_user_info ";
	wstring w_accountid(cli->_accountId, &cli->_accountId[strlen(cli->_accountId)]);
	query.append(L"\"");
	query.append(w_accountid);
	query.append(L"\"");

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2017182012_2022_gameserver", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns
						retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &szLevel, 10, &cbLevel);
						retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &szHP, 10, &cbHP);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &szEXP, 10, &cbEXP);
						retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &szmaxHP, 10, &cbmaxHP);
						retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &szPosX, 10, &cbPosX);
						retcode = SQLBindCol(hstmt, 6, SQL_INTEGER, &szPosY, 10, &cbPosY);
						retcode = SQLBindCol(hstmt, 7, SQL_C_WCHAR, szName, NAME_SIZE, &cbName);
						retcode = SQLBindCol(hstmt, 8, SQL_INTEGER, &szRace, 10, &cbRace);

						// Fetch and print each row of data. On an error, display a message and exit.  

						retcode = SQLFetch(hstmt);
						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
						{
							cli->level = szLevel;
							cli->hpmax = szmaxHP;

							if (szHP < 0)cli->hp = 50;
							else cli->hp = szHP;

							if (szEXP < 0)cli->exp = 0;
							else cli->exp = szEXP;

							if (szPosX > W_WIDTH)cli->x = W_WIDTH - 1;
							else if (szPosX < 0)cli->x = 0;
							else cli->x = szPosX;

							if (szPosX > W_HEIGHT)cli->y = W_HEIGHT - 1;
							else if (szPosY < 0)cli->y = 0;
							else cli->y = szPosY;

							char* temp = new char[NAME_SIZE];

							size_t convertedSize;
							wcstombs_s(&convertedSize, temp, NAME_SIZE, szName, NAME_SIZE);
							strcpy_s(cli->_name, temp);

							cli->race = szRace;

							cli->send_login_ok_packet();
						}
						else {
							cout << "LOAD INFO ERR!\n";
						}
					}
					else {
						db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}

void db_save_user_data(SESSION* cli){
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLINTEGER szPosX, szPosY;
	SQLLEN cbPosX = 0, cbPosY = 0;
	wstring query = L"EXEC save_user_data ";
	wstring w_accountid(cli->_accountId, &cli->_accountId[strlen(cli->_accountId)]);
	query.append(L"\"");
	query.append(w_accountid);
	query.append(L"\"");
	query.append(L",");
	query.append(to_wstring(cli->x));
	query.append(L",");
	query.append(to_wstring(cli->y));
	query.append(L",");
	query.append(to_wstring(cli->level));
	query.append(L",");
	query.append(to_wstring(cli->exp));
	query.append(L",");
	query.append(to_wstring(cli->hp));
	query.append(L",");
	query.append(to_wstring(cli->hpmax));
	query.append(L",");
	query.append(to_wstring(cli->race));

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2017182012_2022_gameserver", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						wcout << "user " << w_accountid << " SAVE DATA OK!\n";
					}
					else {
						cout << "SAVE DATA ERR!\n";
						db_show_error(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}

void add_timer(int obj_id, int act_time, EVENT_TYPE e_type, int target_id) {
	using namespace chrono;
	lock_guard<mutex> tl{ timer_l };
	TIMER_EVENT ev;
	ev.act_time = system_clock::now() + milliseconds(act_time);
	ev.object_id = obj_id;
	ev.ev = e_type;
	ev.target_id=target_id;
	timer_queue.push(ev);
}

bool playerdistance(int cli_a, int cli_b) {
	return (abs(clients[cli_a].x - clients[cli_b].x) <= PSIGHT && abs(clients[cli_a].y - clients[cli_b].y) <= PSIGHT);
}

bool distance(int cli_a, int cli_b) {
	return (abs(clients[cli_a].x - clients[cli_b].x) <= SIGHT && abs(clients[cli_a].y - clients[cli_b].y) <= SIGHT);
}

int calc_maxExp(int level) {
	int retval = 100;
	for (int i = 1; i < level; ++i) {
		retval = retval * 2;
	}
	return retval;
}

int calc_maxHp(int level) {
	int retval = 50;
	for (int i = 1; i < level; ++i) {
		retval = retval + i * 5;
	}
	return retval;
}

bool attack_ok(int a, int t) {
	if (clients[t].x == clients[a].x - 1 && clients[t].y == clients[a].y) return true;
	if (clients[t].x == clients[a].x + 1 && clients[t].y == clients[a].y) return true;
	if (clients[t].y == clients[a].y - 1 && clients[t].x == clients[a].x) return true;
	if (clients[t].y == clients[a].y + 1 && clients[t].x == clients[a].x) return true;
	return false;
}

bool attack_ok(int x1, int y1, int x2, int y2) {
	if (x1 == x2 - 1 && y1 == y2)return true;
	if (x1 == x2 + 1 && y1 == y2)return true;
	if (x1 == x2 && y1 == y2 - 1)return true;
	if (x1 == x2 && y1 == y2 + 1)return true;
	return false;
}

bool dirattack_ok(int a, int t, char dir) {
	switch (dir) {
	case MV_UP:
		if (clients[t].y == clients[a].y - 1 && clients[t].x == clients[a].x) return true;
		break;
	case MV_DOWN:
		if (clients[t].y == clients[a].y + 1 && clients[t].x == clients[a].x) return true;
		break;
	case MV_LEFT:
		if (clients[t].x == clients[a].x - 1 && clients[t].y == clients[a].y) return true;
		break;
	case MV_RIGHT:
		if (clients[t].x == clients[a].x + 1 && clients[t].y == clients[a].y) return true;
		break;
	}
	return false;
}

void SESSION::send_move_packet(int c_id, int client_time)
{
	SC_MOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = clients[c_id].x;
	p.y = clients[c_id].y;
	p.client_time = client_time;
	do_send(&p);
}

void SESSION::send_add_object(int c_id)
{
	SC_ADD_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_ADD_OBJECT_PACKET);
	p.type = SC_ADD_OBJECT;
	p.x = clients[c_id].x;
	p.y = clients[c_id].y;
	p.race = clients[c_id].race;
	p.npc_type = clients[c_id].npc_type;
	strcpy_s(p.name, clients[c_id]._name);
	do_send(&p);
}

void SESSION::send_remove_object(int c_id)
{
	SC_REMOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_REMOVE_OBJECT_PACKET);
	p.type = SC_REMOVE_OBJECT;

	do_send(&p);
}

void SESSION::send_chat_packet(const char* name, const wchar_t* mess, int chat_type) {
	//cout << "npc" << npc_id << ": " << mess << endl;
	SC_CHAT_PACKET p;
	p.size = sizeof(SC_REMOVE_OBJECT_PACKET) - sizeof(p.mess) + wcslen(mess)*2 + 1;
	strcpy_s(p.name, name);
	p.type = SC_CHAT;
	p.chat_type = chat_type;
	wcscpy_s(p.mess, mess);
	do_send(&p);
}

void SESSION::send_auth_packet(const char* name, bool result) {
	SC_AUTH_ACCOUNT_PACKET p;
	p.size = sizeof(SC_AUTH_ACCOUNT_PACKET);
	p.result = result;
	p.type = SC_AUTH;
	strcpy_s(p.name, name);
	do_send(&p);
}

void SESSION::send_stat_packet(int c_id) {
	SC_STAT_CHANGE_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_STAT_CHANGE_PACKET);
	p.type = SC_STAT_CHANGE;
	p.hp = clients[c_id].hp;
	p.hpmax = clients[c_id].hpmax;
	p.exp = clients[c_id].exp;
	p.level = clients[c_id].level;
	p.buf = clients[c_id].buf_on;
	p.id = c_id;
	do_send(&p);
}

void SESSION::send_attack_info_packet(int target, int damage, int atk_type) {
	SC_ATTACK_INFO_PACKET p;
	p.id = target;
	p.size = sizeof(SC_ATTACK_INFO_PACKET);
	p.type = SC_ATTACK_INFO;
	p.atk_type = atk_type;
	p.damage = damage;
	do_send(&p);
}

void SESSION::send_exp_info_packet(int target, int exp)
{
	SC_EXP_INFO_PACKET p;
	p.id = target;
	p.size = sizeof(SC_EXP_INFO_PACKET);
	p.type = SC_EXP_INFO;
	p.exp = exp;
	do_send(&p);
}

int GoalDistEstimate(int sx, int sy, int ex, int ey) {
	return (abs(sx - ex) + abs(sy - ey));
}

bool IsinOpen(ASTARNODE t, priority_queue<ASTARNODE> Open) {
	priority_queue<ASTARNODE> tmp;
	bool o_empty = Open.empty();

	while (!o_empty) {
		ASTARNODE n = Open.top(); Open.pop();
		tmp.push(n);
		if (n.x == t.x && n.y == t.y) {

			return true;
		}
		o_empty = Open.empty();
	}

	return false;
}

bool IsinClosed(ASTARNODE t, list<ASTARNODE> Closed) {
	for (auto c : Closed) {
		if (c.x == t.x && c.y == t.y) return true;
	}
	return false;
}

bool AstarSearch(int sx, int sy, int ex, int ey, int npc_id) {
	priority_queue<ASTARNODE> Open;
	list<ASTARNODE> Closed;

	ASTARNODE s;
	s.x = sx, s.y = sy;
	s.g = 0;
	s.h = GoalDistEstimate(sx, sy, ex, ey);
	s.f = s.g + s.h;
	s.parent_x = 0;
	s.parent_y = 0;
	Open.push(s);

	bool o_empty = Open.empty();
	while (!o_empty) {
		ASTARNODE n = Open.top(); Open.pop();
		if (attack_ok(n.x, n.y, ex, ey)) {
			//construct path
			Closed.push_back(n);
			pair<int, int>next;
			next.first = n.x, next.second = n.y;
			while (true) {
				for (auto c : Closed) {
					if (next.first == c.x && next.second == c.y) {
						if (c.parent_x == sx && c.parent_y == sy) {
							clients[npc_id].next_move_position.first = next.first;
							clients[npc_id].next_move_position.second = next.second;
							return true;
						}
						else if(next.first = c.parent_x, next.second = c.parent_y)	break;
					}
				}
			}
		}

		for (int i = 0; i < 4; ++i) {
			ASTARNODE np;
			switch (i) {
			case MV_UP:
				np.x = n.x;
				np.y = n.y - 1;
				break;
			case MV_DOWN:
				np.x = n.x;
				np.y = n.y + 1;
				break;
			case MV_LEFT:
				np.x = n.x - 1;
				np.y = n.y;
				break;
			case MV_RIGHT:
				np.x = n.x + 1;
				np.y = n.y;
				break;
			}
			if (np.x<0 || np.x > W_WIDTH || np.y<0 || np.y > W_HEIGHT || WorldSpace[np.y][np.x] == 1) {
				continue;
			}

			np.g = GoalDistEstimate(np.x, np.y, sx, sy);
			int newg = n.g + 1;
			if ((IsinOpen(np, Open) || IsinClosed(np,Closed)) && newg >= np.g)
				continue;
			np.g = newg;
			np.h = GoalDistEstimate(np.x, np.y, ex, ey);
			np.f = np.g + np.h;
			np.parent_x = n.x;
			np.parent_y = n.y;
			if (IsinClosed(np,Closed)) {
				Closed.remove_if([np](ASTARNODE as)
					{ return(np.x == as.x && np.y == as.y); });
			}
			if (!IsinOpen(np, Open)) {
				Open.push(np);
			}
		}
		Closed.push_back(n);
		o_empty = Open.empty();
	}
	return false;
}

void disconnect(int c_id)
{
	clients[c_id]._sl.lock();
	if (clients[c_id]._s_state == ST_FREE) {
		clients[c_id]._sl.unlock();
		return;
	}
	else if (clients[c_id]._s_state == ST_ACCEPTED) {
		clients[c_id]._s_state = ST_FREE;
		clients[c_id]._sl.unlock();
		return;
	}
	closesocket(clients[c_id]._socket);
	clients[c_id]._s_state = ST_FREE;
	db_save_user_data(&clients[c_id]);
	clients[c_id]._sl.unlock();

	for (auto& pl : clients) {
		if (pl._id == c_id) continue;
		pl._sl.lock();
		if (pl._s_state != ST_INGAME) {
			pl._sl.unlock();
			continue;
		}
		SC_REMOVE_OBJECT_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_OBJECT;
		pl.do_send(&p);
		pl._sl.unlock();
	}
}

int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i) {
		clients[i]._sl.lock();
		if (clients[i]._s_state == ST_FREE) {
			clients[i]._s_state = ST_ACCEPTED;
			clients[i]._sl.unlock();
			return i;
		}
		clients[i]._sl.unlock();
	}
	return -1;
}

void process_packet(int c_id, char* packet)
{
	switch (packet[1])
	{
		case CS_LOGIN: {
			CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
			//중복접속체크
			for (int i = 0; i < MAX_USER; ++i) {
				if (c_id == i) continue;
				if (clients[i]._s_state != ST_FREE && strcmp(clients[i]._accountId, p->id)==0) {
					clients[c_id].send_login_fail_packet(LF_ALREADY_LOGIN);
					clients[i].send_login_fail_packet(LF_ALREADY_LOGIN);
					return;
				}
			}
			//이름 문자 체크
			for (int i = 0; i < ID_SIZE; ++i) {
				if (p->id[i] == 0) break;
				if ((p->id[i] < 'a' || p->id[i] > 'z') && (p->id[i] < 'A' || p->id[i] > 'Z') && (p->id[i] < '0' || p->id[i] > '9')) {
					clients[c_id].send_login_fail_packet(LF_INVALID_NAME);
					return;
				}
			}
			if (db_check_id_exist(p)) {
				strcpy_s(clients[c_id]._accountId, sizeof(clients[c_id]._accountId), p->id);

				clients[c_id]._sl.lock();
				if (clients[c_id]._s_state == ST_FREE) {
					clients[c_id]._sl.unlock();
					break;
				}
				if (clients[c_id]._s_state == ST_INGAME || clients[c_id]._s_state == ST_DEAD) {
					clients[c_id]._sl.unlock();
					disconnect(c_id);
					break;
				}

				strcpy_s(clients[c_id]._name, p->id);
				clients[c_id]._s_state = ST_INGAME;
				db_load_user_info(&clients[c_id]);
				clients[c_id].send_login_ok_packet();
				clients[c_id]._sl.unlock();

				if (clients[c_id].hp < clients[c_id].hpmax) {
					clients[c_id].regen = true;
					add_timer(c_id, 1000, EV_HEAL, c_id);
				}

				int sy = clients[c_id].sectornum.first = clients[c_id].y / 50;
				int sx = clients[c_id].sectornum.second = clients[c_id].x / 50;
				sector[sy][sx].insert(c_id);

				unordered_set<int> pl_view_list;
				for (auto i : sector[sy][sx]) {
					if (clients[i]._id == c_id) continue;
					clients[i]._sl.lock();
					if (ST_INGAME != clients[i]._s_state) {
						clients[i]._sl.unlock();
						continue;
					}
					if (playerdistance(c_id, i)) {
						pl_view_list.insert(c_id);
						clients[i].send_add_object(c_id);
					}
					clients[i].vl.lock();
					for (auto p : pl_view_list) {
						clients[i].view_list.insert(p);
					}
					clients[i].vl.unlock();
					clients[i]._sl.unlock();
				}
				/*
				for (int i = 0; i < MAX_USER; ++i) {
					if (clients[i]._id == c_id) continue;
					clients[i]._sl.lock();
					if (ST_INGAME != clients[i]._s_state) {
						clients[i]._sl.unlock();
						continue;
					}
					if (distance(c_id, clients[i]._id)) {
						pl_view_list.insert(c_id);
						clients[i].send_add_object(c_id);
					}
					clients[i].vl.lock();
					for (auto p : pl_view_list) {
						clients[i].view_list.insert(p);
					}
					clients[i].vl.unlock();
					clients[i]._sl.unlock();
				}
				*/
				unordered_set<int> my_view_list;
				for (auto i : sector[sy][sx]) {
					if (i == c_id) continue;
					lock_guard<mutex> aa{ clients[i]._sl };
					if (ST_INGAME != clients[i]._s_state) continue;
					if (playerdistance(c_id, i)) {
						my_view_list.insert(i);
						clients[c_id].send_add_object(i);
					}
				}
				/*
				for (int i = 0; i < MAX_USER + NUM_NPC; ++i) {
					if (i == c_id) continue;
					lock_guard<mutex> aa{ clients[i]._sl};
					if (ST_INGAME != clients[i]._s_state) continue;
					if (distance(c_id, i)) {
						my_view_list.insert(i);
						clients[c_id].send_add_object(i);
					}
				}
				*/
				clients[c_id].vl.lock();
				for (auto m : my_view_list) {
					clients[c_id].view_list.insert(m);
				}
				clients[c_id].vl.unlock();
				for (int i = 0; i < NUM_NPC; ++i) {
					int npc_id = i + MAX_USER;
					lock_guard<mutex> aa{ clients[npc_id]._sl };
					if (ST_INGAME != clients[npc_id]._s_state) continue;
					if (clients[npc_id].npc_type == NPC_AGRO || clients[npc_id].npc_type == NPC_AGRO_ROAM) {
						if (distance(npc_id, i) && clients[npc_id].attack_target_id < 0) {
							clients[npc_id].attack_target_id = c_id;
						}
					}
				}
			}
		break;
		}
		case CS_MOVE: {
			//죽어있으면 안받음
			if (clients[c_id]._s_state != ST_INGAME) break;
			// 시간 체크하고 1초 안지났으면 break;
			auto start_t = chrono::system_clock::now();			
			if (start_t < clients[c_id].next_move_time) break;

			CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
			short x = clients[c_id].x;
			short y = clients[c_id].y;
			switch (p->direction) {
			case MV_UP: if (y > 0 && WorldSpace[y - 1][x] == 0) y--; break;
			case MV_DOWN: if (y < W_HEIGHT - 1 && WorldSpace[y + 1][x] == 0) y++; break;
			case MV_LEFT: if (x > 0 && WorldSpace[y][x - 1] == 0) x--; break;
			case MV_RIGHT: if (x < W_WIDTH - 1 && WorldSpace[y][x + 1] == 0) x++; break;
			default: break;
			}
			clients[c_id].last_move_dir = p->direction;
			clients[c_id].x = x;
			clients[c_id].y = y;

			int sy = clients[c_id].y / 50;
			int sx = clients[c_id].x / 50;
			if (sy != clients[c_id].sectornum.first || sx != clients[c_id].sectornum.second) {
				sector[clients[c_id].sectornum.first][clients[c_id].sectornum.second].erase(c_id);
				sector[sy][sx].insert(c_id);
			}

			unordered_set<int> near_list;
			unordered_set<int> view_list;

			clients[c_id].vl.lock();
			for (int vl : clients[c_id].view_list) {
				view_list.insert(vl);
			}
			clients[c_id].vl.unlock();

			for (auto i : sector[sy][sx]) {
				if (i == c_id) continue;
				lock_guard<mutex> aa{ clients[i]._sl};
				if (ST_INGAME != clients[i]._s_state) {
					continue;
				}
				if (playerdistance(c_id, i)) {
					near_list.insert(i);
				}
			}
			/*
			for (int i = 0; i < MAX_USER;++i) {
				auto& pl = clients[i];
				if (pl._id == c_id) continue;
				lock_guard<mutex> aa{ pl._sl };
				if (ST_INGAME != pl._s_state) {
					continue;
				}
				if (distance(c_id, pl._id)) {
					near_list.insert(pl._id);
				}
			}
			for (int i = 0; i < NUM_NPC; ++i) {
				int npc_id = i + MAX_USER;
				if (clients[npc_id]._s_state != ST_INGAME)continue;
				if (distance(c_id, npc_id)) {
					near_list.insert(npc_id);
				}
			}
			*/
			for (int nl : near_list) {
				if (view_list.find(nl) == view_list.end()) {
					// SC_ADD
					{
						view_list.insert(nl);
						lock_guard<mutex> aa{ clients[c_id]._sl };
						if (ST_INGAME == clients[c_id]._s_state) {
							clients[c_id].send_add_object(nl);
						}
					}
					clients[nl].vl.lock();
					if (clients[nl].view_list.find(c_id) != clients[nl].view_list.end()) {
						// SC_MOVE
						lock_guard<mutex> aa{ clients[nl]._sl };
						if (ST_INGAME == clients[nl]._s_state)
							clients[nl].send_move_packet(c_id, p->client_time);
					}
					else {
						clients[nl].view_list.insert(c_id);
						// SC_ADD
						lock_guard<mutex> aa{ clients[nl]._sl };
						if (ST_INGAME == clients[nl]._s_state) {
							clients[nl].send_add_object(c_id);
						}
					}
					clients[nl].vl.unlock();
				}
				else {
					clients[nl].vl.lock();
					if (clients[nl].view_list.find(c_id) != clients[nl].view_list.end()) {
						// SC_MOVE
						lock_guard<mutex> aa{ clients[nl]._sl };
						if (ST_INGAME == clients[nl]._s_state)
							clients[nl].send_move_packet(c_id, p->client_time);
					}
					else {
						clients[nl].view_list.insert(c_id);
						// SC_ADD
						lock_guard<mutex> aa{ clients[nl]._sl };
						if (ST_INGAME == clients[nl]._s_state) {
							clients[nl].send_add_object(c_id);
						}
					}
					clients[nl].vl.unlock();
				}
				
			}
			
			unordered_set<int> vl_removelist;
			for (int vl : view_list) {
				if (near_list.find(vl) == near_list.end()) {
					vl_removelist.insert(vl);
					// SC_REMOVE
					{
						lock_guard<mutex> aa{ clients[c_id]._sl };
						if (ST_INGAME == clients[c_id]._s_state) {
							clients[c_id].send_remove_object(vl);
						}
					}
					clients[vl].vl.lock();
					if (clients[vl].view_list.find(c_id) != clients[vl].view_list.end()) {
						clients[vl].view_list.erase(c_id);
						//SC_REMOVE
						lock_guard<mutex> aa{ clients[vl]._sl };
						if (ST_INGAME == clients[vl]._s_state) {
							clients[vl].send_remove_object(c_id);
						}
					}
					clients[vl].vl.unlock();
				}
				if (view_list.size() == 0) break;
			}
			for (int t : vl_removelist) {
				view_list.erase(t);
			}

			//viewlist 복사
			clients[c_id].vl.lock();
			clients[c_id].view_list.clear();
			for (auto v : view_list) {
				clients[c_id].view_list.insert(v);
			}
			clients[c_id].vl.unlock();
			
			// 자신 보내주기
			clients[c_id]._sl.lock();
			if (ST_INGAME == clients[c_id]._s_state)
				clients[c_id].send_move_packet(c_id, p->client_time);
			clients[c_id]._sl.unlock();

			//npc에게 전송
			for (int i = 0; i < NUM_NPC; ++i) {
				int npc_id = MAX_USER + i;
				lock_guard<mutex> nl{ clients[i]._sl };
				if (clients[npc_id]._s_state != ST_INGAME)continue;
				if (clients[npc_id].npc_type == NPC_AGRO || clients[npc_id].npc_type == NPC_AGRO_ROAM) {
					if (distance(npc_id, c_id) && clients[npc_id].attack_target_id < 0) {
						clients[npc_id].attack_target_id = c_id;
					}
				}
			}

			// 이동 쿨타임 갱신
			clients[c_id].next_move_time = start_t + chrono::seconds(1);
		break;
	}
		case CS_CHAT: {
			CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);
			switch (p->chat_type) {
			case CT_NORMAL:
				clients[c_id].send_chat_packet(clients[c_id]._name, p->mess, p->chat_type);
				for (auto t : clients[c_id].view_list) {
					clients[t].send_chat_packet(clients[c_id]._name, p->mess, p->chat_type);
				}
				break;
			case CT_TELL:
				clients[p->target_id].send_chat_packet(clients[c_id]._name, p->mess, p->chat_type);
				clients[c_id].send_chat_packet(clients[c_id]._name, p->mess, p->chat_type);
				break;
			case CT_SHOUT:
				for (int i = 0; i < MAX_USER;++i) {
					if (clients[i]._s_state == ST_INGAME) {
						clients[i].send_chat_packet(clients[c_id]._name, p->mess, p->chat_type);
					}
				}
				break;
			}
			break;
		}
		case CS_ATTACK: {
			CS_ATTACK_PACKET* p = reinterpret_cast<CS_ATTACK_PACKET*>(packet);

			if (p->atk_type == ATK_Buf) {
				auto start_t = chrono::system_clock::now();
				if (start_t < clients[c_id].next_buf_time) break;

				clients[c_id].buf_on = true;
				clients[c_id].send_stat_packet(c_id);
				add_timer(c_id, 5000, EV_BUF, c_id);
				clients[c_id].next_buf_time = chrono::system_clock::now() + chrono::seconds(15);
				break;
			}


			auto start_t = chrono::system_clock::now();
			if (start_t < clients[c_id].next_attack_time) break;

			vector<int> rmlist;

			lock_guard<mutex> vl{ g_viewlock };
			for (int vl : clients[c_id].view_list) {
				if (vl < MAX_USER)continue;
				lock_guard<mutex> sl{ clients[vl]._sl };
				volatile int damage = 5;
				volatile bool atk_ok = false;

				switch (p->atk_type)
				{
				case ATK_Normal:
					atk_ok = attack_ok(c_id, vl);
					damage = 5;
					break;
				case ATK_Directional:
					atk_ok = dirattack_ok(c_id, vl, clients[c_id].last_move_dir);
					damage = 10;
					break;				
				}
				if (clients[c_id].buf_on) damage *= 2;

				if (atk_ok)
				{
					if (clients[vl]._s_state != ST_INGAME)continue;
					if (clients[vl].attack_target_id < 0) clients[vl].attack_target_id = c_id;
					clients[vl].hp -= damage;
					cout << clients[vl].hp << endl;
					clients[c_id].send_attack_info_packet(vl, damage, ATK_PtoN);
					// npc 죽었을때 처리
					if (clients[vl].hp <= 0) {
						cout << clients[vl]._name << " DEAD" << endl;
						int exp = clients[vl].level * clients[vl].level * 2;
						if (clients[vl].npc_type == NPC_AGRO || clients[vl].npc_type == NPC_AGRO_ROAM) exp *= 2;
						if (clients[vl].npc_type == NPC_PEACE_ROAM || clients[vl].npc_type == NPC_AGRO_ROAM) exp *= 2;
						clients[c_id].exp += exp;
						clients[c_id].send_exp_info_packet(vl, exp);
						// 레벨업 처리
						{
							while (clients[c_id].exp >= calc_maxExp(clients[c_id].level)) {
								clients[c_id].exp -= calc_maxExp(clients[c_id].level);
								clients[c_id].level += 1;
								clients[c_id].hpmax = calc_maxHp(clients[c_id].level);
								clients[c_id].hp = clients[c_id].hpmax;
								clients[c_id].send_chat_packet("SYSTEM", L"레벨업 하였습니다.", CT_SYSTEM);
							}
						}
						clients[c_id].send_stat_packet(c_id);
						clients[c_id].send_remove_object(vl);
						clients[vl]._s_state = ST_DEAD;
						clients[vl].attack_target_id = -1;
						//리스폰타이머
						add_timer(vl, 30000, EV_RESPAWN, vl);
					}
				}
			}
			clients[c_id].next_attack_time = chrono::system_clock::now() + chrono::seconds(1);
			break;
		}
		default: {
			cout << "Unknown Type" << packet[1] << endl;
			break;
		}
	}
	//cout << "PROCESS"<< endl;
}

void do_worker()
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;
		BOOL ret = GetQueuedCompletionStatus(g_h_iocp, &num_bytes, &key, &over, INFINITE);
		OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
		if (FALSE == ret) {
			if (ex_over->_comp_type == OP_ACCEPT) cout << "Accept Error";
			else {
				cout << "GQCS Error on client[" << key << "]\n";
				disconnect(static_cast<int>(key));
				if (ex_over->_comp_type == OP_SEND) delete ex_over;
				continue;
			}
		}

		switch (ex_over->_comp_type) {
		case OP_ACCEPT: {
			SOCKET c_socket = reinterpret_cast<SOCKET>(ex_over->_wsabuf.buf);
			for (int i = 0; i < MAX_USER; ++i) {
				if (clients[i]._s_state != ST_INGAME) break;
				// 서버 꽉찼을때 처리
				if (i == MAX_USER - 1) {
					SESSION tmp;
					tmp._socket = c_socket;
					tmp.send_login_fail_packet(LF_SERVER_FULL);
				}
			}

			int client_id = get_new_client_id();
			if (client_id != -1) {
				clients[client_id].x = 0;
				clients[client_id].y = 0;
				clients[client_id]._id = client_id;
				clients[client_id]._name[0] = 0;
				clients[client_id]._prev_remain = 0;
				clients[client_id]._socket = c_socket;
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket),
					g_h_iocp, client_id, 0);
				clients[client_id].do_recv();
				c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			}
			else {
				cout << "Max user exceeded.\n";
			}
			ZeroMemory(&ex_over->_over, sizeof(ex_over->_over));
			ex_over->_wsabuf.buf = reinterpret_cast<CHAR*>(c_socket);
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(g_s_socket, c_socket, ex_over->_send_buf, 0, addr_size + 16, addr_size + 16, 0, &ex_over->_over);
			break;
		}
		case OP_RECV: {
			if (1 > num_bytes) disconnect(key);
			int remain_data = num_bytes + clients[key]._prev_remain;
			char* p = ex_over->_send_buf;
			while (remain_data > 0) {
				int packet_size = static_cast<unsigned char>(p[0]);
				if (packet_size <= remain_data) {
					process_packet(static_cast<int>(key), p);
					p += packet_size;
					remain_data -= packet_size;
				}
				else break;
			}
			if (remain_data > 0) {
				for (int i = 0; i < remain_data; ++i) {
					ex_over->_send_buf[i] = p[i+1];
				}
			}
			clients[key]._prev_remain = remain_data;
			clients[key].do_recv();
			break;
		}
		case OP_SEND:
			if (0 == num_bytes) disconnect(key);
			delete ex_over;
			break;
		case OP_PLAYER_MOVE: {
			auto L = clients[key].L;
			clients[key].vm_l.lock();
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, ex_over->target_id);
			lua_pcall(L, 1, 0, 0);
			clients[key].vm_l.unlock();
			delete ex_over;
			break;
		}
		case OP_AUTOHEAL: {
			int t_id = ex_over->target_id;
			clients[t_id]._sl.lock();
			if (clients[t_id]._s_state == ST_INGAME) {
				int maxhp = clients[t_id].hpmax;
				clients[t_id].hp += maxhp * 0.1;
				if (clients[t_id].hp > maxhp) {
					clients[t_id].hp = maxhp;
					clients[t_id].regen = false;
				}
				clients[t_id].send_stat_packet(key);
				if (clients[t_id].hp < maxhp)
					add_timer(t_id, 1000, EV_HEAL, t_id);
			}
			clients[t_id]._sl.unlock();
			delete ex_over;
			break;
		}
		case OP_RESPAWN: {
			int t_id = ex_over->target_id;
			cout << clients[t_id]._name << " RESPAWN" << endl;
			clients[t_id]._sl.lock();
			//player respawn
			if (t_id < MAX_USER) {
				if (clients[t_id]._s_state == ST_DEAD) {
					clients[t_id]._s_state = ST_INGAME;
					int maxhp = clients[t_id].hpmax;
					clients[t_id].hp = maxhp;
					clients[t_id].x = 0;
					clients[t_id].y = 0;
					clients[t_id].regen = false;


					sector[clients[t_id].sectornum.first][clients[t_id].sectornum.second].erase(t_id);
					sector[0][0].insert(t_id);
					

					clients[t_id].vl.lock();
					clients[t_id].view_list.clear();
					for (auto i : sector[0][0]) {
						if (i == t_id)continue;
						if (playerdistance(t_id, i)) {
							clients[t_id].view_list.insert(i);
							if (clients[i].view_list.find(t_id) != clients[i].view_list.end())
								clients[i].view_list.insert(t_id);
						}
					}
					/*
					for (int i = 0; i < MAX_USER; ++i) {
						if (i == t_id)continue;
						if (distance(t_id, i)) {
							clients[t_id].view_list.insert(i);
							if (clients[i].view_list.find(t_id) != clients[i].view_list.end())
								clients[i].view_list.insert(t_id);
						}
					}
					*/
					clients[t_id].vl.unlock();
					for (auto t : clients[t_id].view_list) {
						clients[t_id].send_add_object(t);
						clients[t].send_add_object(t_id);
					}

					if (t_id < MAX_USER)
						clients[t_id].send_stat_packet(t_id);
					clients[t_id].send_respawn_packet();
				}
			}
			//npc respawn
			else {
				clients[t_id]._s_state = ST_INGAME;
				int maxhp = clients[t_id].hpmax;
				clients[t_id].hp = maxhp;
				clients[t_id].x = clients[t_id].spawn_x;
				clients[t_id].y = clients[t_id].spawn_y;

				sector[clients[t_id].sectornum.first][clients[t_id].sectornum.second].erase(t_id);
				sector[0][0].insert(t_id);

				for (int i = 0; i < MAX_USER; ++i) {
					lock_guard<mutex> vl{ clients[i]._sl };
					if (ST_INGAME != clients[i]._s_state) continue;
					if (distance(t_id, i)) {
						clients[i].send_add_object(t_id);
						if (clients[t_id].attack_target_id < 0)
							clients[t_id].attack_target_id = i;
					}
				}
			}
			clients[t_id]._sl.unlock();
			delete ex_over;
			break;
		}
		case OP_BUF: {
			int t_id = ex_over->target_id;
			clients[t_id].buf_on = false;
			delete ex_over;
			break;
		}
		}
	}
}


void move_npc(int npc_id) {
	short x = clients[npc_id].x;
	short y = clients[npc_id].y;
	unordered_set<int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) old_vl.insert(i);
	}

	switch (rand() % 4) {
	case MV_UP: if (y > 0 && WorldSpace[y - 1][x] == 0) y--; break;
	case MV_DOWN: if (y < W_HEIGHT - 1 && WorldSpace[y + 1][x] == 0) y++; break;
	case MV_LEFT: if (x > 0 && WorldSpace[y][x - 1] == 0) x--; break;
	case MV_RIGHT: if (x < W_WIDTH - 1 && WorldSpace[y][x + 1] == 0) x++; break;
	}

	clients[npc_id].x = x;
	clients[npc_id].y = y;

	int sy = clients[npc_id].y / 50;
	int sx = clients[npc_id].x / 50;
	if (sy != clients[npc_id].sectornum.first || sx != clients[npc_id].sectornum.second) {
		sector[clients[npc_id].sectornum.first][clients[npc_id].sectornum.second].erase(npc_id);
		sector[sy][sx].insert(npc_id);
	}


	unordered_set<int> new_vl;
	for (auto i : sector[sy][sx]) {
		if (i > MAX_USER) continue;
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) new_vl.insert(i);
	}
	/*
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) new_vl.insert(i);
	}
	*/
	for (auto p_id : new_vl) {
		clients[p_id].vl.lock();
		if (0 == clients[p_id].view_list.count(npc_id)) {
			clients[p_id].view_list.insert(npc_id);
			clients[p_id].vl.unlock();
			clients[p_id].send_add_object(npc_id);
		}
		else {
			clients[p_id].vl.unlock();
			clients[p_id].send_move_packet(npc_id, 0);
		}
	}
	for (auto p_id : old_vl) {
		if (0 == new_vl.count(p_id)) {
			clients[p_id].vl.lock();
			if (clients[p_id].view_list.count(npc_id) == 1) {
				clients[p_id].view_list.erase(p_id);
				clients[p_id].vl.unlock();
				clients[p_id].send_remove_object(npc_id);
			}
			else {
				clients[p_id].vl.unlock();
			}
		}
	}
}
void move_npc(int npc_id, int dir) {
	short x = clients[npc_id].x;
	short y = clients[npc_id].y;
	unordered_set<int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) old_vl.insert(i);
	}

	switch (dir) {
	case MV_UP: if (y > 0 && WorldSpace[y - 1][x] == 0) y--; break;
	case MV_DOWN: if (y < W_HEIGHT - 1 && WorldSpace[y + 1][x] == 0) y++; break;
	case MV_LEFT: if (x > 0 && WorldSpace[y][x - 1] == 0) x--; break;
	case MV_RIGHT: if (x < W_WIDTH - 1 && WorldSpace[y][x + 1] == 0) x++; break;
	}

	clients[npc_id].x = x;
	clients[npc_id].y = y;

	int sy = clients[npc_id].y / 50;
	int sx = clients[npc_id].x / 50;
	if (sy != clients[npc_id].sectornum.first || sx != clients[npc_id].sectornum.second) {
		sector[clients[npc_id].sectornum.first][clients[npc_id].sectornum.second].erase(npc_id);
		sector[sy][sx].insert(npc_id);
	}


	unordered_set<int> new_vl;
	for (auto i : sector[sy][sx]) {
		if (i > MAX_USER) continue;
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) new_vl.insert(i);
	}
	/*
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) new_vl.insert(i);
	}
	*/
	for (auto p_id : new_vl) {
		clients[p_id].vl.lock();
		if (0 == clients[p_id].view_list.count(npc_id)) {
			clients[p_id].view_list.insert(npc_id);
			clients[p_id].vl.unlock();
			clients[p_id].send_add_object(npc_id);
		}
		else {
			clients[p_id].vl.unlock();
			clients[p_id].send_move_packet(npc_id, 0);
		}
	}
	for (auto p_id : old_vl) {
		if (0 == new_vl.count(p_id)) {
			clients[p_id].vl.lock();
			if (clients[p_id].view_list.count(npc_id) == 1) {
				clients[p_id].view_list.erase(p_id);
				clients[p_id].vl.unlock();
				clients[p_id].send_remove_object(npc_id);
			}
			else {
				clients[p_id].vl.unlock();
			}
		}
	}
}
void move_npc(int npc_id, int n_x, int n_y) {
	unordered_set<int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) old_vl.insert(i);
	}

	clients[npc_id].x = n_x;
	clients[npc_id].y = n_y;

	int sy = clients[npc_id].y / 50;
	int sx = clients[npc_id].x / 50;
	if (sy != clients[npc_id].sectornum.first || sx != clients[npc_id].sectornum.second) {
		sector[clients[npc_id].sectornum.first][clients[npc_id].sectornum.second].erase(npc_id);
		sector[sy][sx].insert(npc_id);
	}


	unordered_set<int> new_vl;
	for (auto i : sector[sy][sx]) {
		if (i > MAX_USER) continue;
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) new_vl.insert(i);
	}
	/*
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._s_state != ST_INGAME)continue;
		if (distance(npc_id, i)) new_vl.insert(i);
	}
	*/
	for (auto p_id : new_vl) {
		clients[p_id].vl.lock();
		if (0 == clients[p_id].view_list.count(npc_id)) {
			clients[p_id].view_list.insert(npc_id);
			clients[p_id].vl.unlock();
			clients[p_id].send_add_object(npc_id);
		}
		else {
			clients[p_id].vl.unlock();
			clients[p_id].send_move_packet(npc_id, 0);
		}
	}
	for (auto p_id : old_vl) {
		if (0 == new_vl.count(p_id)) {
			clients[p_id].vl.lock();
			if (clients[p_id].view_list.count(npc_id) == 1) {
				clients[p_id].view_list.erase(p_id);
				clients[p_id].vl.unlock();
				clients[p_id].send_remove_object(npc_id);
			}
			else {
				clients[p_id].vl.unlock();
			}
		}
	}
}

void do_ai_ver_1() {
	for (;;) {
		auto start_t = chrono::system_clock::now();
		for (int i = 0; i < NUM_NPC; ++i) {
			int npc_id = i + MAX_USER;
			if (start_t > clients[npc_id].next_move_time) {
				move_npc(npc_id);
				clients[npc_id].next_move_time = start_t + chrono::seconds(1);
			}		
		}
	}
}

void do_ai_ver_hearth() {
	for (;;) {
		auto start_t = chrono::system_clock::now();
		for (int i = 0; i < NUM_NPC; ++i) {
			int npc_id = i + MAX_USER;
			if (clients[npc_id]._s_state != ST_INGAME) continue;
			switch (clients[npc_id].npc_type) {
			case NPC_PEACE: {
				lua_State* L = clients[npc_id].L;
				if (clients[npc_id].attack_target_id >= 0) {
					int atk_id = clients[npc_id].attack_target_id;
					int damage = 10;

					if (!attack_ok(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y)) {
						if (!distance(npc_id, atk_id)) {
							clients[npc_id].attack_target_id = -1;
							continue;
						}
						AstarSearch(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y, npc_id);
						move_npc(npc_id, clients[npc_id].next_move_position.first, clients[npc_id].next_move_position.second);
					}
					else {
						lock_guard<mutex> sl{ clients[atk_id]._sl };
						if (clients[atk_id]._s_state != ST_INGAME) {
							clients[npc_id].attack_target_id = -1;
							continue;
						}
						clients[atk_id].hp -= damage;
						cout << clients[atk_id].hp << endl;
						clients[atk_id].send_attack_info_packet(npc_id, damage, ATK_NtoP);
						if (clients[atk_id].hp <= 0) {
							cout << clients[atk_id]._name << " DEAD" << endl;
							clients[atk_id].hp = 0;
							clients[atk_id].exp /= 2;
							clients[atk_id]._s_state = ST_DEAD;

							g_viewlock.lock();
							for (auto t : clients[atk_id].view_list) {
								clients[atk_id].send_remove_object(t);
								clients[t].send_remove_object(atk_id);
							}
							clients[atk_id].view_list.clear();
							g_viewlock.unlock();

							clients[atk_id].send_chat_packet("SYSTEM", L"사망하였습니다. 잠시후 부활합니다.", CT_SYSTEM);
							//리스폰타이머
							add_timer(atk_id, 5000, EV_RESPAWN, atk_id);
							clients[npc_id].attack_target_id = -1;
						}
						clients[atk_id].send_stat_packet(atk_id);
						if (!clients[atk_id].regen) {
							clients[atk_id].regen = true;
							add_timer(atk_id, 1000, EV_HEAL, atk_id);
						}
					}
				}
				break;
			}
			case NPC_PEACE_ROAM: {
					lua_State* L = clients[npc_id].L;
					if (clients[npc_id].attack_target_id >= 0) {
						int atk_id = clients[npc_id].attack_target_id;
						int damage = 10;

						if (!attack_ok(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y)) {
							AstarSearch(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y, npc_id);
							//cout << clients[npc_id].next_move_position.first << " " << clients[npc_id].next_move_position.second << endl;
							move_npc(npc_id, clients[npc_id].next_move_position.first, clients[npc_id].next_move_position.second);
						}
						else {
							lock_guard<mutex> sl{ clients[atk_id]._sl };
							if (clients[atk_id]._s_state != ST_INGAME) {
								clients[npc_id].attack_target_id = -1;
								continue;
							}
							clients[atk_id].hp -= damage;
							clients[atk_id].send_attack_info_packet(npc_id, damage, ATK_NtoP);
							if (clients[atk_id].hp <= 0) {
								cout << clients[atk_id]._name << " DEAD" << endl;
								clients[atk_id].hp = 0;
								clients[atk_id].exp /= 2;
								clients[atk_id]._s_state = ST_DEAD;

								g_viewlock.lock();
								for (auto t : clients[atk_id].view_list) {
									clients[atk_id].send_remove_object(t);
									clients[t].send_remove_object(atk_id);
								}
								clients[atk_id].view_list.clear();
								g_viewlock.unlock();

								clients[atk_id].send_chat_packet("SYSTEM", L"사망하였습니다. 잠시후 부활합니다.", CT_SYSTEM);
								//리스폰타이머
								add_timer(atk_id, 5000, EV_RESPAWN, atk_id);
								clients[npc_id].attack_target_id = -1;
							}
							clients[atk_id].send_stat_packet(atk_id);
							if (!clients[atk_id].regen) {
								clients[atk_id].regen = true;
								add_timer(atk_id, 1000, EV_HEAL, atk_id);
							}
						}
					}
					else {
						move_npc(npc_id);
					}
				break;
			}
			case NPC_AGRO: {
				lua_State* L = clients[npc_id].L;
				if (clients[npc_id].attack_target_id >= 0) {
					int atk_id = clients[npc_id].attack_target_id;
					int damage = 10;

					if (!attack_ok(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y)) {
						if (!distance(npc_id, atk_id)) {
							clients[npc_id].attack_target_id = -1;
							continue;
						}
						AstarSearch(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y, npc_id);
						move_npc(npc_id, clients[npc_id].next_move_position.first, clients[npc_id].next_move_position.second);
					}
					else {
						lock_guard<mutex> sl{ clients[atk_id]._sl };
						if (clients[atk_id]._s_state != ST_INGAME) {
							clients[npc_id].attack_target_id = -1;
							continue;
						}
						clients[atk_id].hp -= damage;
						clients[atk_id].send_attack_info_packet(npc_id, damage, ATK_NtoP);
						if (clients[atk_id].hp <= 0) {
							cout << clients[atk_id]._name << " DEAD" << endl;
							clients[atk_id].hp = 0;
							clients[atk_id].exp /= 2;
							clients[atk_id]._s_state = ST_DEAD;

							g_viewlock.lock();
							for (auto t : clients[atk_id].view_list) {
								clients[atk_id].send_remove_object(t);
								clients[t].send_remove_object(atk_id);
							}
							clients[atk_id].view_list.clear();
							g_viewlock.unlock();

							clients[atk_id].send_chat_packet("SYSTEM", L"사망하였습니다. 잠시후 부활합니다.", CT_SYSTEM);
							//리스폰타이머
							add_timer(atk_id, 5000, EV_RESPAWN, atk_id);
							clients[npc_id].attack_target_id = -1;
						}
						clients[atk_id].send_stat_packet(atk_id);
						if (!clients[atk_id].regen) {
							clients[atk_id].regen = true;
							add_timer(atk_id, 1000, EV_HEAL, atk_id);
						}
					}
				}
				else {
					for (int i = 0; i < MAX_USER; ++i) {
						if (clients[i]._s_state != ST_INGAME) continue;
						if (distance(i, npc_id)) {
							clients[npc_id].attack_target_id = i;
							break;
						}
					}
				}
				break;
			}
			case NPC_AGRO_ROAM: {
				lua_State* L = clients[npc_id].L;
				if (clients[npc_id].attack_target_id >= 0) {
					int atk_id = clients[npc_id].attack_target_id;
					int damage = 10;

					if (!attack_ok(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y)) {
						AstarSearch(clients[npc_id].x, clients[npc_id].y, clients[atk_id].x, clients[atk_id].y, npc_id);
						move_npc(npc_id, clients[npc_id].next_move_position.first, clients[npc_id].next_move_position.second);
					}
					else {
						lock_guard<mutex> sl{ clients[atk_id]._sl };
						if (clients[atk_id]._s_state != ST_INGAME) {
							clients[npc_id].attack_target_id = -1;
							continue;
						}
						clients[atk_id].hp -= damage;
						cout << clients[atk_id].hp << endl;
						clients[atk_id].send_attack_info_packet(npc_id, damage, ATK_NtoP);
						if (clients[atk_id].hp <= 0) {
							cout << clients[atk_id]._name << " DEAD" << endl;
							clients[atk_id].hp = 0;
							clients[atk_id].exp /= 2;
							clients[atk_id]._s_state = ST_DEAD;

							g_viewlock.lock();
							for (auto t : clients[atk_id].view_list) {
								clients[atk_id].send_remove_object(t);
								clients[t].send_remove_object(atk_id);
							}
							clients[atk_id].view_list.clear();
							g_viewlock.unlock();

							clients[atk_id].send_chat_packet("SYSTEM", L"사망하였습니다. 잠시후 부활합니다.", CT_SYSTEM);
							//리스폰타이머
							add_timer(atk_id, 5000, EV_RESPAWN, atk_id);
							clients[npc_id].attack_target_id = -1;
						}
						clients[atk_id].send_stat_packet(atk_id);
						if (!clients[atk_id].regen) {
							clients[atk_id].regen = true;
							add_timer(atk_id, 1000, EV_HEAL, atk_id);
						}
					}
				}
				else {
					move_npc(npc_id);
					for (int i = 0; i < MAX_USER; ++i) {
						if (clients[i]._s_state != ST_INGAME) continue;
						if (distance(i, npc_id)) {
							clients[npc_id].attack_target_id = i;
							break;
						}
					}
				}
				break;
			}
			}
		}
		auto end_t = chrono::system_clock::now();
		auto ai_t = end_t - start_t;
		//cout << "AI time : " << chrono::duration_cast<chrono::milliseconds>(ai_t).count();
		//cout << "ms\n";
		this_thread::sleep_until(start_t + chrono::seconds(1));
	}
}

int API_SendMessage(lua_State* L) {
	int client_id = lua_tonumber(L, -3);
	int npc_id = lua_tonumber(L, -2);
	const char* mess = lua_tostring(L, -1);
	lua_pop(L, 4);

	int str_len = strlen(mess) + 1;
	int wstr_len = MultiByteToWideChar(CP_UTF8, 0, mess, str_len, NULL, 0);
	wchar_t* wstr = new wchar_t[wstr_len];
	MultiByteToWideChar(CP_UTF8, 0, mess, str_len, wstr, wstr_len);

	clients[client_id].send_chat_packet(clients[npc_id]._name, wstr, CT_NORMAL);

	delete[] wstr;
	return 0;
}

int API_get_x(lua_State* L) {
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);

	int x = clients[obj_id].x;
	
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L) {
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);

	int y = clients[obj_id].y;

	lua_pushnumber(L, y);
	return 1;
}

void initialize_npc() {
	for (int i = 0; i < NUM_NPC + MAX_USER; ++i) {
		clients[i]._id = i;
	}
	cout << "NPC Initialize begin\n";
	for (int i = 0; i < NUM_NPC; ++i) {
		int npc_id = i + MAX_USER;
		sprintf_s(clients[npc_id]._name, "M-%d", npc_id);
		lua_State* L = luaL_newstate();
		clients[npc_id].L = L;
		clients[npc_id]._s_state = ST_INGAME;
		clients[npc_id].npc_type = i;
		clients[npc_id].spawn_x = rand()%W_WIDTH;
		clients[npc_id].spawn_y = rand()%W_HEIGHT;
		clients[npc_id].x = clients[npc_id].spawn_x;
		clients[npc_id].y = clients[npc_id].spawn_y;
		clients[npc_id].hpmax = 15;
		clients[npc_id].level = 5;
		clients[npc_id].hp = clients[npc_id].hpmax;

		int sy = clients[npc_id].sectornum.first = clients[npc_id].y / 50;
		int sx = clients[npc_id].sectornum.second = clients[npc_id].x / 50;
		sector[sy][sx].insert(npc_id);

		luaL_openlibs(L);
		luaL_loadfile(L, "hello.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_object_id");
		lua_pushnumber(L, npc_id);
		lua_pcall(L, 1, 0, 0);

		lua_register(L, "API_chat", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
	}
	cout << "NPC Initialize complete\n";
}

void post_timer_event(EVENT_TYPE type, int target, int owner) {
	OVER_EXP* ex_over = new OVER_EXP;
	switch (type) {
	case EV_HEAL: {
		ex_over->_comp_type = OP_AUTOHEAL;
		ex_over->target_id = target;
		break;
	}
	case EV_RESPAWN: {
		ex_over->_comp_type = OP_RESPAWN;
		ex_over->target_id = target;
		break;
	}
	}
	PostQueuedCompletionStatus(g_h_iocp, 1, owner, &ex_over->_over);
}

void do_timer() {
	while (true) {
		volatile bool empty = timer_queue.empty();
		if (!empty) {
			lock_guard<mutex> tl{ timer_l };
			TIMER_EVENT timer_ev = timer_queue.top();
			auto now_t = chrono::system_clock::now();
			if (timer_ev.act_time < now_t) {
				switch (timer_ev.ev) {
					case EV_HEAL: {
						post_timer_event(EV_HEAL, timer_ev.target_id, timer_ev.object_id);
						break;
					}
					case EV_RESPAWN: {
						post_timer_event(EV_RESPAWN, timer_ev.target_id, timer_ev.object_id);
						break;
					}
					case EV_BUF: {
						post_timer_event(EV_BUF, timer_ev.target_id, timer_ev.object_id);
						break;
					}
				}
				timer_queue.pop();
			}
		}
		SleepEx(10, true);
	}
}

void load_mapdata() {
	ifstream fin;
	char c;

	cout << "START MAP DATA LOAD\n";
	fin.open("mapdata", ios::binary);
	for (int i = 0; i < W_WIDTH; ++i) {
		for (int j = 0; j < W_HEIGHT; ++j) {
			fin.get(c);
			WorldSpace[i][j] = c;
		}
	}
	fin.close();
	cout << "END MAP DATA LOAD\n";
}

int main()
{
	load_mapdata();
	initialize_npc();

	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);
	int client_id = 0;

	g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), g_h_iocp, 9999, 0);
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	OVER_EXP a_over;
	a_over._comp_type = OP_ACCEPT;
	a_over._wsabuf.buf = reinterpret_cast<CHAR *>(c_socket);
	AcceptEx(g_s_socket, c_socket, a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &a_over._over);

	vector <thread> worker_threads;
	for (int i = 0; i < 6; ++i)
		worker_threads.emplace_back(do_worker);
	thread timer_thread{ do_timer };
	thread ai_thread{ do_ai_ver_hearth };

	ai_thread.join();
	timer_thread.join();
	for (auto& th : worker_threads)
		th.join();
	closesocket(g_s_socket);
	WSACleanup();
}
