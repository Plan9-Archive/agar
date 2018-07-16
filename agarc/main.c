#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <keyboard.h>
#include <mouse.h>
#include <geometry.h>
#include <avl.h>

#include <quad.h>
#include "agar.h"

/* game state */

Rectangle	worldr;

Player*		me = nil;
ulong 		meid = 0;
ulong		players[1000];
int			nplayers = 0;
Intmap*		playermap;

Avltree*	food;

/* ui goo */

Mousectl*	mc;
Keyboardctl*	kc;

Channel*	eventc;

int		srvfd, ctlfd, eventfd;

int		debug = 0;
#define DBG if(debug)

static void
eventproc(void*)
{
	Biobuf *bio;
	char *line;

	threadsetname("eventproc");

	bio = Bfdopen(eventfd, OREAD);

	while((line = Brdstr(bio, '\n', 1)) != nil){
		sendp(eventc, line);
	}

	Bterm(bio);

	postnote(PNGROUP, getpid(), "die yankee pig dog");
	sysfatal("event read error: %r");
}

enum
{
	CMworld = 0,
	CMid,
	CMupdate,
	CMremove,
	CMfood,
	CMeat,
};

Cmdtab cmdtab[] =
{
	CMworld,	"world",	3,
	CMid,		"id",		2,
	CMupdate,	"update",	0,
	CMremove,	"remove",	3,
	CMfood,		"food",		0,
	CMeat,		"eat",		0,
};

static void
cmdworld(int argc, char *argv[])
{
	int w, h;

	ARGBEGIN{
	}ARGEND

	w = strtol(argv[0], nil, 10);
	h = strtol(argv[1], nil, 10);
	worldr = Rect(0, 0, w, h);
}

static void
cmdid(int argc, char *argv[])
{
	ARGBEGIN{
	}ARGEND

	if(argc != 1)
		sysfatal("no id received");

	meid = strtoul(argv[0], nil, 10);
}

static void
cmdupdate(int argc, char *argv[])
{
	char *a, *name;
	ulong id, color;
	int cell, mass, isfood;
	Point3 pt, target;
	Player *pl;
	Cell *c;

	name = nil;
	id = color = 0;
	cell  = mass = 0;
	pt = ZP3;
	target = pt;

	ARGBEGIN{
	case 'i':
		a = ARGF();
		id = strtoul(a, nil, 0);
		break;
	case 'n':
		name = ARGF();
		break;
	case 'c':
		a = ARGF();
		if(!parseint(a, &cell)){
			fprint(2, "bad update cell %s\n", a);
			return;
		}
		break;
	case 'x':
		a = ARGF();
		if(!parsedouble(a, &pt.x)){
			fprint(2, "bad update x %s\n", a);
			return;
		}
		break;
	case 'y':
		a = ARGF();
		if(!parsedouble(a, &pt.y)){
			fprint(2, "bad update y %s\n", a);
			return;
		}
		break;
	case 'X':
		a = ARGF();
		if(!parsedouble(a, &target.x)){
			fprint(2, "bad update X %s\n", a);
			return;
		}
		break;
	case 'Y':
		a = ARGF();
		if(!parsedouble(a, &target.y)){
			fprint(2, "bad update Y %s\n", a);
			return;
		}
		break;
	case 'm':
		a = ARGF();
		if(!parseint(a, &mass)){
			fprint(2, "bad update mass %s\n", a);
			return;
		}
		break;
	case 'v':
		a = ARGF();
		color = strtoul(a, nil, 0);
		break;
	case 'f':
		a = ARGF();
		if(!parseint(a, &isfood)){
			fprint(2, "bad update isfood %s\n", a);
			return;
		}
		break;
	}ARGEND

	DBG fprint(2, "update args: %lud %d %5.2f,%5.2f %5.2f,%5.2f %d 0x%08lux, %d\n",
		id, cell, pt.x, pt.y, target.x, target.y,mass, color, isfood);

	if((pl = lookupkey(playermap, id)) == nil){
		fprint(2, "new player %lud\n", id);
		pl = pmake(name, pt, mass);
		insertkey(playermap, id, pl);
		players[nplayers] = id;
		nplayers++;

		if(id == meid)
			me = pl;
	}

	c = &pl->cells[cell];

	if(!eqpt3(pt,ZP3))
		c->pos = pt;

	if(mass != 0)
		c->mass = mass;

	if(color != 0)
		c->color = color;

	c->isfood = isfood;

	// TODO(mischief): compute overall position in pl->pos
	if(!eqpt3(pt, ZP3))
		pl->pos = pt;
	if(!eqpt3(target, ZP3))
		pl->target = target;
}

