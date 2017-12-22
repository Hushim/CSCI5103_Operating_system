#include <types.h>
#include <syscall.h>
#include <test.h>
#include <thread.h>
#include <lib.h>
#include <synch.h>
#include <current.h>
#include <spl.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>
#include <cpu.h>
#include <spl.h>
#include <spinlock.h>
#include <wchan.h>
#include <threadlist.h>
#include <threadprivate.h>
#include <proc.h>
#include <addrspace.h>
#include <mainbus.h>
#include <vnode.h>
#include <array.h>
#include <syscall.h>
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <vm.h>
#include <vfs.h>
#include <copyinout.h>



int sys_fork(struct trapframe * tf, pid_t *retval) {
	//create new process
  int s = splhigh();
	struct  proc * new_proc = proc_create_runprogram(curproc->p_name);

	struct trapframe * child_tf = kmalloc(sizeof(struct trapframe));
	if(child_tf == NULL) {
		return -1;
        splx(s);
	}

	memcpy(child_tf, tf, sizeof(struct trapframe));

	struct addrspace * child_as = kmalloc(sizeof(struct addrspace));
	if(child_as == NULL) {
		kfree(child_tf);
        splx(s);
		return -1;
	}

	int result = as_copy(curthread->t_proc->p_addrspace, &child_as);

	if(result) {

		kfree(child_tf);
		kfree(child_as);
        splx(s);
		panic("Failed to copy addrspace\n");
        return -1;
	}

	//assign parent process and new address
	new_proc->parent_proc = curproc;
	new_proc->p_addrspace = child_as;
    new_proc->t_ppid = curproc->t_pid;

	spinlock_acquire(&curproc->p_lock);
	array_add(&curproc->child_proc_list, new_proc, NULL);
	spinlock_release(&curproc->p_lock);

      
	int err = thread_fork("new_child", new_proc,
		enter_forked_process, (void *)child_tf, 0);

	if(err) {
		kfree(child_tf);
		kfree(child_as);
        splx(s);
		return -1;
	}

	*retval = new_proc->t_pid;
    splx(s);

	return 0;




}

int sys__exit(int code) {

  struct proc * tmp_proc = curthread->t_proc;
  if(tmp_proc == NULL){
    panic("current process is NULL\n");
    return -1;
  }
  int s = splhigh();
  
  //clean up child process array
  int i = 0;
  for (i = array_num(&tmp_proc->child_proc_list)-1; i >= 0 ; i--) {
    array_remove(&tmp_proc->child_proc_list, i);                                  
  }

  //deacticate current address space
  as_deactivate();
  struct addrspace * as = proc_setas(NULL);
  as_destroy(as);

  //encoding exit code
  tmp_proc->exit_val = _MKWAIT_EXIT(code);
  tmp_proc->exit_state = 1;

  //collect pid back
  remove_pid(tmp_proc->t_pid);
  
  (void)code;

  //signal the parent waiting for current process
  V(tmp_proc->wait_child_sem);
  thread_exit();
  splx(s);
  return 0;
}

int sys_waitpid(pid_t child_pid, int * status, int flag, int * retval) {

  (void)flag;
  unsigned int i = 0;
  struct proc * cur_proc = curproc;

  //waited pid is not available
  if(child_pid>0 &&t_pid_list[child_pid-1] == 0){

    return 0;
  }

  //waited pid eqauls current pid
  if(child_pid == cur_proc->t_pid){
    return 0;
  }

  //other case:
  int is_not_child = 1;

  struct proc * waited_proc;
  for(i = 0; i < array_num(&cur_proc->child_proc_list); i++) {
    waited_proc = array_get(&cur_proc->child_proc_list, i);
    if(child_pid == waited_proc->t_pid) {
      is_not_child = 0;
      break;
    }
  }

  //waited pid is not current proc's chld
  if(is_not_child) {
   // panic("This is not cur process's child\n ");
    return 0;
  }

  //wait until the child exit
  P(waited_proc->wait_child_sem);

  //seting exit code
  *status = waited_proc->exit_val;

  //return value:w
  *retval = child_pid;

  return 0;
}

