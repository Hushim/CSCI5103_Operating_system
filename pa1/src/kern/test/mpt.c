// #include <types.h>
// #include <lib.h>
// #include <test.h>

// int hello(int nargs, char **args) {
//   (void)nargs;
//   (void)args;

//   kprintf("Hello world\n");

//   return 0;
// }

/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Scheduler test code.
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <current.h>
#include <spl.h>

#define NTHREADS 25

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

	//counter++;
	volatile int i;
	int num_loops = 20;
	int ch ='@'+ priority;

	//struct thread * tmp_thread = curcpu->c_runqueue.tl_head.tln_next->tln_self;
	//int t = '0' + tmp_thread->waiting_time;

	P(barrier);
	V(barrier);

	(void)junk;

	for (i=0; i<num_loops; i++) {
		int s = splhigh();
	 	kprintf("current thread is : %s  \t neweast priority is: %u \t initial priority is %d\n",curthread->t_name,curthread->priority,ch - '@');
		splx(s);
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
	unsigned int priority = 1;

	for (i=1; i<=NTHREADS; i++) {
		snprintf(name, sizeof(name), "thread_%d", i);
		result = thread_fork_priority(name, priority, NULL, loop, NULL, priority);
		if (result) {
			panic("schedulertest: thread_fork failed %s)\n", strerror(result));
		}
		priority += 1;
	}

	V(barrier);

	for (i=0; i<NTHREADS; i++) {
		P(tsem);
	}
}


int
schedulertest_mpt(int nargs, char **args)
{

    switch_flag = MULTI_LEVEL_FLAG;
	(void)nargs;
	(void)args;

	init_semaphores();
	kprintf("Starting scheduler test...\n");
	runthreads();
	kprintf("\nScheduler test done.\n");

	return 0;
}
