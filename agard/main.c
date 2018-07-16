#include <u.h>
#include <libc.h>
#include <draw.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <geometry.h>
#include <avl.h>

#include "quad.h"
#include "agar.h"

#include "dat.h"
#include "fns.h"

int debug = 0;

void
clientsend(Client *c, char *fmt, ...)
{
	int n;
	va_list arg;

	va_start(arg, fmt);
	n = vsnprint(c->genbuf, sizeof(c->genbuf), fmt, arg);
	va_end(arg);

	if(strrchr(c->genbuf, '\n') == nil){
		strcat(c->genbuf, "\n");
		n += 1;
	}

	DBG fprint(2, "clientsend %d %s", n, c->genbuf);
	write(c->eventfd[0], c->genbuf, n);
}


int 		width	= 5000;
int		height	= 5000;

void
readevent(Req *r)
{
	int fd, n;
	char err[ERRMAX];
	Client *c;

	c = s2c(r->srv);
	fd = c->eventfd[1];
	n = read(fd, r->ofcall.data, r->ifcall.count);

	if(n == -1){
		rerrstr(err, sizeof(err));
		respond(r, err);
		return;
	}

	DBG fprint(2, "readevent %d %.*s", n, n, r->ofcall.data);

	r->ofcall.count = n;
	respond(r, nil);
}

enum
{
	CMping,
	CMsync,
	CMmouse,
};

Cmdtab cmdtab[] =
{
	CMping,		"ping",		1,
	CMsync,		"sync",		1,
	CMmouse,	"mouse",	3,
};

void
cmdping(Req *r, int argc, char *argv[])
{
	Client *c;

	c = s2c(r->srv);

	ARGBEGIN{
	default:
		respond(r, "ping: invalid argument");
		return;
	}ARGEND

	clientsend(c, "pong");

	respond(r, nil);
}

void
cmdsync(Req *r, int argc, char *argv[])
{
	Client *c;
	Cell *cell;
	Room *room;

	c = s2c(r->srv);

	ARGBEGIN{
	default:
		respond(r, "sync: invalid argument");
		return;
	}ARGEND

	clientsend(c, "world %d %d", width, height);
	clientsend(c, "id %d", c->id);

	room = roomget(c->room);

	for(cell = (Cell*)avlmin(&room->food); cell != nil; cell = (Cell*)avlnext(cell)){
		clientsend(c, "food -i %lud -x %5.2f -y %5.2f -m %d -c %#lux",
			cell->id, cell->pos.x, cell->pos.y, cell->mass, cell->color);
	}

	decref(room);
	qunlock(room);

	respond(r, nil);
}

void
cmdmouse(Req *r, int argc, char *argv[])
{
	Client *c;
	Point3 p;

	ARGBEGIN{
	default:
		respond(r, "mouse: invalid argument");
		return;
	}ARGEND

	if(argc != 2){
		respond(r, "mouse: not enough arguments");
		return;
	}

	p.x = strtod(argv[0], nil);
	p.y = strtod(argv[1], nil);

	if(p.x == 0.0 || p.y == 0.0){
		respond(r, "mouse: invalid argument");
		return;
	}

	DBG fprint(2, "mouse: %s -> %5.2f,%5.2f\n", r->srv->addr, p.x, p.y);

	c = s2c(r->srv);
	c->player->target = p;

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static void (*cmdfns[])(Req*, int, char*[]) = {
[CMping]	cmdping,
[CMsync]	cmdsync,
[CMmouse]	cmdmouse,
};

void
docmd(Req *r)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	void (*cmdfn)(Req*, int, char*[]);

	cb = parsecmd(r->ifcall.data, r->ifcall.count);
	ct = lookupcmd(cb, cmdtab, nelem(cmdtab));
	if(ct == nil){
		respond(r, "bad command");
		
	} else {
		cmdfn = cmdfns[ct->index];
		cmdfn(r, cb->nf, cb->f);
	}

	free(cb);
}

void
usage(void)
{
	fprint(2, "%s: [-D] [-d]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	quotefmtinstall();
	fmtinstall('P', Pfmt);
	fmtinstall('R', Rfmt);

	rfork(RFNAMEG|RFNOTEG);

	startfs("tcp!*!19000");
}
