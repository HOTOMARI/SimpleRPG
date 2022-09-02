constexpr int PORT_NUM = 4597;
constexpr int BUF_SIZE = 255;
constexpr int CHAT_SIZE = 100;
constexpr int ID_SIZE = 11;
constexpr int NAME_SIZE = 20;

constexpr int W_WIDTH = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int MAX_USER = 10000;
constexpr int NUM_NPC = 200000;

constexpr int SIGHT = 5;
constexpr int PSIGHT = 10;

// Packet ID
constexpr char CS_LOGIN = 0;
constexpr char CS_MOVE = 1;
constexpr char CS_AUTH = 2;
constexpr char CS_CHAT = 3;
constexpr char CS_ATTACK = 4;

constexpr char SC_LOGIN_OK = 11;
constexpr char SC_LOGIN_FAIL = 12;
constexpr char SC_ADD_OBJECT = 13;
constexpr char SC_REMOVE_OBJECT = 14;
constexpr char SC_MOVE_OBJECT = 15;
constexpr char SC_CHAT = 16;
constexpr char SC_STAT_CHANGE = 17;
constexpr char SC_AUTH = 18;
constexpr char SC_KILL = 19;
constexpr char SC_RESPAWN = 20;
constexpr char SC_ATTACK_INFO = 21;
constexpr char SC_EXP_INFO = 22;

// Move Dirrection
constexpr char MV_UP = 0;
constexpr char MV_DOWN = 1;
constexpr char MV_LEFT = 2;
constexpr char MV_RIGHT = 3;

// LoginFail Reason
constexpr char LF_NONE = -1;
constexpr char LF_INVALID_NAME = 0;
constexpr char LF_ALREADY_LOGIN = 1;
constexpr char LF_SERVER_FULL = 2;

// ChatType
constexpr char CT_NORMAL = 0;
constexpr char CT_TELL = 1;
constexpr char CT_SHOUT = 2;
constexpr char CT_SYSTEM = 3;

//NPC TYPE
constexpr char NPC_NONE = 0;
constexpr char NPC_PEACE = 1;
constexpr char NPC_PEACE_ROAM = 2;
constexpr char NPC_AGRO = 3;
constexpr char NPC_AGRO_ROAM = 4;

// Attack type (who atk)
constexpr char ATK_PtoN = 0;
constexpr char ATK_NtoP = 1;

// Attack type (what atk)
constexpr char ATK_Normal = 0;
constexpr char ATK_Directional = 1;
constexpr char ATK_Buf = 2;


#pragma pack (push, 1)
struct CS_AUTH_ACCOUNT_PACKET {
	unsigned char size;
	char	type;
	char	id[ID_SIZE];
};

struct SC_AUTH_ACCOUNT_PACKET {
	unsigned char size;
	char	type;
	char	name[NAME_SIZE];
	bool result;
};

struct CS_LOGIN_PACKET {
	unsigned char size;
	char	type;
	char	id[NAME_SIZE];
};

struct CS_MOVE_PACKET {
	unsigned char size;
	char	type;
	char	direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	unsigned int client_time;
};

struct CS_ATTACK_PACKET {
	unsigned char size;
	char	type;
	char	atk_type;
};

struct CS_CHAT_PACKET {
	unsigned char size;
	char	type;
	int		target_id;
	char	chat_type;			// 1 : say,  2 : tell, 3 : shout
	wchar_t	mess[CHAT_SIZE];
};

struct SC_LOGIN_OK_PACKET {
	unsigned char size;
	char	type;
	int	id;
	short race;
	short x, y;
	short level;
	int	  exp;
	int   hp, hpmax;
};

struct SC_LOGIN_FAIL_PACKET {
	unsigned char size;
	char	type;
	int		reason;				// 0 : Invalid Name  (특수문자, 공백 제외)
								// 1 : Name Already Playing
								// 2 : Server Full
};

struct SC_ADD_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	short	x, y;
	int	race;			// 종족 : 인간, 엘프, 드워프, 오크, 드래곤
							// 클라이언트에서 종족별로 별도의 그래픽 표현
							// 추가적으로 성별이나, 직업을 추가할 수 있다.
	int npc_type;
	char	name[NAME_SIZE];
	short	level;
	int		hp, hpmax;
};

struct SC_REMOVE_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int	id;
};

struct SC_MOVE_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int	id;
	short	x, y;
	unsigned int client_time;
};

struct SC_CHAT_PACKET {
	unsigned char size;
	char	type;
	char	name[NAME_SIZE];
	char	chat_type;			// 0 : say,  1 : tell, 2 : shout, 3 : system
	wchar_t	mess[CHAT_SIZE];
};

struct SC_STAT_CHANGE_PACKET {
	unsigned char size;
	char	type;
	int		id;
	short	level;
	int		exp;
	int		hp, hpmax;
	bool buf;
};

struct SC_KILL_PACKET {
	unsigned char size;
	char	type;
};

struct SC_RESPAWN_PACKET {
	unsigned char size;
	char	type;
};

struct SC_ATTACK_INFO_PACKET {
	unsigned char size;
	char	type;
	int atk_type;
	int id;
	int damage;
};

struct SC_EXP_INFO_PACKET {
	unsigned char size;
	char	type;
	int id;
	int exp;
};

#pragma pack (pop)