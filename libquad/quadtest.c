#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>

#include "quad.h"

typedef struct Blob Blob;
struct Blob
{
	Quad;

	int mass;
	char *name;

	double xspeed;
	double yspeed;
};

Blob*
mkblob(Point p)
{
	Blob *b;

	b = mallocz(sizeof(*b), 1);

	b->xspeed = 1.0;
	b->yspeed = 1.0;

	switch(0){
	case 0:
		b->Quad = qcircle(p, nrand(30)+10);
		b->name = smprint("%P %d", b->p, b->radius);
		break;
	case 1:
		b->Quad = qrect(canonrect(Rpt(p, addpt(p, Pt(nrand(100)+10, nrand(100)+10)))));
		b->name = smprint("%R", b->r);
		break;
	}

	return b;
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
dq(Quad *v, Image *color)
{
	switch(v->type){
	case QNCIRCLE:
		fillellipse(screen, addpt(screen->r.min, v->p), v->radius, v->radius, color, ZP);
		break;
	case QNRECTANGLE:
		draw(screen, rectaddpt(v->r, screen->r.min), color, nil, ZP);
		break;
	}	
}

void
drawquads(QuadTree *qt)
{
	int i;
	Rectangle r;
	QuadTree *child;
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
		blob = (Blob*)qt->quads[i];
		dq(blob, red);

		//print("%R\n", blob->r);
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

	if(new && getwindow(display, Refmesg) < 0)
		sysfatal("can't reattach to window");

	//if(new)
		//originwindow(screen, Pt(0, 0), screen->r.min);

	draw(screen, screen->r, display->black, nil, ZP);

	qtclear(qt);

	for(i = 0; i < nblobs; i++){
		qtinsert(qt, blobs[i]);
	}

	drawquads(qt);

	if(nblobs > 1){

		Quad **res = nil;
		int nres = 0;
		Rectangle search = quad2rect(blobs[0]);

		qtsearch(qt, search, &res, &nres);

		//print("== in %R ==\n", search);

		for(i = 0; i < nres; i++){
			dq(res[i], blue);

			//print("%R\n", res[i]->r);
		}

		free(res);

		dq(blobs[0], green);
	}

	qtclear(qt);

	flushimage(display, 1);
}

static void
step(void)
{
	int i;
	Rectangle r;
	Blob *b;

	for(i = 0; i < nblobs; i++){
		b = blobs[i];

		// TODO(mischief): move and collide shit.
	}
}

void
threadmain(int argc, char *argv[])
{
	int i;
	Rune r;

	ARGBEGIN{
	}ARGEND

	srand(truerand());

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");

	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	red = allocimagemix(display, DRed, DRed);
	blue = allocimagemix(display, DBlue, DBlue);
	green = allocimagemix(display, DGreen, DGreen);

	nblobs = 0;

	qt = qtmk(Rect(0, 0, 1000, 1000));

	blobs[nblobs] = mkblob(Pt(475, 475));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(200, 200));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(400, 100));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(100, 400));
	nblobs++;
	blobs[nblobs] = mkblob(Pt(755, 755));
	nblobs++;

	drawquad(0);

	Alt alts[3];

	alts[0].op = CHANRCV;
	alts[0].c = kc->c;
	alts[0].v = &r;
	alts[1].op = CHANRCV;
	alts[1].c = mc->resizec;
	alts[1].v = nil;
	alts[2].op = CHANRCV;
	alts[2].c = mc->c;
	alts[2].v = &mc->Mouse;
	alts[3].op = CHANEND;

	for(;;){
		switch(alt(alts)){
		case 0:
			switch(r){
			case 'q':
			case 'Q':
			case 0x04:
			case 0x7F:
				threadexitsall(nil);
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

				blobs[nblobs] = mkblob(Pt(nrand(800)+50, nrand(900)+50));
				nblobs++;
				drawquad(0);
				break;
			default:
				drawquad(0);
				break;
			}
			break;
		case 1:
			drawquad(1);
			break;
		case 2:
			break;
		}
	}

	threadexitsall(nil);
}
