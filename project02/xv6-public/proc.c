#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);


void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}
// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  /* init */
  p->queue = 0;
  p->priority = 0;
  p->lastqueue = 0;
  p->ppid = 1;
  p->mode = 0;
  p->limit = 0;
  p->shared_memory = 0;
  p->stack_count = 1;
  acquire(&tickslock);
  p->ticks = ticks;
  release(&tickslock);

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  sz = curproc->sz;

  if(curproc->limit < sz+n && curproc->limit !=0)
	  return -1;

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  char *va;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    goto bad;
  }


  if((va = kalloc()) == 0){
	goto bad;	  
  }
  np->shared_memory = va; 


  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  np->ppid = np->parent->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;

bad:
	kfree(np->kstack);
	np->kstack = 0;
	np->state = UNUSED;
	return -1;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  char *va;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  pde_t *pde;
  pde_t *pgtab;
  pde_t *pgdir;
  pde_t *pte;

  va= curproc->shared_memory;

  if(va == 0)
	  goto skip_free_shared_memory;
  acquire(&ptable.lock);
  for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
	  if(p->pid != 0 && p->shared_memory !=0 && p->pgdir != 0 && p->pid != curproc->pid){
		  pgdir = p->pgdir;
		  pde = &pgdir[PDX(va)];
		  if(*pde & PTE_P){
			  pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
		  }
		  else
			  continue;
		  pte = &pgtab[PTX(va)];
		  *pte = *pte | PTE_W;
		  *pte = *pte & ~PTE_U;
	  }
  }
  release(&ptable.lock);
  kfree(va);

skip_free_shared_memory:
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}


//-----------------------------------------------------------------------
// check whether even numbers exist
// if table has even numbers pid, it returns 1
// else it returns 0
int
even_exist(void)
{
	struct proc *p;
	int count = 0;
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if((p->pid) % 2 == 0 && p->state == RUNNABLE && (p->pid) != 2){
			count++;
		}
	}
	if(count == 0)
		return 0;
	return 1;
}



// to find minimum odd pid
// it is used in scheduler() to compose FCFS scheduler
struct proc*
min_odd_pid(void)
{
	struct proc *p;
	struct proc *return_p = ptable.proc;
	int min = 9999999;
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if((p->pid) < min && (p->pid) > 2 && (p->pid)%2 == 1 && p->state == RUNNABLE){
			// pid 1 and 2 is running before program start. therefore pid should be bigger than 2
			min = p -> pid;
			return_p = p;
		}
	}
	return return_p;
}

// to check queue is empty or not
int
check_queuenum()
{
	struct proc *p;
	int count = 0;
	int highest = 8;
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if((p->queue) < highest && p->state == RUNNABLE && p->pid != 1 && p->pid !=2 && p->lastqueue== 0){
			highest = p->queue;
			count++;
		}
	}
	if(count == 0)
		return -1;
	return highest;
}

// return proc* which has the highest priority
struct proc*
select_p(int num)
{
	struct proc *p;
	struct proc *return_p=0;
	int max = -2;

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if((p->priority) >= max && (p->state) == RUNNABLE && (p->queue) == num 
			&& !(p->lastqueue) && p->pid != 1 && p->pid !=2){
				max = p->priority;
				return_p = p;
		}
	}
	return return_p;
	
}



//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{

#ifdef MULTILEVEL_SCHED
	struct proc *p;
  	struct cpu *c = mycpu();
  	struct proc *choice = ptable.proc;
  	c->proc = 0;
  	int check = 0;
  	for(;;){
    	// Enable interrupts on this processor.
    	sti();
    	// Loop over process table looking for process to run.
    
	
		acquire(&ptable.lock);
		check = even_exist();
    	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
	  		choice = p;// default
			if(p-> state != RUNNABLE)
				continue;
	 
	  		// check == 1 means, there are even numbers in ptable
	  		// using RR
	  		if(p->pid == 1 || p->pid ==2){
				choice = p;
	  		}
	  		else if(check){
		 	// there are even number in ptable but this time is odd number  
		 		if((p->pid) %2 != 0)
			 		continue;
			 	choice = p;
	  		}		 


	  		// check == 0 means, there are only odd numbers in ptable
	  		// using FCFS
	  		// process which has minimum pid came earlier than otehrs
	  		else
				choice = min_odd_pid();

    
	  		// Switch to chosen process.  It is the process's job
      		// to release ptable.lock and then reacquire it
      		// before jumping back to us.
	  		c->proc = choice;
      		switchuvm(choice);// load process
      		choice->state = RUNNING;

      		swtch(&(c->scheduler), choice->context);
      		switchkvm();// kernel load its memory

      		// Process is done running for now.
      		// It should have changed its p->state before coming back.
      		c->proc = 0;
	  	}
		release(&ptable.lock);
	}
