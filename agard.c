#include <u.h>
#include <libc.h>
#include <draw.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include <quad.h>
#include "agar.h"

int debug = 0;

#define DBG if(debug)

enum
{
	Qroot = 0,
	Qctl,
	Qevent,
};

typedef struct Client Client;
struct Client
{
	QLock;
	Ref;

	/* room name */
	char*		room;

	ulong		id;

	Player*		player;

	/* 0 write, 1 read */
	int		eventfd[2];
	Reqqueue*	eventrq;
};

void
clientsend(Client *c, char *msg)
{
	int n;
	char buf[512];
	n = snprint(buf, sizeof(buf), "%s\n", msg);

	DBG fprint(2, "clientsend %d %s", n, buf);
	write(c->eventfd[0], buf, n);
}

typedef struct Room Room;
struct Room
{
	QLock;
	Ref;

	char*	name;
	
	ulong*		clientids;
	int		nclients;
	int		maxclients;
	Intmap*		clients;
	Channel*	freec;

	Room*		next;
};

QLock	roomlock;
Room*	rooms;

/* r is locked */
static void
roombcast(Room *r, char *msg)
{
	int i;
	Client *cl;

	for(i = 0; i < r->nclients; i++){
		cl = lookupkey(r->clients, r->clientids[i]);
		clientsend(cl, msg);
	}
}

static void
roomcollide(Room *r, Client *cl)
{
	Client *o;
	QuadTree *qt;
	Rectangle search, rr;
	int i, j, nres = 0;
	Quad **res = nil;

	qt = qtmk(Rect(0, 0, 5000, 5000));

	search = quad2rect(&cl->player->cells[0]);

	/* insert all cells in the quadtree */
	for(i = 0; i < r->nclients; i++){
		o = lookupkey(r->clients, r->clientids[i]);
		for(j = 0; j < o->player->ncells; j++){
			qtinsert(qt, &o->player->cells[j]);
		}
	}

	qtsearch(qt, search, &res, &nres);

	if(nres > 1)
		fprint(2, "%d collisions with [%d,%d %d,%d]\n", nres, search.min.x, search.min.y, search.max.x, search.max.y);

	for(i = 0; i < nres; i++){
		/* skip self */
		if(res[i] == &cl->player->cells[0])
			continue;

		rr = quad2rect(res[i]);
		if(rectXrect(search, rr)){
			// TODO(mischief): do collision :/
			fprint(2, "%s hit [%d,%d %d,%d]\n", cl->player->name, rr.min.x, rr.min.y, rr.max.x, rr.max.y);
		}
	}

	free(res);

	qtclear(qt);
	free(qt);
}

static void
roomproc(void *v)
{
	int i;
	ulong id;
	char msg[512];
	Room *r;
	Channel *tick;
	Client *cl;

	r = v;

	threadsetname("roomproc #%s", r->name);

	tick = timerchan(250, r->name);

	Alt alts[] = {
	{ tick,		nil,	CHANRCV },
	{ r->freec,	&cl,	CHANRCV },
	{ nil,		nil,	CHANEND },
	};

	for(;;){
		switch(alt(alts)){
		case 0:

			qlock(r);

			if(r->nclients < 1){
				qunlock(r);
				break;
			}

			DBG fprint(2, "update #%s at %ld\n", r->name, time(nil));
			for(i = 0; i < r->nclients; i++){
				DBG fprint(2, "updating %lud\n", r->clientids[i]);
				cl = lookupkey(r->clients, r->clientids[i]);
				qlock(cl);
				pmove(cl->player);

				roomcollide(r, cl);

				Cell *cell = &cl->player->cells[0];

				/* id cell newx newy mass color isfood */
				snprint(msg, sizeof(msg), "update -i %lud -n %q -c %d -x %d -y %d -m %d -v 0x%lux -f %d",
					cl->id, cl->player->name, 0, cell->p.x, cell->p.y, cell->mass, cell->color, cell->isfood);

				qunlock(cl);

				roombcast(r, msg);

			}
			qunlock(r);
			break;
		case 1:
			DBG fprint(2, "roompart %q %lud %q\n", r->name, cl->id, cl->player->name);

			qlock(cl);
			id = cl->id;
			qunlock(cl);

			qlock(r);
			deletekey(r->clients, id);

			for(i = 0; i < r->nclients; i++){
				if(r->clientids[i] == id){
					memmove(&r->clientids[i], &r->clientids[i+1], (r->nclients-i-1)*sizeof(ulong));
					r->nclients--;
					break;
				}
			}

			snprint(msg, sizeof(msg), "remove -i %lud", id);
			roombcast(r, msg);

			qunlock(r);

			qlock(cl);

			free(cl->room);
			free(cl->player->cells);
			free(cl->player->name);
			free(cl->player);

			close(cl->eventfd[0]);
			close(cl->eventfd[1]);
			reqqueuefree(cl->eventrq);

			free(cl);
		
			break;
		default:
			sysfatal("roomproc alt: %r");
		}
#include <pool.h>
poolcheck(mainmem);
	}
}

