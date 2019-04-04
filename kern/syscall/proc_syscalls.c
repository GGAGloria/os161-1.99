#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include "opt-A2.h"
#include <synch.h>
#include <machine/trapframe.h>
#include <copyinout.h>
#include <vfs.h>
#include <kern/fcntl.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
#if OPT_A2
  pid_t parent = p->parent;
  
  if (pid_exist(parent)) {
      for (int i=0; i<10; i++){
        if (pid_getchildpid(parent,i)==p->p_pid){
          pid_setexited(parent,i);
          pid_setexitcode(parent,exitcode,i);
          V(pid_getsemaphore(parent,i));
          break;
        }      
    }
  }
#else 
  (void)exitcode;
#endif

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */


  as = curproc_setas(NULL);
  as_destroy(as);
    
  proc_remthread(curthread);
  proc_destroy(p);

  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  
#if OPT_A2
  *retval = curproc->p_pid;
#else 
  *retval = 1;
#endif

  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval)
{
  int exitstatus;
  int result;

  if (options != 0) {
    return(EINVAL);
  }
#if OPT_A2
    pid_t curpid = curproc->p_pid;
    if (!pid_exist(pid)) return ESRCH;
    if (status ==NULL) return EFAULT;
    if (pid_getparent(pid)!=curpid) return ECHILD;
    int i;
    for (i=0; i<10; i++){
        if (pid_getchildpid(curpid,i) == pid) break;
    }
    P(pid_getsemaphore(curpid,i));
    exitstatus = _MKWAIT_EXIT(pid_getexitcode(curpid,i));
    result = copyout((void *)&exitstatus,status,sizeof(int));
    if (result) {
      return(result);
    }
    *retval = pid;
    return(0);
#else
    exitstatus = 0;
    result = copyout((void *)&exitstatus,status,sizeof(int));
    if (result) {
      return(result);
    }
    *retval = pid;
    return(0);
#endif
}

#if OPT_A2
  

  void fork_init(void* p, unsigned long v);
  void fork_init(void* p, unsigned long v){
    (void)v;
    enter_forked_process((struct trapframe*)p);
  }

  pid_t sys_fork(struct trapframe* tf, pid_t* retval){
    struct trapframe* temp = kmalloc(sizeof(struct trapframe));
    if (temp == NULL) return ENOMEM;
    *temp = *tf;
    struct proc * child = proc_create_runprogram(curproc->p_name);
    if (child==NULL){
      kfree(temp);
      return ENOMEM;
    }
    int result = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (result){
      kfree(temp);
      proc_destroy(child);
      
      return ENOMEM;
    }
    pid_setparent(child->p_pid, curproc->p_pid);
    for (int i=0; i<10; i++){
      if (!(pid_child_exist(curproc->p_pid,i))){
        create_child(curproc->p_pid, i, child->p_pid);
        break;
      }
    }
    
    result = thread_fork(curthread->t_name, child, fork_init, temp, 0);
    if (result){
      as_destroy(child->p_addrspace);
      proc_destroy(child);
      kfree(temp);
      return result;
    }
    *retval = child->p_pid;
    return 0;
  }

int execv(userptr_t program, userptr_t args);
int execv(userptr_t program, userptr_t args){
  if(program == NULL) return ENOENT;
  if(args == NULL) return EFAULT;

  //Count the number of arguments and copy them into the kernel
  
  int argc = 0;
  int result = 0;

  while(true){
      char *temp = NULL;
      result = copyin(args + argc * sizeof(char*), &temp, sizeof(char*));
      if (temp!=NULL) argc++;
      if (temp == NULL) break;
      
  }

  result = 0;
  char **argv = kmalloc(sizeof(char *) * (argc+1));
  if (argv==NULL) return ENOMEM;
  size_t * size = kmalloc(sizeof(size_t)*argc);
  for (int i=0; i<argc; i++){
    argv[i] = kmalloc(sizeof(char*)*ARG_MAX);
    size_t actual_len;
    result = copyinstr(((userptr_t *)args)[i],argv[i],ARG_MAX,&actual_len);
    size[i] = actual_len;
    if(result) return result;
  }
  //Copy the program path into the kernel
  argv[argc] = NULL;
  char *fname_temp;
  fname_temp = kstrdup((char *)program);

  //Open the program file using vfs_open(prog_name, …)
  struct vnode *vn;
  result = vfs_open(fname_temp,O_RDONLY, 0,&vn);
  kfree(fname_temp);
  //Create new address space, set process to the new address space, and activate it
  struct addrspace *ads;
  ads = as_create();
  if (ads ==NULL) {
      vfs_close(vn);
      return ENOMEM;
  }
  struct addrspace *old=curproc_setas(ads);
  as_activate();
  //Using the opened program file, load the program image using load_elf
  vaddr_t entry_point; //
  result = load_elf(vn, &entry_point);
  if (result) {
      vfs_close(vn);
      return result;
  }
  vfs_close(vn);
  /**Need to copy the arguments into the new address space. Consider copying the
arguments (both the array and the strings) onto the user stack as part of
as_define_stack.**/
  vaddr_t stack;
  result = as_define_stack(ads, &stack);
  if (result) return result;
  vaddr_t argsonstack[argc];
  argsonstack[argc] = 0;
  for (int i = argc-1; i>=0; i--){
    size_t len = ROUNDUP(size[i]+1,8);
    stack = stack - len;
    copyoutstr(argv[i],(userptr_t)stack,ARG_MAX,&len);
    if (result) return result;
    argsonstack[i] = stack;
  }
  for (int i=argc; i>=0; i--){
    stack = stack - sizeof(vaddr_t);
    result = copyout(&argsonstack[i], (userptr_t)stack, sizeof(vaddr_t)); 
    if (result) return result;
  }
  vaddr_t userspace = stack;
  //Delete old address space
  
  as_destroy(old);
  for(int i = 0; i < argc; i++){
    kfree(argv[i]);
    argv[i] = NULL;
  }

  kfree(argv);
  argv = NULL;
  /**Call enter_new_process with address to the arguments on the stack, the stack
pointer (from as_define_stack), and the program entry point (from vfs_open)**/
  enter_new_process(argc, (userptr_t)userspace, stack, entry_point);
  return EINVAL;

}
#endif

