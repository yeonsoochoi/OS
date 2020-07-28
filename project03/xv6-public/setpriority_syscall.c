#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int
setpriority(int pid, int priority)
{
	struct proc *p;
	int onoff = 1;

	if(priority < 0 || priority > 10){
		return -2;	
	}
	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
		if(p->pid == pid && p->pid > 0)
			onoff = 0;
	}
	if(onoff)
		return -1;
	
	acquire(&ptable.lock);
	p->priority = priority;
	release(&ptable.lock);
	return 0;
	
}

int
sys_setpriority(void)
{
	int pid, priority;
	if(argint(0,&pid) < 0)
		return -1;
	if(argint(1,&priority) < 0)
		return -1;

	return setpriority(pid, priority);
}
