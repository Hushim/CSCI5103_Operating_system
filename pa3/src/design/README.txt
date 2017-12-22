Group M
Group Member: Shimao Hu, Zhangjun Xu

Environment: all parts are tested on CSE lab

How to compile:(step 1-4 copy from the ASSIGNMENT 0)
*type the following command line in the ./src directrory:
	1. make -f mymakefile

*Boost up os161
	1. cd ~/os161/root
*sys161 kernel
run test:
To run the coremap test, you have to test
km1 
Km2
Km3[space] [value] <- a reasonable integer value 
 TLB test and page testing
p  /bin /{true, false}
p /testbin/{badcall, randcall, crash}(maybe delete later)
P /testbin/{matmult, sort, huge}
P /testbin/{forktesting, triplemat}(triplemat not pass)
P /testbin/parallelvm

 Sbrk test
P /testbin/sbrktest

For the file structure:
	We didn’t replace the dumbvm.c. We directly replace the content with the new implementation.


*****
***** we had implement page swap(in /out ) function, but we don't let those function get involved 
with os161, Because it is not that pretty consistent, sometime will TLB fault, However, please 
take look the arch/mis/vm/swap.c, if that implementation reasonable,please give us partial credit. 
(we will explain in design txt file  ) 
*****

