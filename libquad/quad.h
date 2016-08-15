typedef struct Quad Quad;
typedef struct QuadTree QuadTree;

#pragma incomplete QuadTree

enum
{
	QNCIRCLE,
	QNRECTANGLE,
};

struct Quad
{
	int type;
	union {
		/* circle */
		struct {
			Point p;
			int radius;
		};
		/* rectangle */
		struct {
			Rectangle r;
		};
	};
};

/* XXX: should be hidden in quad.c */
enum
{
	QTNW = 0,
	QTNE,
	QTSW,
	QTSE,
	QTNZONE,
};

typedef struct QuadTree QuadTree;
struct QuadTree
{
	Rectangle boundary;

	Quad **quads;
	int nquads;
	int maxquads;

	/* QTNZONE QuadTrees */
	QuadTree *zones;
};
 
Quad qcircle(Point p, int radius);
Quad qrect(Rectangle r);
Rectangle quad2rect(Quad *v);

QuadTree *qtmk(Rectangle aabb);
int qtinsert(QuadTree *qt, Quad *v);
void qtsubdivide(QuadTree *qt);
Point *qtquery(QuadTree *qt, Rectangle aabb, int *npoint);
void qtclear(QuadTree *qt);
int qtsearch(QuadTree *qt, Rectangle r, Quad ***res, int *nres);
