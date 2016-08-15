#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>

#include <quad.h>
#include "agar.h"

void
main(int argc, char *argv[])
{
	int i;

	ARGBEGIN{
	}ARGEND

	for(i = 0; i < 10; i++)
		print("getid() = %lud\n", getid());

	for(i = 0; i <= 100; i += 10)
		print("mass2radius(%d) = %d\n", i*10, mass2radius(i*10));

	exits(nil);
}
