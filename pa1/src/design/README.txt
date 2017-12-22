Group M
Group Member: Shimao Hu, Zhangjun Xu

Environment: all parts are tested on CSE lab

How to compile:(step 1-4 copy from the ASSIGNMENT 0)
*type the following command line in the ./src directrory:
	1. make -f Mymakefile

*Boost up os161
	1. cd ~/os161/root
*sys161 kernel
	run test (we declare an extern variable to share a global “switch flag” between all source files
		  which include it so that each test can recognize its own scheduling algorithm)
	1. Type “?t” command
	2. Type command “spt” to running part A test 
	3. Type command “dpt” to running part B test
	4. Type command “mpt” to running part C test