static void
cmdremove(int argc, char *argv[])
{
	int i;
	char *a;
	ulong id;
	Player *pl;

	id = 0;

	ARGBEGIN{
	case 'i':
		a = ARGF();
		id = strtoul(a, nil, 0);
		break;
	}ARGEND

	if((pl = deletekey(playermap, id)) != nil){
		DBG fprint(2, "remove %lud\n", id);
		for(i = 0; i < nplayers; i++){
			if(players[i] == id){
				memmove(&players[i], &players[i+1], (nplayers-i-1)*sizeof(ulong));
				nplayers--;
			}
		}

		free(pl->name);
		free(pl->cells);
		free(pl);
	}
}

static void
cmdfood(int argc, char *argv[])
{
	char *a;
	ulong id, color;
	Point3 pos;
	int mass;
	Cell *c;

	a = nil;
	id = color = 0;
	pos.x = pos.y = 0.;
	mass = 0;

	USED(a);

	ARGBEGIN{
	case 'i':
		a = ARGF();
		id = strtoul(a, nil, 0);
		break;
	case 'x':
		a = ARGF();
		if(!parsedouble(a, &pos.x)){
			fprint(2, "bad food x %s\n", a);
			return;
		}
		break;
	case 'y':
		a = ARGF();
		if(!parsedouble(a, &pos.y)){
			fprint(2, "bad food y %s\n", a);
			return;
		}
		break;
	case 'm':
		a = ARGF();
		if(!parseint(a, &mass)){
			fprint(2, "bad update mass %s\n", a);
			return;
		}
		break;
	case 'c':
		a = ARGF();
		color = strtoul(a, nil, 0);
		break;
	}ARGEND

	c = mallocz(sizeof(Cell), 1);
	c->id = id;
	c->pos = pos;
	// TODO QUAD c->radius = mass2radius(mass);
	c->mass = mass;
	c->color = color;
	c->isfood = 1;
	avlinsert(food, c);
}

static void
cmdeat(int argc, char *argv[])
{
	char *a;
	Cell *c, l;

	ARGBEGIN{
	case 'i':
		a = ARGF();
		l.id = strtoul(a, nil, 0);
		break;
	}ARGEND

	c = (Cell*)avldelete(food, &l);
	if(c == nil)
		sysfatal("eat command with missing cell %lud", l.id);

	free(c);
}

static void (*cmdfns[])(int, char*[]) = {
[CMworld]	cmdworld,
[CMid]		cmdid,
[CMupdate]	cmdupdate,
[CMremove]	cmdremove,
[CMfood]	cmdfood,
[CMeat]		cmdeat,
};

static void
docmd(char *string)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	void (*cmdfn)(int, char*[]);

	cb = parsecmd(string, strlen(string));
	ct = lookupcmd(cb, cmdtab, nelem(cmdtab));
	if(ct == nil){
		fprint(2, "bad server command: %s\n", string);
		free(cb);
		return;
	}

	cmdfn = cmdfns[ct->index];

	cmdfn(cb->nf, cb->f);
}

QLock	colorlock;
Intmap*	colormap;

Image*
colorget(ulong color)
{
	Image* img;

	qlock(&colorlock);
	if(colormap == nil)
		colormap = allocmap(nil);
	qunlock(&colorlock);

	if((img = lookupkey(colormap, color)) == nil){
		img = allocimagemix(display, color, color);
		insertkey(colormap, color, img);
	}

	return img;
}

void
drawcell(Cell *c, char *label)
{
	int radius;
	Point p3, p;
	Image *color;

	p3 = pt3topt(c->pos);
	p = addpt(screen->r.min, p3);
	color = colorget(c->color);

	DBG fprint(2, "drawcell %P label=%s mass=%d color=%#lux\n",
		p3, label, c->mass, c->color);

	radius = mass2radius(c->mass);

	fillellipse(screen, p, radius, radius, color, ZP);
	if(label != nil){
		p.x -= stringwidth(font, label) / 2;
		p.y -= font->height/2 + 1;
		string(screen, p, display->white, ZP, font, label);
	}
}

void
drawplayer(Player *pl)
{
	int i;
	Cell *c;

	for(i = 0; i < pl->ncells; i++){
		c = &pl->cells[i];

		drawcell(c, pl->name);
	}
}

