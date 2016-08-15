typedef struct Cell Cell;
typedef struct Player Player;

struct Cell
{
	Quad;

	Player*	owner;
	int	mass;
	double	speed;
	ulong	color;

	/* 1 if food, 0 otherwise */
	int 	isfood;
};

struct Player
{
	ulong	id;
	/* name if any */
	char*	name;

	/* overall position */
	Point	p;

	/* players target center */
	Point	target;

	ulong	color;

	Cell*	cells;
	int	ncells;
	int	maxcells;
};

Player*	pmake(char *name, Point pos, int mass);
void	pmove(Player*);

ulong	getid(void);
int	mass2radius(int);

void	ptdist(Point a, Point b, double *dist, double *rad);

int	parseint(char*, int*);
int	parseulong(char*, ulong*);
int	parsedouble(char*, double*);

Channel*	timerchan(int ms, char *tag);
