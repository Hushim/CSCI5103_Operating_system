#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>

#define NTHREADS 9

static struct semaphore *tsem = NULL;
static struct semaphore *barrier = NULL;

static
void
init_semaphores(void)
{
	if (tsem==NULL) {
		tsem = sem_create("tsem", 0);
		if (tsem == NULL) {
			panic("schedulertest: tsem sem_create failed\n");
		}
	}

	if (barrier==NULL) {
		barrier = sem_create("barrier", 0);
		if (barrier == NULL) {
			panic("schedulertest: barrier sem_create failed\n");
		}
	}
}

static
void
loop(void *junk, unsigned long priority)
{


	volatile int i;
	int num_loops = 50;
	int ch ='0'+ priority;

	P(barrier);
	V(barrier);

	(void)junk;

	for (i=0; i<num_loops; i++) {
		putch(ch);

	}

	V(tsem);
}

/*****************************************************
 * copy from the thread.c
 * and did a change the way of the thread_create_priority
 * instant of call thread_create
 * ***************************************************/




static
void
runthreads()
{
	char name[16];
	int i, result;
	unsigned int priority = 9;

	for (i=1; i<=NTHREADS; i++) {
		snprintf(name, sizeof(name), "thread_%d", i);
		result = thread_fork_priority(name, priority, NULL, loop, NULL, priority);
		if (result) {
			panic("schedulertest: thread_fork failed %s)\n", strerror(result));
		}
		priority -= 1;
	}

	V(barrier);

	for (i=0; i<NTHREADS; i++) {
		P(tsem);
	}
}

int
schedulertest_spt(int nargs, char **args)
{
    switch_flag = STATIC_PRIORITY_FLAG;
	(void)nargs;
	(void)args;

	init_semaphores();
	kprintf("Starting scheduler test...\n");
	runthreads();
	kprintf("\nScheduler test done.\n");

	return 0;
}
