typedef struct Room Room;
typedef struct Client Client;

struct Client
{
	QLock;
	Ref;

	char		genbuf[512];

	/* room name */
	char*		room;

	ulong		id;

	Player*		player;

	/* 0 write, 1 read */
	int		eventfd[2];
	Reqqueue*	eventrq;
};

struct Room
{
	QLock;
	Ref;

	char genbuf[512];

	char*	name;

	ulong*		clientids;
	int		nclients;
	int		maxclients;
	Intmap*		clients;
	Channel*	freec;

	/* track food */
	Avltree		food;
	int			nfood;
	int			maxfood;

	Room*		next;
};

extern int debug;
#define DBG if(debug)
