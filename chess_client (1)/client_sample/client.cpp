#define SFML_STATIC 1
#define _WINSOCKAPI_
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <Windows.h>
using namespace std;

#ifdef _DEBUG
#pragma comment (lib, "lib/sfml-graphics-s-d.lib")
#pragma comment (lib, "lib/sfml-window-s-d.lib")
#pragma comment (lib, "lib/sfml-system-s-d.lib")
#pragma comment (lib, "lib/sfml-network-s-d.lib")
#else
#pragma comment (lib, "lib/sfml-graphics-s.lib")
#pragma comment (lib, "lib/sfml-window-s.lib")
#pragma comment (lib, "lib/sfml-system-s.lib")
#pragma comment (lib, "lib/sfml-network-s.lib")
#endif
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

#include "..\..\IOCP_server_multithread\IOCP_server_multithread\protocol.h"
sf::TcpSocket socket;

constexpr auto SCREEN_WIDTH = 20;
constexpr auto SCREEN_HEIGHT = 20;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH + 10 + 350;

constexpr int WINDOW_GAME_MODE = 0;
constexpr int WINDOW_CHAT_MODE = 1;

int g_left_x;
int g_top_y;
int g_myid;

char g_loginid[ID_SIZE];
char g_myname[NAME_SIZE];
bool g_rcvlogin = false;
int g_windowmode = WINDOW_GAME_MODE;
int g_chatmode = CT_NORMAL;
int g_tellTarget = -1;


sf::RenderWindow* g_window;
sf::Font g_font;
sf::Font g_chatfont;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Text m_name;
	sf::Text m_chat;
public:
	int m_x, m_y;
	int level, hp, exp, maxhp;
	int race;
	bool buf_on = false;
	int npc_type;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("DEFAULT");
	}
	OBJECT() {
		m_showing = false;
	}
	void setAvatarRace() {
		m_sprite.setTextureRect(sf::IntRect(64 * race, 0, 64, 64));
	}
	void setNpcType() {
		m_sprite.setTextureRect(sf::IntRect(64 * (5 + npc_type), 0, 64, 64));
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		m_name.setPosition(rx - 10, ry - 20);
		g_window->draw(m_name);
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}
	string get_name() {
		return m_name.getString();
	}
};

OBJECT avatar;
//unordered_map<int, OBJECT>players;
//unordered_map<int, OBJECT>npcs;
OBJECT players[MAX_USER];
OBJECT npcs[NUM_NPC];

OBJECT white_tile;
OBJECT black_tile;
OBJECT wall;

class CHAT {
	sf::Text chat;
	sf::Text sender;
	short type = CT_NORMAL;

public:
	CHAT() {
		chat.setFont(g_chatfont);
		chat.setStyle(sf::Text::Bold);
		sender.setFont(g_chatfont);
		sender.setStyle(sf::Text::Bold);
	}

	CHAT(wstring s) {
		chat.setFont(g_chatfont);
		chat.setString(s);
		chat.setStyle(sf::Text::Bold);
		sender.setFont(g_chatfont);
		sender.setStyle(sf::Text::Bold);
	}
	CHAT(string s) {
		chat.setFont(g_chatfont);
		chat.setString(s);
		chat.setStyle(sf::Text::Bold);
		sender.setFont(g_chatfont);
		sender.setStyle(sf::Text::Bold);
	}

	void SetText(wstring s) {
		chat.setString(s);
	}
	void SetText(string s) {
		chat.setString(s);
	}

	void SetSender(char* name) {
		sender.setString(name);
	}
	void SetSender(wstring name) {
		sender.setString(name);
	}
	void SetSender(string name) {
		sender.setString(name);
	}

	//for my chat
	void SetPosition() {
		chat.setPosition(200, WINDOW_HEIGHT - 50);
	}
	//for chat log
	void SetPosition(int i) {
		sender.setPosition(20, WINDOW_HEIGHT - i * 30 - 100);
		chat.setPosition(200, WINDOW_HEIGHT - i * 30 - 100);
	}

	void Draw() {
		g_window->draw(sender);
		g_window->draw(chat);
	}