const double lfactor = 0.25;

/* move all players... */
void
plerp(double dt)
{
	int i, j;
	double dist;
	Point3 p;
	Player *pl;
	Cell *c;

	for(i = 0; i < nplayers; i++){
		if(players[i] == 0)
			continue;

		pl = lookupkey(playermap, players[i]);
		for(j = 0; j < pl->ncells; j++){
			c = &pl->cells[j];
			ptdist(c->pos, pl->target, &dist, nil);
			if(dist <= 5.0)
				continue;

			p = sub3(pl->target, c->pos);
			p = unit3(p);
			c->pos = add3(c->pos, mul3(p, c->speed * dt));
		}
	}
}

void
redraw(int new)
{
	int i;
	Player *pl;
	Cell *c;

	if(new && getwindow(display, Refmesg) < 0)
		sysfatal("can't reattach to window");

	draw(screen, screen->r, display->black, nil, ZP);

	/* draw all food */
	for(c = (Cell*)avlmin(food); c != nil; c = (Cell*)avlnext(c)){
		drawcell(c, nil);
	}

	/* draw all players */
	for(i = 0; i < nplayers; i++){
		fprint(2, "try player %d\n", i);
		if(players[i] != 0){
			pl = lookupkey(playermap, players[i]);
			fprint(2, "draw player %lud '%s' %5.2f %5.2f\n", players[i], pl->name, pl->pos.x, pl->pos.y);
			drawplayer(pl);
		}
	}

	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "usage: %s [-c server]\n", argv0);
	threadexitsall(nil);
}

void
threadmain(int argc, char *argv[])
{
	Channel *timerc;
	char *server = "$sys";

	ARGBEGIN{
	case 'c':
		server = EARGF(usage());
		break;
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	rfork(RFNOTEG);

	srvfd = dial(netmkaddr(server, "tcp", "19000"), nil, nil, nil);
	if(srvfd < 0)
		sysfatal("dial: %r");

	if(mount(srvfd, -1, "/n/agar", MREPL, "") < 0)
		sysfatal("mount: %r");

	ctlfd = open("/n/agar/ctl", OWRITE);
	if(ctlfd < 0)
		sysfatal("open ctl: %r");

	eventfd = open("/n/agar/event", OREAD);
	if(eventfd < 0)
		sysfatal("open event: %r");

	playermap = allocmap(nil);
	food = avlcreate(cellcmp);
	if(food == nil)
		sysfatal("avlcreate: %r");

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");

	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	timerc = timerchan(25, server);
	eventc = chancreate(sizeof(char*), 100);
	proccreate(eventproc, nil, 8192);

	fprint(ctlfd, "sync\n");

	Rune r;
	char *msg;
	Point pt;
	int resized;
	double t, dt;

	t = 0.0;

	enum { MOUSE, RESIZE, KEY, MSG, TIMER, END };
	Alt alts[] = {
	[MOUSE]		{ mc->c,	&mc->Mouse,	CHANRCV },
	[RESIZE]	{ mc->resizec,	nil,		CHANRCV },
	[KEY]		{ kc->c,	&r,		CHANRCV },
	[MSG]		{ eventc,	&msg,		CHANRCV },
	[TIMER]		{ timerc,	&dt,		CHANRCV },
	[END]		{ nil,		nil,		CHANEND },
	};

	for(;;){
		switch(alt(alts)){
		case MOUSE:
			pt = subpt(mc->xy, screen->r.min);
			// TODO: fix me
/*
			if(me != nil)
				me->target = pttopt3(pt);
*/

			if(fprint(ctlfd, "mouse %5.2f %5.2f\n", (double)pt.x, (double)pt.y) < 0)
				fprint(2, "fprint ctl: %r\n");
			break;
		case RESIZE:
			resized = 1;
			goto redraw;
			break;
		case KEY:
			switch(r){
			case 'q':
			case 0x7f:
				close(ctlfd);
				close(eventfd);
				unmount(nil, "/n/agar");
				close(srvfd);
				goto quit;
			}
			break;
		case TIMER:
			resized = 0;
redraw:
			t += dt;
			plerp(dt);
			redraw(resized);
			break;
		case MSG:
			/* server event */
			fprint(2, "server event: %s\n", msg);
			docmd(msg);
			free(msg);
			break;
		}
	}

quit:
	postnote(PNGROUP, getpid(), "die yankee pig dog");
	threadexitsall(nil);
}