int
sys_execv(char *progname , char *argv[])
{

  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;
    int index; //use for count how long does the argv will be



    // copies from user address "path into kernel"
    // we may need value of size
    // size_t = size;
    char *kpath_progrname = (char *) kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t) progname, kpath_progrname, PATH_MAX, NULL );

    if(result) {
      kfree(kpath_progrname);
      /* ran into user-kernel boundary */
      return EFAULT;
    }

    /* copy the addresses using copyin*/
    char **kpath_argv = (char **)kmalloc(PATH_MAX);
    result = copyin((const_userptr_t) argv, kpath_argv, sizeof(char **));

    if(result) {
      kfree(kpath_argv);
      return EFAULT;
    }

    for(index = 0;argv[index] != NULL; index++) {
      size_t size = 0;
      kpath_argv[index] = (char *)kmalloc(sizeof(char *));
      result = copyinstr((const_userptr_t)argv[index], kpath_argv[index], PATH_MAX, NULL);

      if(result) {
        kfree(kpath_argv[index]);
      }

      if(size > __ARG_MAX) {
        kprintf("in execv_syscall file line 80\n");
      }

    }

    kpath_argv[index] = NULL;

  /* Open the file. */
  result = vfs_open(kpath_progrname, O_RDONLY, 0, &v);
  if (result) {
    return result;
  }



  /* We should be a new process. */
    /* since we are forked alreadly
     * and the process existed alreadly we do not have to
     * get a new process*/
  //KASSERT(proc_getas() == NULL);


    /* destroy to old vmspace*/
    as_destroy(curthread->t_proc->p_addrspace);
    curthread->t_proc->p_addrspace = NULL;

  /* Create a new address space. */
  curthread->t_proc->p_addrspace = as_create();
    as = curthread->t_proc->p_addrspace;
  if (as == NULL) {
    vfs_close(v);
    return ENOMEM;
  }

  /* Switch to it and activate it. */
    proc_setas(as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    vfs_close(v);
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    return result;
  }


    /*
     * from the tutorial "as_define_stack sets up a stack in a address space , and 
     * returns the address of the top of the stack "
     *
     * -- since the stack grows down toward lower numbered addresses
     * -- we have to reverse from kernel to user space via decrement 
     *    program counter
     *
     *
     *    ------------------
     *    |                | <-----pc
     *    ------------------
     *    |               2| 12
     *    ------------------
     *    |              \0| 11
     *    ------------------
     *    |               1| 10
     *    ------------------
     *    |              \0| 9
     *    -----------------|
     *    |         argv[1]| 8
     *    ------------------
     *    |         argv[1]| 7
     *    ------------------
     *    |         argv[1]| 6
     *    ------------------
     *    |   12  = argv[1]| 5
     *    ------------------
     *    |         argv[0]| 4
     *    ------------------
     *    |         argv[0]| 3
     *    ------------------
     *    |         argv[0]| 2
     *    ------------------
     *    |    10 = argv[0]| 1
     *    ------------------
     *
     *    have to remamber to to alien
     *
     * */

    int pc_maker[index];
    int i;

    /*decrement for element*/
    for(i = index -1; i >=0; i--) {
      int len = strlen(kpath_argv[i])+1;
      int size = sizeof(char *);
      int decrement = 0;
      if(len % size != 0) {
        decrement = len + (size - (len % size));
        stackptr -= decrement;
        pc_maker[i] = stackptr;
      } else {
        decrement = len + 4;
        stackptr -= decrement;
        pc_maker[i] =stackptr;
      }

      result = copyoutstr((const void *)kpath_argv[i], (userptr_t)stackptr,len, NULL);
      
      if(result){
        panic("doing the erro checking, execv_syscall 168 \n");
        return EINVAL ; 
      }
    }

    /*decrement from argv[1] to argv[0] */
    for(i = index -1; i >= 0; i--) {
      stackptr -= sizeof(char *);
      copyout(&pc_maker[i],(userptr_t)stackptr,sizeof(int));
    }
  /* Warp to user mode. */
  enter_new_process(index /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/, NULL
        /*userspace addr of environment*/,
        stackptr, entrypoint);

  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
}



int sys_getpid(pid_t *retval) {

  *retval = curproc->t_pid;

  return 0;
}



ssize_t sys_write(int filehandle, const void *buf,
          size_t size, ssize_t *retval) {
  (void)filehandle;
  (void)size;
  (void)retval;
  (void)buf;

  int s = splhigh();
  kprintf("%s",(char *)buf);
  splx(s);
  return 0;
}


int sys_myprint(const void *buf) {
  kprintf("%s", (char *)buf);
  return 0;
}
