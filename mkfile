</$objtype/mkfile

LIBS=\
	libquad\
	libagar

CMDS=\
	agarc\
	agard

default:V:	all

all install clean nuke:VQ:
	date
	for (i in $LIBS $CMDS) @{
		echo $i
		cd $i
		mk $target
	}
