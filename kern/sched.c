#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.
	// LAB 4: Your code here.
	envid_t i,curid;
	idle = curenv;

	if(idle == NULL)
		curid = NENV-1;
	else
		curid = ENVX(idle->env_id);
	// 将envs视为一个循环列表来查找，找到一个runnable的进程就run
	// 找不到，就查看curenv能否再run(有可能已经结束了)
	for( i=(curid+1)%NENV ; i != curid ; i = (i+1)%NENV){
		if(envs[i].env_status == ENV_RUNNABLE){
			env_run(&envs[i]);
		}
	}

	if(idle && (idle->env_status==ENV_RUNNING || idle->env_status==ENV_RUNNABLE)){
		env_run(idle);
	}
	// sched_halt never returns
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
// halted cpu is in kernel, but doesn't do anything, and release
// the kernel_lock
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

void sched_priority_yield(void){
	struct Env *idle;
	envid_t i,maxi=-1;
	idle = curenv;
	// 寻找可以run的优先级最大的进程
	priority max=-1;
	for(i=0; i<NENV; i++){
		if(envs[i].env_status == ENV_RUNNABLE && envs[i].env_priority>max){
			max = envs[i].env_priority;
			maxi = i;
		}
	}
	// 未找到
	if (maxi<0){
		if(idle && (idle->env_status==ENV_RUNNING || idle->env_status==ENV_RUNNABLE)){
			env_run(idle);
		}
	}
	// 找到
	else{
		env_run(&envs[maxi]);
	}
	sched_halt();
}