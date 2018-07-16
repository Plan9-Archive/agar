#define s2c(s) ((Client*)s->aux)

/* main.c */
void clientsend(Client *c, char *fmt, ...);
void readevent(Req *r);
void docmd(Req *r);

/* fs.c */
void startfs(char *addr);

/* room.c */
void roombcast(Room *r, char *fmt, ...);
void roomcollide(Room *r, Client *cl);
void roommkfood(Room *r);
void roomproc(void *v);
Room* roomget(char *name);
int roomjoin(Room *r, Client *cl);
