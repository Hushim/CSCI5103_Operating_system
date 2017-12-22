#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <err.h>


int
main()
{
 

   //static char * args[4] = {(char *)"/testbin/add",(char*)"1", (char *) "2", NULL};
	//j = atoi(argv[2]);
    //
    //printf("Answer: %d\n", i+j);

   //execv("/testbin/add",args);


    const char *filename = "/testbin/add";
	static char *args[4];
	//pid_t pid;
	args[0] =(char *) "add";
	args[1] =(char *) "5";
	args[2] =(char *) "12";
	args[3] =(char *) NULL;
	//pid = fork();
    execv(filename, args);
	return 0;
}
