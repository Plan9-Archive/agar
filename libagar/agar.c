#include <u.h>
#include <libc.h>
#include <draw.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <geometry.h>
#include <avl.h>

#include <quad.h>
#include "agar.h"

extern int debug;
#define DBG if(debug)

/*
 * player logic
 */

Player*
pmake(char *name, Point3 pos, int mass)
{
	Player *p;
	Cell *c;

	c = emalloc9p(sizeof(*c));

	c->pos = pos;
	// TODO QUAD c->radius = mass2radius(mass);
	c->mass = mass;
	c->speed = 100.0;

	c->color = DRed;

	p = emalloc9p(sizeof(*p));

	p->name = estrdup9p(name);
	p->pos = pos;
	p->target = pos;

	p->color = DRed;

	p->cells = c;
	p->ncells = 1;
	p->maxcells = 1;

	return p;
}

void
pmove(Player *pl, double dt)
{
	int i;
	double dx, dy, tx, ty, dist, deg;
	Point3 target, p, unit;
	Cell *c;

	for(i = 0; i < pl->ncells; i++){
		c = &pl->cells[i];
		target = pl->target;

		ptdist(c->pos, pl->target, &dist, &deg);

		/* small distance from target - don't move */
		//if(closept3(c->pos, pl->target, 5.0))
			//continue;
		if(dist <= 10.0)
			continue;

		DBG print("player %8s cell %02d pos %5.2f,%5.2f target %5.2f,%5.2f dist %5.2f deg %5.2f\n",
			pl->name, i, c->pos.x, c->pos.y, target.x, target.y, dist, deg);

/*
		ty = target.y - c->p.y;	
		tx = target.x - c->p.x;
		dy = (ty / dist) * c->speed;
		dx = (tx / dist) * c->speed;

		p = (Point3){dx, dy, 0, 0};
		p = add3(c->pos, p);
*/
		//p = lerp3(c->pos, pl->target, 1 - pow(0.25, dt));
		p = sub3(pl->target, c->pos);
		unit = unit3(p);
		p = add3(c->pos, mul3(unit, c->speed * dt));

		DBG print("dx %5.2f dy %5.2f newpos %5.2f,%5.2f\n", dx, dy, p.x, p.y);

		// TODO(mischief): pack cells

		c->pos = p;
		// TODO QUAD c->p = Pt(p.x, p.y);
	}

	pl->pos = pl->cells[0].pos;
}

int
cellcmp(Avl *a, Avl *b)
{
	Cell *ca, *cb;

	ca = (Cell*)a;
	cb = (Cell*)b;
	if(ca->id == cb->id)
		return 0;

	return ca->id < cb->id ? -1 : 1;
}

/*
 * misc
 */

QLock	idlock;
ulong	idgen = 0;

ulong
getid(void)
{
	ulong id;

	qlock(&idlock);
	id = ++idgen;
	qunlock(&idlock);
	return id;
}

int
mass2radius(int mass)
{
	return 2 + (int)(sqrt((double)mass) * 3.0);
}

/*
 * returns the distance between a and b if dist it not nil
 * returns the angle in radians between a and b if rad is not nil
 * needs a better name.
 */
void
ptdist(Point3 a, Point3 b, double *dist, double *rad)
{
	double dx, dy;

	dx = b.x - a.x;
	dy = b.y - a.y;

	if(dist)
		*dist = sqrt(dx*dx + dy*dy);

	if(rad)
		*rad = atan2(dy, dx);
}

Point3
pttopt3(Point p)
{
	return (Point3){p.x, p.y, 0., 0.};
}

Point
pt3topt(Point3 p)
{
	return (Point){p.x, p.y};
}

Quad
celltoquad(Cell *c)
{
	Quad q;

	q = qcircle(Pt(c->pos.x, c->pos.y), mass2radius(c->mass), c);

	return q;
}

int
parseint(char *str, int *res)
{
	char c, *p;
	int r;

	if(str == nil){
		werrstr("empty number");
		return 0;
	}

	if(strlen(str) > 11){
		werrstr("number too long");
		return 0;
	}

	/* check for invalid beginning character */
	c = *str;
	if(c < '0' || c > '9' || (c == '-' && strlen(str) < 2)){
		werrstr("invalid character in number");
		return 0;
	}

	r = strtol(str, &p, 0);

	/* check for trailing garbage */
	if(*p != '\0'){
		werrstr("trailing garbage in number");
		return 0;
	}

	if(res != nil)
		*res = r;

	return 1;
}

int
parsedouble(char *str, double *res)
{
	char *p;
	double d;

	if(str == nil){
		werrstr("empty number");
		return 0;
	}

	d = strtod(str, &p);

	if(*p != '\0'){
		werrstr("trailing garbage in number");
		return 0;
	}

	if(res != nil)
		*res = d;

	return 1;
}

double
dtime(void)
{
	return nsec()/1000000000.0;
}

static void
timerproc(void *v)
{
	void **vv;
	char *tag;
	Channel *nsc;
	int ms;
	double dt, ct, nt;

	vv = v;
	tag = vv[0];
	nsc = vv[1];
	ms = (int)(uintptr)vv[2];

	free(v);

	threadsetname("timerproc #%s %d", tag, ms);
	free(tag);

	ct = dtime();

	for(;;){
		if(sleep(ms) < 0){
			chanclose(nsc);
			threadexits(nil);
		}

		nt = dtime();
		dt = nt - ct;
		ct = nt;

		send(nsc, &dt);
	}
}

Channel*
timerchan(int ms, char *tag)
{
	void **v;
	Channel *ns;

	v = mallocz(sizeof(void*)*3, 1);

	ns = chancreate(sizeof(double), 0);

	v[0] = strdup(tag);
	v[1] = ns;
	v[2] = (void*)ms;

	proccreate(timerproc, v, 8192);
	return ns;
}

/* zero Point3 */
Point3 ZP3;
