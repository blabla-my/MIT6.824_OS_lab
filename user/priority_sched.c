// yield the processor to other environments

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int i;

	cprintf("Hello, I am environment %08x. MY priority is: %d.\n", thisenv->env_id,thisenv->env_priority);
	for (i = 0; i < 5; i++) {
		sys_priority_yield();
		cprintf("Back in environment %08x, iteration %d.\n",thisenv->env_id, i);
	}
	cprintf("All done in environment %08x.\n", thisenv->env_id);
}
