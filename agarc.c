#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <keyboard.h>
#include <mouse.h>

#include <quad.h>
#include "agar.h"

/* game state */

QLock		yay;
Rectangle	worldr;

Player*		me;
ulong		players[1000];
int		nplayers = 0;
Intmap*		playermap;

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
	CMupdate,
	CMremove,
};

Cmdtab cmdtab[] =
{
	CMworld,	"world",	3,
	CMupdate,	"update",	0,
	CMremove,	"remove",	3,
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
cmdupdate(int argc, char *argv[])
{
	char *a, *name;
	ulong id, color;
	int cell, x, y, mass, isfood;
	Player *pl;
	Cell *c;

	a = name = nil;
	id = color = 0;
	cell = x = y = mass = 0;

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
		if(!parseint(a, &x)){
			fprint(2, "bad update x %s\n", a);
			return;
		}
		break;
	case 'y':
		a = ARGF();
		if(!parseint(a, &y)){
			fprint(2, "bad update y %s\n", a);
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

	DBG fprint(2, "update args: %lud %d %d %d %d 0x%08lux, %d\n", id, cell, x, y, mass, color, isfood);

	if((pl = lookupkey(playermap, id)) == nil){
		fprint(2, "new player %lud\n", id);
		pl = pmake(name, Pt(x, y), mass);
		insertkey(playermap, id, pl);
		players[nplayers] = id;
		nplayers++;
	}

	c = &pl->cells[cell];
	c->p = Pt(x, y);
	c->radius = mass2radius(mass);
	c->mass = mass;
	c->color = color;
	c->isfood = isfood;


	// TODO(mischief): compute overall position in pl->p
	pl->p = c->p;
}

static void
cmdremove(int argc, char *argv[])
{
	int i;
	char *a;
	ulong id;
	Player *pl;

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
docmd(char *string)
{
	Cmdbuf *cb;
	Cmdtab *ct;

	cb = parsecmd(string, strlen(string));
	ct = lookupcmd(cb, cmdtab, nelem(cmdtab));
	if(ct == nil){
		fprint(2, "bad server command: %s\n", string);
		free(cb);
		return;
	}

	switch(ct->index){
	case CMworld:
		cmdworld(cb->nf, cb->f);
		break;
	case CMupdate:
		cmdupdate(cb->nf, cb->f);
		break;
	case CMremove:
		cmdremove(cb->nf, cb->f);
		break;
	}
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
drawplayer(Player *pl)
{
	int i;
	Point p;
	Cell *c;
	Image *color;


	for(i = 0; i < pl->ncells; i++){
		c = &pl->cells[i];
		p = addpt(screen->r.min, c->p);

		fprint(2, "draw cell %d,%d mass %d\n", c->p.x, c->p.y, c->mass);

		color = allocimagemix(display, c->color, c->color);
		fillellipse(screen, p, c->radius, c->radius, color, ZP);
		p.x -= stringwidth(font, pl->name) / 2;
		p.y -= font->height/2 + 1; 
		string(screen, p, display->white, ZP, font, pl->name);
		freeimage(color);
	}
}

void
redraw(int new)
{
	int i;
	Player *pl;

	if(new && getwindow(display, Refmesg) < 0)
		sysfatal("can't reattach to window");

	draw(screen, screen->r, display->black, nil, ZP);

	/* draw all players */
	for(i = 0; i < nplayers; i++){
		fprint(2, "try player %d\n", i);
		if(players[i] != 0){
			pl = lookupkey(playermap, players[i]);
			fprint(2, "draw player %lud '%s' %d %d\n", players[i], pl->name, pl->p.x, pl->p.y);
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

	enum { MOUSE, RESIZE, KEY, MSG, TIMER, END };
	Alt alts[] = {
	[MOUSE]		{ mc->c,	&mc->Mouse,	CHANRCV },
	[RESIZE]	{ mc->resizec,	nil,		CHANRCV },
	[KEY]		{ kc->c,	&r,		CHANRCV },
	[MSG]		{ eventc,	&msg,		CHANRCV },
	[TIMER]		{ timerc,	nil,		CHANRCV },
	[END]		{ nil,		nil,		CHANEND },
	};

	for(;;){
		switch(alt(alts)){
		case MOUSE:
			pt = subpt(mc->xy, screen->r.min);
			if(fprint(ctlfd, "mouse %d %d\n", pt.x, pt.y) < 0)
				fprint(2, "fprint ctl: %r\n");
			break;
		case RESIZE:
			redraw(1);
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
			redraw(0);
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
