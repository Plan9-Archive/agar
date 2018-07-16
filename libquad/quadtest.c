#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>
#include <geometry.h>
#include <avl.h>

#include "quad.h"
#include "agar.h"

int debug = 0;

typedef struct Blob Blob;
struct Blob
{
	Point3 pos;
	Point3 vel;

	int mass;
	char *name;
	int id;
};

Blob*
mkblob(Point p)
{
	Blob *b;
	static int id = 0;

	b = mallocz(sizeof(*b), 1);

	b->pos = pttopt3(p);

	b->vel.x = frand() * 100.0;
	b->vel.y = frand() * 100.0;

	b->mass = 5;

	switch(0){
	case 0:
		b->name = smprint("%P %d", p, b->mass);
		break;
	case 1:
		//b->Quad = qrect(canonrect(Rpt(p, addpt(p, Pt(nrand(100)+10, nrand(100)+10)))));
		//b->name = smprint("%R", b->r);
		break;
	}

	b->id = id++;

	return b;
}

Quad
blobtoquad(Blob *b)
{
	return qcircle(Pt(b->pos.x, b->pos.y), mass2radius(b->mass), b);
}

Image *red;
Image *blue;
Image *green;

Mousectl *mc;
Keyboardctl *kc;

Blob *blobs[1000];
int nblobs;

QuadTree *qt;

void
dq(Quad v, Image *color)
{
	switch(v.type){
	case QNCIRCLE:
		fillellipse(screen, addpt(screen->r.min, v.p), v.radius, v.radius, color, ZP);
		break;
	case QNRECTANGLE:
		draw(screen, rectaddpt(v.r, screen->r.min), color, nil, ZP);
		break;
	}	
}

void
drawquads(QuadTree *qt)
{
	int i;
	Rectangle r;
	QuadTree *child;
	Quad q;
	Blob *blob;

	//print("drawquads %R %d %d\n", qt->boundary, qt->nquads, qt->maxquads);

	/* draw box */
	r = qt->boundary;
	r = rectaddpt(r, screen->r.min);
	line(screen, r.min, Pt(r.max.x, r.min.y), 0, 0, 1, display->white, ZP);
	line(screen, Pt(r.max.x, r.min.y), r.max, 0, 0, 1, display->white, ZP);
	line(screen, r.max, Pt(r.min.x, r.max.y), 0, 0, 1, display->white, ZP);
	line(screen, Pt(r.min.x, r.max.y), r.min, 0, 0, 1, display->white, ZP);

	//print("== all ==\n");

	for(i = 0; i < qt->nquads; i++){
		blob = (Blob*)qt->quads[i].aux;
		q = blobtoquad(blob);
		dq(q, red);

		//print("%d %d %R\n", blob->id, blob->mass, quad2rect(q));
	}

	if(qt->zones != nil){
		for(i = 0; i < QTNZONE; i++){
			child = &qt->zones[i];
			drawquads(child);
		}
	}
}

void
drawquad(int new)
{
	int i;
	char buf[32];
	Point p;
	Quad q;
	Blob *blob;

	if(new && getwindow(display, Refmesg) < 0)
		sysfatal("can't reattach to window");

	//if(new)
		//originwindow(screen, Pt(0, 0), screen->r.min);

	draw(screen, screen->r, display->black, nil, ZP);

	qtclear(qt);

	for(i = 0; i < nblobs; i++){
		blob = blobs[i];
		q = blobtoquad(blob);
		//print("insert %d %d %R\n", blob->id, blob->mass, quad2rect(q));
		qtinsert(qt, q);
	}

	drawquads(qt);

	if(nblobs > 1){
		Quad *res = nil;
		int nres = 0;
		Rectangle search = quad2rect(blobtoquad(blobs[0]));

		qtsearch(qt, search, &res, &nres);

		//print("== in %R ==\n", search);

		for(i = 0; i < nres; i++){
			dq(res[i], blue);

			//print("%R\n", res[i]->r);
		}

		free(res);

		dq(blobtoquad(blobs[0]), green);
	}

	qtclear(qt);

	for(i = 0; i < nblobs; i++){
		snprint(buf, sizeof(buf), "%d", blobs[i]->id);
		q = blobtoquad(blobs[i]);
		p = addpt(q.p, screen->r.min);
		string(screen, p, display->white, ZP, font, buf);
	}

	flushimage(display, 1);
}

static void
step(double dt)
{
	int i;
	Blob *b;

	for(i = 0; i < nblobs; i++){
		b = blobs[i];

		b->pos = add3(b->pos, mul3(b->vel, dt));

		/* bounce back */
		if(b->pos.x < 0 || b->pos.x > 500)
			b->vel.x = -b->vel.x;
		if(b->pos.y < 0 || b->pos.y > 500)
			b->vel.y = -b->vel.y; 
	}
}

void
threadmain(int argc, char *argv[])
{
	int i, pause;
	Rune r;
	double dt;
	Channel *timerc;

	ARGBEGIN{
	}ARGEND

	pause = 0;

	srand(truerand());

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");

	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	timerc = timerchan(40, argv0);

	red = allocimagemix(display, DRed, DRed);
	blue = allocimagemix(display, DBlue, DBlue);
	green = allocimagemix(display, DGreen, DGreen);

	nblobs = 0;

	qt = qtmk(Rect(0, 0, 500, 500));

	blobs[nblobs] = mkblob(Pt(100, 100));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(100, 400));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(400, 100));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(400, 400));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(400, 250));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(250, 250));
	nblobs++;

	drawquad(0);

	enum { MOUSE, RESIZE, KEY, TIMER, END };
	Alt alts[] = {
	[MOUSE]		{ mc->c,		&mc->Mouse,	CHANRCV },
	[RESIZE]	{ mc->resizec,	nil,		CHANRCV },
	[KEY]		{ kc->c,		&r,			CHANRCV },
	[TIMER]		{ timerc,		&dt,		CHANRCV },
	[END]		{ nil,			nil,		CHANEND },
	};

	for(;;){
		switch(alt(alts)){
		case KEY:
			switch(r){
			case 'q':
			case 'Q':
			case 0x04:
			case 0x7F:
				goto out;
			case 'c':
				qtclear(qt);
				for(i = 0; i < nblobs; i++){
					free(blobs[i]->name);
					free(blobs[i]);
				}
				nblobs = 0;
				drawquad(0);
				break;
			case 'a':
				if(nblobs >= nelem(blobs))
					break;

				blobs[nblobs] = mkblob(Pt(nrand(400)+50, nrand(400)+50));
				nblobs++;
				drawquad(0);
				break;
			case ' ':
				pause = !pause;
				break;
			default:
				drawquad(0);
				break;
			}
			break;
		case MOUSE:
			break;
		case RESIZE:
			drawquad(1);
			break;
		case TIMER:
			if(!pause){
				step(dt);
			drawquad(0);
			}
			break;
		}
	}

out:
	threadexitsall(nil);
}