Room*
roomget(char *name)
{
	Room *r;

	qlock(&roomlock);
	for(r = rooms; r != nil; r = r->next){
		if(strcmp(name, r->name) == 0){
			qlock(r);
			incref(r);
			qunlock(&roomlock);
			return r;
		}
	}

	r = emalloc9p(sizeof(*r));
	qlock(r);
	incref(r);
	r->name = estrdup9p(name);
	r->clients = allocmap(nil);
	r->freec = chancreate(sizeof(Client*), 10);
	r->next = rooms;

	proccreate(roomproc, r, 8192);

	rooms = r;
	qunlock(&roomlock);
	return r;
}

/* r locked */
static int
roomjoin(Room *r, Client *cl)
{
	if(lookupkey(r->clients, cl->id) != nil){
		return 0;
	}

	if(r->maxclients == 0 || (r->nclients == r->maxclients)){
		r->clientids = erealloc9p(r->clientids, (r->maxclients + 10) * sizeof(ulong));
		r->maxclients += 10;
	}

	r->clientids[r->nclients] = cl->id;
	r->nclients++;

	insertkey(r->clients, cl->id, cl);
	return 1;
}

int 		width	= 5000;
int		height	= 5000;

static Client*
s2c(Srv *s)
{
	return (Client*)s->aux;
}

void
fsstart(Srv *s)
{
	Client *c;

	DBG fprint(2, "client connecting: %s\n", s->addr);

	threadsetname("fsproc %s", s->addr);

	c = mallocz(sizeof(*c), 1);

	c->id = getid();

	pipe(c->eventfd);
	c->eventrq = reqqueuecreate();
	s->aux = c;
}

void
fsend(Srv *s)
{
	Room *r;
	Client *c;

	c = s2c(s);
	r = roomget(c->room);

	sendp(r->freec, c);

	qunlock(r);
}

void
fsattach(Req *r)
{
	char *room;
	Client *c;
	Room *rm;

	room = r->ifcall.aname;
	if(room && room[0]){
		respond(r, "invalid room");
		return;
	}

	c = s2c(r->srv);
	qlock(c);

	c->room = estrdup9p(room);
	c->player = pmake(r->ifcall.uname, Pt(50, 50), 10);

	rm = roomget(c->room);
	roomjoin(rm, c);
	decref(rm);
	qunlock(rm);

	qunlock(c);

	r->ofcall.qid = (Qid){Qroot, 0, QTDIR};
	r->fid->qid = r->ofcall.qid;

	respond(r, nil);
}

void
fsopen(Req *r)
{
	respond(r, nil);
}

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

static int
rootgen(int off, Dir *d, void*)
{
	if(off > 1)
		return -1;

	memset(d, 0, sizeof *d);
	d->atime = 1<<30;
	d->mtime = 1<<30;

	switch(off){
	case 0:
		d->name = estrdup9p("ctl");
		d->qid.path = Qctl;
		d->mode = 0444;
		break;
	case 1:
		d->name = estrdup9p("event");
		d->qid.path = Qevent;
		d->mode = 0222;	
	}

	d->qid.type = QTFILE;
	d->uid = estrdup9p("agar");
	d->gid = estrdup9p("agar");
	d->muid = estrdup9p("");
	return 0;
}
       
void
fsread(Req *r)
{
	Client *c;

	switch((int)r->fid->qid.path){
	case Qroot:
		dirread9p(r, rootgen, nil);
		break;
	case Qevent:
		c = s2c(r->srv);
		incref(c);
		reqqueuepush(c->eventrq, r, readevent);
		//readevent(r);
		return;
	default:
		respond(r, "fsread bug");
		return;
	}

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
	char msg[512];
	Client *c;

	c = s2c(r->srv);

	ARGBEGIN{
	default:
		respond(r, "sync: invalid argument");
		return;
	}ARGEND

	snprint(msg, sizeof(msg), "world %d %d", width, height);
	clientsend(c, msg);

	respond(r, nil);
}