#elif MLFQ_SCHED
	struct proc *p;
	struct proc *tmp=0;
	struct proc *choice=0;
	struct cpu *c = mycpu();
	
	c->proc = 0;
	for(;;){
		sti();

		acquire(&ptable.lock);
		
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->state != RUNNABLE)
				continue;
			int num=0;
			if(p-> pid == 1 || p->pid ==2){
				choice = p;
			}

			else{ 
				// find the highest level queue which contain RUNNABLE
				num = check_queuenum();
						
				// there's nothing in queues
				if(num == -1)
					priority_boosting_nolock();
				// if something is in the queue
				else{
					// return the highest priority process in the queue
					tmp = select_p(num); 
					if(tmp -> ticks > ((2*num)+4)){ // tmp used in the last and over the time quantum
						if(num != MLFQ_K){ // it is not last queue
							tmp->queue = num+1; // go to below queue
							tmp->ticks = 0;
						}
						else
							tmp->lastqueue = 1; // it's finished. wait boosting
						continue;
					}
					
					else{ // ramains time quantum
						choice = tmp;
					}
				}

			}
			c->proc = choice;
      		switchuvm(choice);
      		choice->state = RUNNING;

      		swtch(&(c->scheduler), choice->context);
      		switchkvm();
			c->proc = 0;
		}
		release(&ptable.lock);
	}

#else  // original scheduling
	struct proc *p;
  	struct cpu *c = mycpu();
  	c->proc = 0;
	
  	for(;;){
    	// Enable interrupts on this processor.
    	sti();

    	// Loop over process table looking for process to run.
    	acquire(&ptable.lock);
    	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      		if(p->state != RUNNABLE)
        		continue;

      		// Switch to chosen process.  It is the process's job
      		// to release ptable.lock and then reacquire it
      		// before jumping back to us.
	  		c->proc = p;
      		switchuvm(p);// load process
      		p->state = RUNNING;

      		swtch(&(c->scheduler), p->context);
      		switchkvm();// kernel load its memory

      		// Process is done running for now.
      		// It should have changed its p->state before coming back.
      		c->proc = 0;
	 	} 
    	release(&ptable.lock);
  	}
#endif
}




// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
print_hello(void)
{
	cprintf("hello\n");	
}
int
sys_print_hello(void)
{
	print_hello();
	return 0;
}



void
yield(void)
{ 
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}


/**** make yield to system call****/
int 
sys_yield(void)
{
	yield();
	return 0;
}
/**********************************/


/** make getlev to system call ***/
int 
getlev(void)
{
	int q_lev;
	struct proc *p = myproc();
	q_lev = p -> queue;
	return q_lev;
}

int
sys_getlev(void)
{
	int lev;
	lev = getlev();
	return lev;	
}
/***********************************/


int
setpriority(int pid, int priority)
{
	struct proc *p;
	struct proc *current_p = myproc();
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
	
	onoff = 1;
	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
        
		if(current_p && p->pid == pid && p->ppid == current_p->pid){
			onoff = 0;
			break;
		}
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

// when time is finished, go to below queue
void
yield_MLFQ(int queuelevel)
{
	acquire(&ptable.lock);
	myproc()->state = RUNNABLE;
	myproc()->queue = queuelevel+1;
	sched();
	release(&ptable.lock);
	
}

void
yield_MLFQ_last_Level(int queuelevel)
{
	myproc()->lastqueue = 1;
	yield();	
}
#ifdef MLFQ_SCHED

void
priority_boosting(void)
{
	struct proc *p;
	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->queue != L0 && p->pid > 0){
			if(p-> state == RUNNING || p->state == RUNNABLE){
				p->queue = L0;
				p->ticks = 0;
				p->state = RUNNABLE;
				p->lastqueue = 0;
			}
		}		
	}
	release(&ptable.lock);

}
void
priority_boosting_nolock(void)
{
	struct proc *p;
	
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->queue != L0 && p->pid > 0){
			if(p-> state == RUNNING || p->state == RUNNABLE){
				p->queue = L0;
				p->ticks = 0;
				p->state = RUNNABLE;
				p->lastqueue = 0;
			}
		}			
	}
}

