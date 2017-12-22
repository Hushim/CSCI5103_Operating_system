/*
 * Copyright (c) 2013
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
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <thread.h>
#include <limits.h>
#include <synch.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

int t_pid_count = 0;
pid_t t_pid_list[PID_MAX] = {0};
/*
 * Create a proc structure.
 */
int list_init(int * list) {
	int i = 0;
	for(i = 0; i < PID_MAX; i++) {
		list[i] = 0;
	}

	return 0;
}

pid_t allocate_pid() {

	//spinlock_acquire(&pid_list_lock);
	if(t_pid_count == 0) {
		if(list_init(t_pid_list) != 0) {
			panic("Failed to initialize list\n");

		//	spinlock_release(&pid_list_lock);
			return -1;
		}
	}

	if(t_pid_count == PID_MAX) {
		panic("Reached max number\n");

	//	spinlock_release(&pid_list_lock);
		return -1;
	}

	int i = 0;
	for(i = 0; i < PID_MAX; i++) {

		if(!t_pid_list[i]) {
			t_pid_list[i] = 1;

		//	spinlock_release(&pid_list_lock);
		//	spinlock_acquire(&count_lock);
			++t_pid_count;
		//	spinlock_release(&count_lock);
			return ++i;
		}
	}

	//spinlock_release(&pid_list_lock);
	return -1;
}

int remove_pid(pid_t cur_pid) {
	if(cur_pid < 1 || cur_pid > PID_MAX) {
		panic("Pid is out of range\n");
		return -1;
	} else if(!t_pid_list[cur_pid-1]) {
		panic("Pid doesn't exist\n");
		return -1;
	} else {
		t_pid_list[cur_pid-1] = 0;
		
	//	spinlock_acquire(&count_lock);
		--t_pid_count;
	//	spinlock_release(&count_lock);

		return 0;
	}
}



static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

    char sem_name[16];

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	proc->t_ppid = 0;

	proc->t_pid = allocate_pid();

	//proc->child_proc_num = 0;
    proc->exit_val = 0;

    proc->exit_state = 0;

    proc->child_proc_list = *array_create();

	array_init(&proc->child_proc_list);

	proc->parent_proc = NULL;

    snprintf(sem_name, sizeof(sem_name), "sem_%d", proc->t_pid);
    proc->wait_child_sem = sem_create(sem_name, 0);
    if(proc->wait_child_sem == NULL) {
      panic("Failed to create sem");
    }


	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

    //clean up the child process list
    int i = 0;
    for(i = array_num(&proc->child_proc_list)-1;i >= 0; i--) {
      array_remove(&proc->child_proc_list, i);
    }

    //cleanup child process array
    array_cleanup(&proc->child_proc_list);

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

    remove_pid(proc->t_pid);

    sem_destroy(proc->wait_child_sem);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

//int get_child_proc_list_num(struct proc * process) {
//	return process->child_proc_num;
//}
/*
int  add_child_porc(struct child_proc_list * list, struct proc * child_proc) {
	if(list == NULL){
		list = kmalloc(sizeof(struct child_proc_list));

		if(list == NULL) {
			panic("Failde to malloc child_proc_listt\n");
			return -1;
		}

		list->proc = child_proc;
		list->next = NULL;

	}else{

		struct proc * new_proc = kmalloc(sizeof(struct proc));
		proc->proc = child_proc;
		proc->next = NULL;

		struct child_proc_list * p = list;

		while(p->next!=NULL) {
			p = p->next;
		}

		p->next = new_proc;
	}

	return 0;
}
int remove_child_proc(struct child_proc_list ** list, struct proc * child_proc) {

	struct child_proc_list * p = *list;

	if(p == NULL) {
		panic("the child proc list is empty\n");
		return -1;
	}

	if(p->proc->t_pid == child_proc->t_pid){
		(*list) = (*list)->next;
		return 0;
	}
	while(p->next!=NULL){
		if(p->next->proc->t_pid == child_proc->t_pid) {
			p->next = p->next->next;
			return 0;
		}
	}
	return -1;
    
}*/
