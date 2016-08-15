</$objtype/mkfile

TARG=agarc agard
LIB=libquad/libquad.$O.a
BIN=/$objtype/bin/games

</sys/src/cmd/mkmany

agar.$O: agar.h

$O.agarc: agarc.$O agar.$O
$O.agard: agard.$O agar.$O

$O.agartest: agar.$O

servertest:V:	$O.agard
	@{
		rfork ne
		rm -f /srv/agar
		unmount /n/agar >[2]/dev/null || status=''
		broke|grep agard|rc
		kill $O.agard|rc
		$O.agard -D -d
		#srv tcp!$sysname!19000 agar /n/agar
		#ls /n/agar
		#exec cat /n/agar/event &
		#cpid=$apid
		#echo ping > /n/agar/ctl
		#echo kill > /proc/$cpid/ctl
		#rm -f /srv/agar
	}

CFLAGS=$CFLAGS -Ilibquad

$LIB:V:
	cd libquad
	mk

clean nuke:V:
	@{ cd libquad; mk $target }
	rm -f *.[$OS] [$OS].* $TARG
