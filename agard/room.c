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

QLock	roomlock;
Room*	rooms;

/* r is locked */
void
roombcast(Room *r, char *fmt, ...)
{
	int n, i;
	va_list arg;
	Client *cl;

	va_start(arg, fmt);
	n = vsnprint(r->genbuf, sizeof(r->genbuf), fmt, arg);
	va_end(arg);

	for(i = 0; i < r->nclients; i++){
		cl = lookupkey(r->clients, r->clientids[i]);
		clientsend(cl, r->genbuf);
	}
}

void
roomcollide(Room *r, Client *cl)
{
	int i, j, nres = 0;
	Rectangle search, rr;
	Cell *c, *coll;
	Client *o;
	QuadTree *qt;
	Quad q, *res = nil;

	qt = qtmk(Rect(0, 0, 5000, 5000));

	/* for now just worry about cell 0 */
	c = &cl->player->cells[0];
	q = celltoquad(c);
	search = quad2rect(q);

	fprint(2, "roomcollide %R\n", search);

	/* insert all cells in the quadtree */
	for(i = 0; i < r->nclients; i++){
		o = lookupkey(r->clients, r->clientids[i]);
		for(j = 0; j < o->player->ncells; j++){
			c = &o->player->cells[j];
			q = celltoquad(c);
			qtinsert(qt, q);
		}
	}

	/* insert food in quadtree */
	for(c = (Cell*)avlmin(&r->food); c != nil; c = (Cell*)avlnext(c)){
		q = celltoquad(c);
		qtinsert(qt, q);
	}

	qtsearch(qt, search, &res, &nres);

	if(nres > 1)
		fprint(2, "%d collisions with [%d,%d %d,%d]\n", nres, search.min.x, search.min.y, search.max.x, search.max.y);

	c = &cl->player->cells[0];
	q = celltoquad(c);

	for(i = 0; i < nres; i++){
		/* skip self */
		if(res[i].aux == c)
			continue;

		rr = quad2rect(res[i]);
		if(rectXrect(search, rr)){
			fprint(2, "%s hit [%d,%d %d,%d]\n", cl->player->name, rr.min.x, rr.min.y, rr.max.x, rr.max.y);

			coll = (Cell*) res[i].aux;
			fprint(2, "%s hit %lud %.0f,%.0f mass=%d isfood=%d\n", cl->player->name, coll->id,
				coll->pos.x, coll->pos.y, c->mass, coll->isfood);

			if(coll->isfood){
				assert(avldelete(&r->food, coll) != nil);
				r->nfood--;
				roombcast(r, "eat -i %lud", coll->id);
				c->mass += coll->mass;

				roombcast(r, "update -i %lud -c %d -m %d", cl->id, 0, c->mass);
			}
		}
	}

	free(res);

	qtclear(qt);
	free(qt);
}

/* r locked */
Cell*
mkfood(void)
{
	Cell *food;

	food = emalloc9p(sizeof(Cell));

	food->id = getid();
	food->pos.x = 20+nrand(100)*5;
	food->pos.y = 20+nrand(100)*5;
	food->mass = 3;
	food->speed = 0.0;
	food->color = DGreen;
	food->isfood = 1;

	return food;
}

void
roomproc(void *v)
{
	int i;
	ulong id;
	double dt, dist;
	Channel *tick;
	Room *r;
	Client *cl;
	Player *pl;
	Cell *cell;

	r = v;

	threadsetname("roomproc #%s", r->name);

	tick = timerchan(250, r->name);

	Alt alts[] = {
	{ tick,		&dt,	CHANRCV },
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

				pl = cl->player;

				pmove(pl, dt);

				roomcollide(r, cl);

				Cell *cell = &pl->cells[0];

				/* only broadcast if cell is moving */
				ptdist(cell->pos, pl->target, &dist, nil);
				if(dist > 5.0){
					/* id cell newx newy mass color isfood */

					roombcast(r, "update -i %lud -n %q -c %d -x %5.2f -y %5.2f "
						"-X %5.2f -Y %5.2f -m %d -v 0x%lux -f %d",
						cl->id, pl->name, 0, cell->pos.x, cell->pos.y,
						pl->target.x, pl->target.y, cell->mass, cell->color, cell->isfood);
				}

				qunlock(cl);


			}

			/* add a food */
			if(r->nfood < r->maxfood && nrand(5) == 0){
				r->nfood++;
				cell = mkfood();
				avlinsert(&r->food, cell);
				roombcast(r, "food -i %lud -x %5.2f -y %5.2f -m %d -c %#lux",
					cell->id, cell->pos.x, cell->pos.y, cell->mass, cell->color);
			}

			qunlock(r);
			break;
		case 1:
			qlock(cl);
			pl = cl->player;
			DBG fprint(2, "roompart %q %lud %q\n", r->name, cl->id, pl->name);
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

			roombcast(r, "remove -i %lud", id);

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

	r->maxfood = 10;
	r->nfood = 0;
	avlinit(&r->food, cellcmp);

	r->next = rooms;

	proccreate(roomproc, r, 8192);

	rooms = r;
	qunlock(&roomlock);
	return r;
}

/* r locked */
int
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