void
cmdmouse(Req *r, int argc, char *argv[])
{
	Client *c;
	Point p;

	ARGBEGIN{
	default:
		respond(r, "mouse: invalid argument");
		return;
	}ARGEND

	if(argc != 2){
		respond(r, "mouse: not enough arguments");
		return;
	}

	p.x = strtol(argv[0], nil, 10);
	p.y = strtol(argv[1], nil, 10);

	if(p.x == 0 || p.y == 0){
		respond(r, "mouse: invalid argument");
		return;
	}

	DBG fprint(2, "mouse: %s -> %d, %d\n", r->srv->addr, p.x, p.y);

	c = s2c(r->srv);
	c->player->target = p;

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

void
fswrite(Req *r)
{
	Cmdbuf *cb;
	Cmdtab *ct;

	switch((uintptr)r->fid->qid.path){
	default:
		respond(r, "write bug");
		return;
	case Qctl:
		break;
	}

	/* Qctl write */
	cb = parsecmd(r->ifcall.data, r->ifcall.count);
	ct = lookupcmd(cb, cmdtab, nelem(cmdtab));
	if(ct == nil){
		respond(r, "bad command");
	} else {
		switch(ct->index){
		case CMping:
			cmdping(r, cb->nf, cb->f);
			break;
		case CMsync:
			cmdsync(r, cb->nf, cb->f);
			break;
		case CMmouse:
			cmdmouse(r, cb->nf, cb->f);
			break;
		default:
			respond(r, "command unimplemented");
			break;
		}
	}

	free(cb);
}

/*
 * since clients block in read of Qevent, they may want to cancel the request.
 * reqqueueflush takes care of respond.
 */
static void
fsflush(Req *r)
{
	Client *c;
	c = s2c(r->srv);
	reqqueueflush(c->eventrq, r->oldreq);
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	int q;
	Dir *d;
	
	d = &r->d;
	memset(d, 0, sizeof *d);
	d->qid = r->fid->qid;
	d->atime = d->mtime = 1<<30;
	q = r->fid->qid.path;
	switch(q){
	case Qroot:
		d->name = estrdup9p("/");
		d->mode = DMDIR|0555;
		break;
	case Qctl:
		d->name = estrdup9p("ctl");
		d->mode = 0444;
		break;
	case Qevent:
		d->name = estrdup9p("event");
		d->mode = 0222;
		break;
	default:
		sysfatal("oops");
		break;
	}

	d->uid = estrdup9p("agar");
	d->gid = estrdup9p("agar");
	d->muid = estrdup9p("");

	respond(r, nil);
}

char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	switch((int)fid->qid.path){
	case Qroot:
		if(strcmp(name, "ctl") == 0){
			fid->qid.path = Qctl;
			goto found;
		}
		if(strcmp(name, "event") == 0){
			fid->qid.path = Qevent;
found:
			fid->qid.vers = 0;
			fid->qid.type = 0;
			*qid = fid->qid;
			return nil;
		}
		break;
	}

	return "not found";
}

void
fsdestroyfid(Fid *fid)
{
	DBG fprint(2, "destroy fid %lud %hho %s %p\n", fid->fid, fid->omode, fid->uid, fid->aux);
}

void
fsdestroyreq(Req *r)
{
	DBG fprint(2, "destroy req %lud %p\n", r->tag, r->aux);
}

Srv fs = {
.start		= fsstart,
.end		= fsend,
.attach		= fsattach,
.open		= fsopen,
.read		= fsread,
.write		= fswrite,
.flush		= fsflush,
.stat		= fsstat,
.walk1		= fswalk1,
.destroyfid	= fsdestroyfid,
.destroyreq	= fsdestroyreq,
};

void
usage(void)
{
	fprint(2, "%s: [-D] [-d] [-F]\n", argv0);
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

#include <pool.h>
mainmem->flags = POOL_ANTAGONISM | POOL_PARANOIA | POOL_DEBUGGING;

	rfork(RFNAMEG|RFNOTEG);

	quotefmtinstall();

	threadlistensrv(&fs, "tcp!*!19000");
	threadexits(nil);
}
