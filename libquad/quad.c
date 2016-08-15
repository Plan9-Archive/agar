#include <u.h>
#include <libc.h>
#include <draw.h>

#include "quad.h"

Quad
qcircle(Point p, int radius)
{
	Quad q;
	q.type = QNCIRCLE;
	q.p = p;
	q.radius = radius;
	return q;
}

Quad
qrect(Rectangle r)
{
	Quad q;
	q.type = QNRECTANGLE;
	q.r = r;
	return q;
}

Rectangle
quad2rect(Quad *v)
{
	Rectangle r;
	Point p;

	switch(v->type){
	case QNCIRCLE:
		p = Pt(v->radius, v->radius);
		r.min = subpt(v->p, p);
		r.max = addpt(v->p, p);
		break;
	case QNRECTANGLE:
		r = v->r;
		break;
	}

	return r;
}

QuadTree*
qtmk(Rectangle aabb)
{
	QuadTree *qt;

	qt = mallocz(sizeof(*qt), 1);
	if(qt == nil)
		sysfatal("malloc: %r");

	qt->boundary = aabb;

	return qt;
}

static Quad*
qtremove(QuadTree *qt, int i)
{
	Quad *q;

	q = qt->quads[i];

	memmove(&qt->quads[i], &qt->quads[i+1], (qt->nquads-i-1)*sizeof(Quad*));
	qt->nquads--;

	return q;
}

static Quad*
qtget(QuadTree *qt, int i)
{
	Quad *q;

	assert(qt->quads);
	q = qt->quads[i];
	assert(q);

	return q;
}

static int
qtadd(QuadTree *qt, Quad *q)
{
	int n;

	if(qt->quads == nil || qt->nquads == qt->maxquads){
		qt->quads = realloc(qt->quads, (qt->maxquads+10) * sizeof(Quad*));
		if(qt->quads == nil)
			sysfatal("realloc: %r");
		qt->maxquads += 10;
	}

	n = qt->nquads;
	qt->quads[n] = q;
	qt->nquads++;

	return n;
}

static int
qtgetsub(QuadTree *qt, Rectangle r)
{
	int vmid, hmid, fittop, fitbottom;

	vmid = qt->boundary.min.x + (Dx(qt->boundary)/2);
	hmid = qt->boundary.min.y + (Dy(qt->boundary)/2);

	/* fits in top */
	fittop = (r.min.y < hmid && r.min.y + Dy(r) < hmid);

	/* fits in bottom */
	fitbottom = (r.min.y > hmid);

	/* fits left */
	if(r.min.x < vmid && r.min.x + Dx(r) < vmid){
		if(fittop)
			return QTNW;
		if(fitbottom)
			return QTSW;
	/* fits right */
	} else if(r.min.x > vmid){
		if(fittop)
			return QTNE;
		if(fitbottom)
			return QTSE;
	}

	/* doesn't fit */
	return -1;
}

int
qtinsert(QuadTree *qt, Quad *v)
{
	int sub, i;
	Quad *q;

	//print("qtinsert %#p %P in %R\n", qt, xy, qt->boundary);

	if(qt->zones != nil){
		sub = qtgetsub(qt, quad2rect(v));
		if(sub != -1){
			if(qtinsert(&qt->zones[sub], v) != 1)
				goto failure;
			return 1;
		} else {
			return 1;
		}
	}

	qtadd(qt, v);

	if(qt->nquads > 4){
		if(qt->zones == nil)
			qtsubdivide(qt);

		i = 0;
		while(i < qt->nquads){
			q = qtget(qt, i);
			sub = qtgetsub(qt, quad2rect(q));
			if(sub != -1)
				qtinsert(&qt->zones[sub], qtremove(qt, i));
			else
				i++;
		}
	}

	return 1;

failure:
	print("qtinsert %#p failed\n", qt);

	abort();
	return 0;
}

void
qtsubdivide(QuadTree *qt)
{
	int w, h;
	Rectangle sub;

	qt->zones = mallocz(sizeof(QuadTree) * QTNZONE, 1);
	if(qt->zones == nil)
		sysfatal("malloc: %r");

	sub = qt->boundary;
	w = Dx(sub)/2;
	h = Dy(sub)/2;

	qt->zones[QTNW].boundary = Rect(sub.min.x, sub.min.y, sub.min.x + w, sub.min.y + h);
	qt->zones[QTNE].boundary = Rect(sub.min.x + w, sub.min.y, sub.max.x, sub.min.y + h);
	qt->zones[QTSW].boundary = Rect(sub.min.x, sub.min.y + h, sub.min.x + w, sub.max.y);
	qt->zones[QTSE].boundary = Rect(sub.min.x + w, sub.min.y + h, sub.max.x, sub.max.y);
}

void
qtclear(QuadTree *qt)
{
	int i;

	memset(qt->quads, 0, qt->maxquads * sizeof(Quad*));
	free(qt->quads);
	qt->quads = nil;
	qt->nquads = 0;
	qt->maxquads = 0;

	if(qt->zones == nil)
		return;

	for(i = 0; i < QTNZONE; i++){
		qtclear(&qt->zones[i]);
	}

	free(qt->zones);
	qt->zones = nil;
}

int
qtsearch(QuadTree *qt, Rectangle r, Quad ***res, int *nres)
{
	int i, n;
	Quad **v;

	//print("check %R X %R\n", qt->boundary, r);

	if(!rectXrect(qt->boundary, r))
		return 1;

	if(qt->nquads > 0){
		n = qt->nquads;

		*res = realloc(*res, (*nres + n) * sizeof(Quad*));
		if(*res == nil)
			sysfatal("realloc: %r");

		v = *res;

		for(i = 0; i < n; i++){
			//print("add %R\n", qt->quads[i]->r);
			v[*nres+i] = qt->quads[i];
		}

		*nres += n;
	}

	if(qt->zones != nil){
		for(i = 0; i < QTNZONE; i++){
			qtsearch(&qt->zones[i], r, res, nres);
		}
	}

	return 1;
}
