host=psi.millennium.berkeley.edu
path=/project/eecs/parlab/www/lithe/data/libgomp
parlaball_grpid=683

install:
	rsync -rlvzC --delete-after ./* $(host):$(path)/
	-ssh $(host) chgrp -fR $(parlaball_grpid) $(path)/\*
	-ssh $(host) chmod -fR 775 $(path)/\*