#endif

int
getadmin(char *password)
{
	struct proc *p;
	p = myproc();
	if(!strcmp(password, "2015005141")){
		p->mode = 1;
		return 0;
	}
	return -1;
}

int
sys_getadmin(void)
{
	char *password;
	if(argstr(0,&password) <0)
		return -1;
	return getadmin(password);
}

int
setmemorylimit(int pid, int limit)
{    
	int check=1;
    struct proc *p;
    
    if(limit < 0 || !(myproc()->mode))
        return -1;
  acquire(&ptable.lock);
    for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            check = 0;
            if(p->sz >= limit){
				release(&ptable.lock);
                return -1;
			}
            else{ 
                p -> limit = limit;
                break;
            }
        }
    }
  release(&ptable.lock);
  if(check){
	  return -1;
  }
  return 0;
}

int
sys_setmemorylimit(void)
{
    int pid, limit;
    if(argint(0,&pid)<0)
        return -1;
    if(argint(1,&limit)<0)
        return -1;
    return setmemorylimit(pid,limit);
}

char*
getshmem(int pid)
{
	struct proc *p;
	char *va;

	va=0;
	acquire(&ptable.lock);
	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
		if(p->pid != 0 && p->pid == pid && p->shared_memory != 0){
			va = p->shared_memory;
			break;
		}
	}
	release(&ptable.lock);

	return va;
}

char*
sys_getshmem(void)
{
	int pid;
	char *va;
	struct proc *p = myproc();

	if(!p)
		return 0;
	if(argint(0,&pid)<0)
		return 0;

	va = getshmem(pid);
	if(va==0)
		return 0;

	pde_t *pde;
	pde_t *pgtab;
	pde_t *pgdir;
	pde_t *pte;

	pgdir = p->pgdir;
	pde = &pgdir[PDX(va)];
	if(*pde & PTE_P){
		pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
	}
	else{
		cprintf("error at getshmem\n");
		if((pgtab = (pte_t*)kalloc()) == 0){
			return 0;
		}
		memset(pgtab, 0, PGSIZE);
		*pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
	}
	pte = &pgtab[PTX(va)];
	*pte = *pte | PTE_U;

	if(pid != p->pid)
		*pte = *pte & ~PTE_W;
	return va;
}

void
aligned_print(int num)
{
	int count = 7;
	int original = num;
	cprintf(" ");
	while(num >= 10){
		num /= 10;
		count--;
	}
	for(int i=0; i< count ; i++){
		cprintf(" ");	
	}
	cprintf("%d", original);
	cprintf("  ");
}


void
list_process(void)
{
	struct proc *p;
	int cur_ticks=0;

	acquire(&tickslock);
	cur_ticks = ticks;
	release(&tickslock);

	
	cprintf("NAME          | PID |  TIME  | STACK PAGES | MEMORY (bytes) |  MEMLIM (bytes)\n");
	acquire(&ptable.lock);
	for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid != 0 && p->killed != 1){
			// print name
			int count = strlen(p->name);
			count = 15 - count;
			cprintf("%s",p->name);
			for(int i = 0; i < count; i++){
				cprintf(" ");	
			}
			// print pid
			
			count = 3;
			int num = p->pid;
			while(num >= 10){
				num /= 10;
				count--;
			}
			for(int i=0; i< count; i++){
				cprintf(" ");
			}
			cprintf("%d",p->pid);
			cprintf(" ");

			// print time
			int time_passed = cur_ticks - p->ticks;
			aligned_print(time_passed);

			// print stack size
			cprintf("   ");
			aligned_print(p->stack_count);

			// print memory size
			cprintf("      ");
			aligned_print(p->sz);
			
			// print memory limit
			cprintf("      ");
			aligned_print(p->limit);
			cprintf("\n");
		}
	}
	release(&ptable.lock);
	cprintf("\n");
}

int
sys_list_process(void)
{
	list_process();	
	return 1;
}


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
