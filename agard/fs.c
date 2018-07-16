#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <geometry.h>
#include <fcall.h>
#include <9p.h>
#include <avl.h>

#include "quad.h"
#include "agar.h"

#include "dat.h"
#include "fns.h"

enum
{
	Qroot = 0,
	Qctl,
	Qevent,
};

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
	c->player = pmake(r->ifcall.uname, (Point3){50, 50, 0, 0}, 10);

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

static int
fsrootgen(int off, Dir *d, void*)
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
		dirread9p(r, fsrootgen, nil);
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

void
fswrite(Req *r)
{
	switch((uintptr)r->fid->qid.path){
	default:
		respond(r, "write bug");
		return;
	case Qctl:
		break;
	}

	/* Qctl write */
	docmd(r);
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

static char*
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
	//DBG fprint(2, "destroy fid %lud %hho %s %p\n", fid->fid, fid->omode, fid->uid, fid->aux);
}

static void
fsdestroyreq(Req *r)
{
	//DBG fprint(2, "destroy req %lud %p\n", r->tag, r->aux);
}

static Srv fs = {
.destroyfid	= fsdestroyfid,
.destroyreq	= fsdestroyreq,
.start		= fsstart,
.end		= fsend,
.attach		= fsattach,
/*.auth		= fsauth,*/
.open		= fsopen,
/*.create	= fscreate,*/
.read		= fsread,
.write		= fswrite,
/*.remove	= fsremove,*/
.flush		= fsflush,
.stat		= fsstat,
/*.wstat	= fswstat,*/
/*.walk		= fswalk,*/
/*.clone	= fsclone,*/
.walk1		= fswalk1,
};

void
startfs(char *addr)
{
	threadlistensrv(&fs, addr);
	threadexits(nil);
}