	void SetType(short i) {
		type = i;
		switch (type) {
		case CT_NORMAL:
			sender.setFillColor(sf::Color(255, 255, 255));
			chat.setFillColor(sf::Color(255, 255, 255));
			break;
		case CT_TELL:
			sender.setFillColor(sf::Color(255, 255, 0));
			chat.setFillColor(sf::Color(255, 255, 0));
			break;
		case CT_SHOUT:
			sender.setFillColor(sf::Color(0, 255, 255));
			chat.setFillColor(sf::Color(0, 255, 255));
			break;
		case CT_SYSTEM:
			sender.setFillColor(sf::Color(255, 0, 255));
			chat.setFillColor(sf::Color(255, 0, 255));
			break;
		}
	}
};

sf::Texture* board;
sf::Texture* background;
sf::Texture* deadbackground;
sf::Texture* pieces;
sf::Texture* buf;

sf::Sprite backgrond_sprite;
sf::Sprite deadbackgrond_sprite;
sf::Sprite buf_sprite;

vector<CHAT> chatlog;
CHAT mychat_obj;
wstring mychat_str;

char WorldSpace[2000][2000];

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

void client_initialize()
{
	load_mapdata();

	board = new sf::Texture;
	pieces = new sf::Texture;
	background = new sf::Texture;
	deadbackground = new sf::Texture;
	buf = new sf::Texture;
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");
	background->loadFromFile("background.bmp");
	buf->loadFromFile("buf.png");
	background->setRepeated(true);
	deadbackground->loadFromFile("dead.bmp");
	deadbackground->setRepeated(true);
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	if (false == g_chatfont.loadFromFile("SourceHanSansK-Normal.otf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	wall = OBJECT{ *board, 69 + 64, 5, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
	avatar.move(4, 4);
	for (auto& pl : players) {
		pl = OBJECT{ *pieces, 64, 0, 64, 64 };
	}
	for (auto& pl : npcs) {
		pl = OBJECT{ *pieces, 0, 0, 64, 64 };
	}
	
	backgrond_sprite.setTexture(*background);
	backgrond_sprite.setTextureRect(sf::IntRect(0, 0, SCREEN_WIDTH * 64 + 25, SCREEN_HEIGHT * 64 + 25));

	deadbackgrond_sprite.setTexture(*deadbackground);
	deadbackgrond_sprite.setTextureRect(sf::IntRect(0, 0, SCREEN_WIDTH * 64 + 25, SCREEN_HEIGHT * 64 + 25));

	buf_sprite.setTexture(*buf);
	buf_sprite.setPosition(300, 50);
}

void client_finish()
{
	delete board;
	delete pieces;
}

void send_packet(void* packet)
{
	unsigned char* p = reinterpret_cast<unsigned char*>(packet);
	size_t sent = 0;
	socket.send(packet, p[0], sent);
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_AUTH: {
		SC_AUTH_ACCOUNT_PACKET* packet = reinterpret_cast<SC_AUTH_ACCOUNT_PACKET*>(ptr);
		strcpy_s(g_myname, packet->name);
		cout << g_myname << endl;
		g_rcvlogin = true;
		break;
	}
	case SC_LOGIN_OK:
	{
		g_rcvlogin = true;

		SC_LOGIN_OK_PACKET* packet = reinterpret_cast<SC_LOGIN_OK_PACKET*>(ptr);
		g_myid = packet->id;
		avatar.m_x = packet->x;
		avatar.m_y = packet->y;
		avatar.level = packet->level;
		avatar.hp = packet->hp;
		avatar.maxhp = packet->hpmax;
		avatar.exp = packet->exp;
		avatar.race = packet->race;

		g_left_x = packet->x - 7;
		g_top_y = packet->y - 7;

		avatar.setAvatarRace();
		avatar.set_name(g_myname);
		avatar.show();
		break;
	}
	case SC_LOGIN_FAIL: {
		SC_LOGIN_FAIL_PACKET* packet = reinterpret_cast<SC_LOGIN_FAIL_PACKET*>(ptr);
		cout << "FAIL: ";
		switch (packet->reason) {
		case LF_INVALID_NAME:
			cout << "잘못된 이름\n";
			MessageBox(NULL, "잘못된 이름입니다.", "Error", MB_OK);
			exit(LF_INVALID_NAME);
			break;
		case LF_ALREADY_LOGIN:
			cout << "중복 접속\n";
			MessageBox(NULL, "중복 접속이 감지되었습니다.", "Error", MB_OK);
			exit(LF_ALREADY_LOGIN);
			break;
		case LF_SERVER_FULL:
			cout << "서버 인원 초과\n";
			MessageBox(NULL, "서버 최대 접속인원 초과", "Error", MB_OK);
			exit(LF_SERVER_FULL);
			break;
		}
		break;
	}
	case SC_ADD_OBJECT:
	{
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		int id = my_packet->id;

		if (id < MAX_USER) {
			players[id].race = my_packet->race;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].setAvatarRace();
			players[id].show();
		}
		else {
			npcs[id - MAX_USER].race = 5;
			npcs[id - MAX_USER].npc_type = my_packet->npc_type;
			npcs[id - MAX_USER].move(my_packet->x, my_packet->y);
			npcs[id - MAX_USER].set_name(my_packet->name);
			npcs[id - MAX_USER].setNpcType();
			npcs[id - MAX_USER].show();
		}
		break;
	}
	case SC_MOVE_OBJECT:
	{
		SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - 7;
			g_top_y = my_packet->y - 7;
		}
		else if (other_id < MAX_USER) {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		else {
			npcs[other_id - MAX_USER].move(my_packet->x, my_packet->y);
		}
		break;
	}
	case SC_REMOVE_OBJECT:
	{
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else if (other_id < MAX_USER) {
			players[other_id].hide();
		}
		else {
			
			npcs[other_id - MAX_USER].hide();
		}
		break;
	}
	case SC_CHAT:
	{
		SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
		wstring tmp_msg(my_packet->mess);
		CHAT tmp_obj(tmp_msg);
		tmp_obj.SetType(my_packet->chat_type);
		tmp_obj.SetSender(my_packet->name);
		chatlog.push_back(tmp_obj);
		break;
	}
	case SC_STAT_CHANGE: {
		SC_STAT_CHANGE_PACKET* my_packet = reinterpret_cast<SC_STAT_CHANGE_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hp = my_packet->hp;
			avatar.maxhp = my_packet->hpmax;
			avatar.exp = my_packet->exp;
			avatar.level = my_packet->level;
			avatar.buf_on = my_packet->buf;
		}
		break;
	}
	case SC_RESPAWN: {
		CS_MOVE_PACKET p;
		p.size = sizeof(p);
		p.type = CS_MOVE;
		p.direction = -1;
		send_packet(&p);
		break;
	}
	case SC_ATTACK_INFO: {
		SC_ATTACK_INFO_PACKET* my_packet = reinterpret_cast<SC_ATTACK_INFO_PACKET*>(ptr);
		int other_id = my_packet->id;
		wchar_t buff[20] = L"";
		string tmpsender = { "BATTLE" };
		size_t size = npcs[other_id - MAX_USER].get_name().length() + 1;
		wstring tmp(size, L'#');

		mbstowcs_s(NULL, &tmp[0], size, npcs[other_id - MAX_USER].get_name().c_str(), size);
		tmp.erase(tmp.length() - 1, 1);
		switch (my_packet->atk_type) {
		case ATK_PtoN: {
			tmp.append(L"에게 ");
			tmp.append(to_wstring(my_packet->damage));
			tmp.append(L"데미지");
			break;
		}
		case ATK_NtoP: {
			tmp.append(L"가 플레이어에게 ");
			tmp.append(to_wstring(my_packet->damage));
			tmp.append(L"데미지");
			break;
		}
		}
		
		
		CHAT tmpChat;
		tmpChat.SetText(tmp);
		tmpChat.SetType(CT_SYSTEM);
		tmpChat.SetSender(tmpsender);

		chatlog.push_back(tmpChat);
		break;
	}
	case SC_EXP_INFO: {
		SC_EXP_INFO_PACKET* my_packet = reinterpret_cast<SC_EXP_INFO_PACKET*>(ptr);
		int other_id = my_packet->id;
		wchar_t buff[20] = L"";
		string tmpsender = { "BATTLE" };
		size_t size = npcs[other_id - MAX_USER].get_name().length() + 1;
		wstring tmp(size, L'#');

		mbstowcs_s(NULL, &tmp[0], size, npcs[other_id - MAX_USER].get_name().c_str(), size);
		tmp.erase(tmp.length() - 1, 1);
		tmp.append(L"를 무찔러 ");
		tmp.append(to_wstring(my_packet->exp));
		tmp.append(L"의 경험치 획득");

		CHAT tmpChat;
		tmpChat.SetText(tmp);
		tmpChat.SetType(CT_SYSTEM);
		tmpChat.SetSender(tmpsender);

		chatlog.push_back(tmpChat);
		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

int calc_maxExp(int level) {
	int retval = 100;
	for (int i = 1; i < level; ++i) {
		retval = retval * 2;
	}
	return retval;
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	//채팅로그
	{
		int i = 0;
		for (auto chat = chatlog.rbegin(); chat != chatlog.rend();chat++) {
			chat->SetPosition(i);
			chat->Draw();
			i++;
		}
	}
	//내 채팅
	if (g_windowmode == WINDOW_CHAT_MODE)
	{
		mychat_obj.SetPosition();
		mychat_obj.Draw();
	}
	// 채팅모드출력
	{
		sf::Text chatmode;
		chatmode.setFont(g_chatfont);
		switch (g_chatmode) {
		case CT_NORMAL:
			chatmode.setFillColor(sf::Color(255, 255, 255));
			chatmode.setString(L"일반");
			break;
		case CT_TELL:
			chatmode.setFillColor(sf::Color(255, 255, 0));
			chatmode.setString(L"귓속말");
			break;
		case CT_SHOUT:
			chatmode.setFillColor(sf::Color(0, 255, 255));
			chatmode.setString(L"외치기");
			break;
		}
		chatmode.setPosition(20, WINDOW_HEIGHT - 50);
		g_window->draw(chatmode);
	}

	//배경
	g_window->draw(backgrond_sprite);

	//맵
	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if ((tile_x > W_WIDTH-1) || (tile_y > W_HEIGHT-1)) continue;

			if (WorldSpace[tile_y][tile_x] == 0) {
				if (((tile_x + tile_y) % 6) < 3) {
					white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
					white_tile.a_draw();
				}
				else
				{
					black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
					black_tile.a_draw();
				}
			}
			else {
				wall.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				wall.a_draw();
			}
		}

	//캐릭터들
	avatar.draw();
	for (auto& pl : players) pl.draw();
	for (auto& pl : npcs) pl.draw();
	// 미니맵
	{
		// 미니맵 배경
		sf::RectangleShape mapBackground(sf::Vector2f(2000.f, 2000.f));
		mapBackground.setSize(sf::Vector2f(200.f, 200.f));
		mapBackground.setFillColor(sf::Color(80, 80, 80));
		mapBackground.setPosition(SCREEN_WIDTH * 65 - 200, SCREEN_HEIGHT * 65 - 200);
		// 미니맵에서 플레이어 위치 표시
		sf::RectangleShape PlayerOnMap(sf::Vector2f(10.f, 10.f));
		PlayerOnMap.setSize(sf::Vector2f(5.f, 5.f));
		PlayerOnMap.setFillColor(sf::Color(0, 255, 0));
		PlayerOnMap.setPosition(SCREEN_WIDTH * 65 - (2000 - avatar.m_x) / 10, SCREEN_HEIGHT * 65 - (2000 - avatar.m_y) / 10);

		g_window->draw(mapBackground);
		g_window->draw(PlayerOnMap);
	}
	// 상태창
	{
		// 배경
		sf::RectangleShape statusWindow(sf::Vector2f(200.f, 120.f));
		statusWindow.setFillColor(sf::Color(80, 80, 80, 125));
		statusWindow.setPosition(10, 10);
		// 텍스트
		sf::Text status_hp;
		status_hp.setFont(g_font);
		string hpText;
		hpText.append(to_string(avatar.hp));
		hpText.append(" / ");
		hpText.append(to_string(avatar.maxhp));
		status_hp.setString(hpText);
		status_hp.setFillColor(sf::Color(255, 0, 0));
		status_hp.setStyle(sf::Text::Bold);
		status_hp.setPosition(20, 20);

		sf::Text status_level;
		status_level.setFont(g_font);
		string levelText;
		levelText.append(to_string(avatar.level));
		status_level.setString(levelText);
		status_level.setFillColor(sf::Color(0, 255, 0));
		status_level.setStyle(sf::Text::Bold);
		status_level.setPosition(20, 50);

		sf::Text status_exp;
		status_exp.setFont(g_font);
		string expText;
		expText.append(to_string(avatar.exp));
		expText.append(" / ");
		expText.append(to_string(calc_maxExp(avatar.level)));
		status_exp.setString(expText);
		status_exp.setFillColor(sf::Color(0, 255, 200));
		status_exp.setStyle(sf::Text::Bold);
		status_exp.setPosition(20, 80);

		g_window->draw(statusWindow);
		g_window->draw(status_hp);
		g_window->draw(status_level);
		g_window->draw(status_exp);

		if (avatar.buf_on)
			g_window->draw(buf_sprite);
	}

	// 죽는 배경
	if (avatar.hp <= 0)
		g_window->draw(deadbackgrond_sprite);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = socket.connect("127.0.0.1", PORT_NUM);
	socket.setBlocking(false);

	wcout << L"ID입력: ";
	cin >> g_loginid;
	g_loginid[10] = 0;

	{
		CS_LOGIN_PACKET ap;
		ap.size = sizeof(CS_LOGIN_PACKET);
		ap.type = CS_LOGIN;
		strcpy_s(ap.id, sizeof(ap.id), g_loginid);
		send_packet(&ap);
	}

	while (!g_rcvlogin) {
		char net_buf[BUF_SIZE];
		size_t	received;

		auto recv_result = socket.receive(net_buf, BUF_SIZE, received);

		if (recv_result == sf::Socket::Error)
		{
			wcout << L"Recv 에러!";
			while (true);
		}
		if (recv_result != sf::Socket::NotReady)
			if (received > 0) process_data(net_buf, received);
	}


	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	client_initialize();

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				if (g_windowmode == WINDOW_GAME_MODE) {
					switch (event.key.code) {
					case sf::Keyboard::Left:
						direction = 2;
						break;
					case sf::Keyboard::Right:
						direction = 3;
						break;
					case sf::Keyboard::Up:
						direction = 0;
						break;
					case sf::Keyboard::Down:
						direction = 1;
						break;
					case sf::Keyboard::Escape:
						window.close();
						break;
					case sf::Keyboard::A: {
						CS_ATTACK_PACKET p;
						p.size = sizeof(p);
						p.type = CS_ATTACK;
						p.atk_type = ATK_Normal;
						send_packet(&p);
						break;
					}
					case sf::Keyboard::S: {
						CS_ATTACK_PACKET p;
						p.size = sizeof(p);
						p.type = CS_ATTACK;
						p.atk_type = ATK_Directional;
						send_packet(&p);
						break;
					}
					case sf::Keyboard::Tab:
						g_chatmode++;
						g_chatmode %= 3;
						break;
					case sf::Keyboard::Enter:
						g_windowmode = WINDOW_CHAT_MODE;
						break;
					}
					if (-1 != direction) {
						CS_MOVE_PACKET p;
						p.size = sizeof(p);
						p.type = CS_MOVE;
						p.direction = direction;
						send_packet(&p);
					}
				}
				else if (g_windowmode == WINDOW_CHAT_MODE) {
					switch (event.key.code) {
					case sf::Keyboard::Escape:
						mychat_str.clear();
						g_windowmode = WINDOW_GAME_MODE;
						break;
					case sf::Keyboard::Enter:
						{
						CS_CHAT_PACKET p;
						p.size = sizeof(p);
						p.type = CS_CHAT;
						p.chat_type = g_chatmode;
						// 귓속말 타겟 설정
						if (g_chatmode == CT_TELL) {
							p.target_id = g_tellTarget;
						}
						mychat_str.erase(0, 1);
						wcscpy_s(p.mess, mychat_str.c_str());

						wcout << p.mess << endl;
						send_packet(&p);
						mychat_str.clear();
						g_windowmode = WINDOW_GAME_MODE;
						break;
						}
					}
				}
			}
			if (event.type == sf::Event::TextEntered) {
				if (g_windowmode == WINDOW_CHAT_MODE) {
					//wcout << event.text.unicode << "\n";
					//0x0008 backspace
					if (event.text.unicode == 0x0008) {
						if (mychat_str.size() > 1) {
							mychat_str.erase(mychat_str.size() - 1);
							mychat_obj.SetText(mychat_str);
						}
					}
					else if (event.key.code == sf::Keyboard::Enter) {
					}
					else {
						mychat_str += event.text.unicode;
						mychat_obj.SetText(mychat_str);
					}
				}
			}
			if (event.type == sf::Event::MouseButtonPressed) {
				sf::Vector2i pos = sf::Mouse::getPosition(window);
				for (int i = 0; i < MAX_USER; ++i) {
					if (i == g_myid) continue;
					float rx = (players[i].m_x - g_left_x) * 65.0f + 8;
					float ry = (players[i].m_y - g_top_y) * 65.0f + 8;
					if (pos.x >= rx && pos.x < rx + 65.0f && pos.y >= ry && pos.y < ry + 65.0f) {
						g_tellTarget = i;
					}
				}
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}