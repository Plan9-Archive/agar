#include <u.h>
#include <libc.h>
#include <draw.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include <quad.h>
#include "agar.h"

extern int debug;
#define DBG if(debug)

/*
 * player logic
 */

Player*
pmake(char *name, Point pos, int mass)
{
	Player *p;
	Cell *c;

	c = emalloc9p(sizeof(*c));

	c->p = pos;
	c->radius = mass2radius(mass);
	c->mass = mass;
	c->speed = 8.0;

	c->color = DRed;

	p = emalloc9p(sizeof(*p));

	p->name = estrdup9p(name);
	p->p = pos;
	p->target = pos;

	p->color = DRed;

	p->cells = c;
	p->ncells = 1;
	p->maxcells = 1;

	return p;
}

void
pmove(Player *pl)
{
	int i, dx, dy;
	double tx, ty, dist, deg;
	Point target,  p;
	Cell *c;

	for(i = 0; i < pl->ncells; i++){
		c = &pl->cells[i];
		target = pl->target;

		ptdist(c->p, pl->target, &dist, &deg);

		/* small distance from target - don't move */
		if(dist <= 5.0)
			continue;

		ty = target.y - c->p.y;	
		tx = target.x - c->p.x;

		DBG print("player %8s cell %02d pos %03d,%03d target %03d,%03d dist %5.2f deg %5.2f\n",
			pl->name, i, c->p.x, c->p.y, target.x, target.y, dist, deg);

		dy = (int)((ty / dist) * c->speed);
		dx = (int)((tx / dist) * c->speed);

		p = addpt(c->p, Pt(dx, dy));

		DBG print("dx %03d dy %03d newpos %03d,%03d\n", dx, dy, p.x, p.y);

		// TODO(mischief): pack cells

		c->p = p;
	}

	pl->p = pl->cells[0].p;
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
	return 4 + (int)(sqrt((double)mass) * 8.0);
}

/*
 * returns the distance between a and b if dist it not nil
 * returns the angle in radians between a and b if rad is not nil
 * needs a better name.
 */
void
ptdist(Point a, Point b, double *dist, double *rad)
{
	double dx, dy;

	dx = b.x - a.x;
	dy = b.y - a.y;

	if(dist)
		*dist = sqrt(dx*dx + dy*dy);

	if(rad)
		*rad = atan2(dy, dx);
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

static void
timerproc(void *v)
{
	void **vv;
	char *tag;
	Channel *nsc;
	int ms;
	vlong ns;

	vv = v;
	tag = vv[0];
	nsc = vv[1];
	ms = (int)(uintptr)vv[2];

	free(v);

	threadsetname("timerproc #%s %d", tag, ms);
	free(tag);

	for(;;){
		if(sleep(ms) < 0){
			chanclose(nsc);
			threadexits(nil);
		}

		ns = nsec();
		send(nsc, &ns);
	}
}

Channel*
timerchan(int ms, char *tag)
{
	void **v;
	Channel *ns;

	v = mallocz(sizeof(void*)*3, 1);

	ns = chancreate(sizeof(vlong), 0);

	v[0] = strdup(tag);
	v[1] = ns;
	v[2] = (void*)ms;

	proccreate(timerproc, v, 8192);
	return ns;
}
