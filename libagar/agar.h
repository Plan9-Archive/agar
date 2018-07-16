typedef struct Cell Cell;
typedef struct Player Player;

struct Cell
{
	Avl;
	//Quad;

	ulong id;

	Point3 pos;

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
	Point3	pos;

	/* players target center */
	Point3	target;

	ulong	color;

	Cell*	cells;
	int	ncells;
	int	maxcells;
};

Player*	pmake(char *name, Point3 pos, int mass);
void	pmove(Player*, double);

int		cellcmp(Avl *a, Avl *b);

ulong	getid(void);
int	mass2radius(int);

void	ptdist(Point3 a, Point3 b, double *dist, double *rad);

Point3	pttopt3(Point);
Point	pt3topt(Point3);

Quad	celltoquad(Cell *c);

int	parseint(char*, int*);
int	parseulong(char*, ulong*);
int	parsedouble(char*, double*);

double	dtime(void);

Channel*	timerchan(int ms, char *tag);

extern Point3 ZP3;
