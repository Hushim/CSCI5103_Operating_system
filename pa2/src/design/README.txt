Group M
Group Member: Shimao Hu, Zhangjun Xu

Environment: all parts are tested on CSE lab

How to compile:(step 1-4 copy from the ASSIGNMENT 0)
*type the following command line in the ./src directrory:
	1. make -f mymakefile

*Boost up os161
	1. cd ~/os161/root
*sys161 kernel(run test)

	getpid()
		In side of sys161 kernel type “p /testbin/gitpidtest”

	fork()
		In side of sys161 kernel type “p /testbin/forktest”

	execv()
		In side of sys161 kernel type “p /testbin/execvtest”

	waitpid()
		In side of sys161 kernel type “p /testbin/forktest”

_	exit()
		In side of sys161 kernel type “p /testbin/forktest”


the file in testbin/forktest/ include fork, waitpid, and _exit();


