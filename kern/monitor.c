// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display information about the kernel", mon_backtrace },
	{ "showva2pa", "Display information about mapping virtual address to physical address",mon_showva2pa}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t *ebp = (uint32_t *)read_ebp();
	struct Eipdebuginfo eipdebuginfo;
	while (ebp != 0)
	{
		//打印ebp, eip, 最近的五个参数
		uint32_t eip = *(ebp + 1);
		cprintf("ebp %08x eip %08x args %08x %08x %08x %08x %08x\n", ebp, eip, *(ebp + 2), *(ebp + 3), *(ebp + 4), *(ebp + 5), *(ebp + 6));
		//打印文件名等信息
		debuginfo_eip((uintptr_t)eip, &eipdebuginfo);
		cprintf("%s:%d", eipdebuginfo.eip_file, eipdebuginfo.eip_line);
		cprintf(": %.*s+%d\n", eipdebuginfo.eip_fn_namelen, eipdebuginfo.eip_fn_name, eipdebuginfo.eip_fn_addr);
		//更新ebp
		ebp = (uint32_t *)(*ebp);
	}
	return 0;
}

int
mon_showva2pa(int argc,char **argv, struct Trapframe *tf){
	intptr_t va;
	struct PageInfo* pp; 
	physaddr_t pa;
	pte_t *pte;
	char *end;
	if(argc > 3 || argc <2){
		cprintf("usage:\n");
		cprintf("showva2pa [va] , to show mapping of va\n");
		cprintf("showva2pa [va start] [va end] , (both align to PGSIZE),to show the mapping in the range [va_start, va_end]\n");
	}
	// print one va mapping
	if(argc == 2){
		va=strtol(argv[1],&end,16);
		pp = page_lookup(kern_pgdir,(void*)va,&pte);
		if(!pp) {
			cprintf("VA: 0x%8x doesn't have a pa mapped\n");
		}
		else 
			cprintf("VA: 0x%8x, PA: 0x%6x, pp_ref: %d, PTE_W: %d, PTE_U: %d\n",
					va, page2pa(pp), pp->pp_ref, ((*pte)&PTE_W)>>1, ((*pte)&PTE_U)>>2);
	}
	// print a range of virtual page
	if(argc == 3){
		intptr_t va_pg_begin,va_pg_end;
		//caculate the begin and end
		va_pg_begin = strtol(argv[1],&end,16);
		va_pg_end = strtol(argv[2],&end,16);
		if( va_pg_begin % PGSIZE==0 && va_pg_end % PGSIZE==0 ){
			//output
			for(va=va_pg_begin; va<=va_pg_end; va+=PGSIZE){
				pp = page_lookup(kern_pgdir,(void *)va,&pte);
				if(!pp) {
					cprintf("VA: 0x%8x doesn't have a pa mapped\n");	
					continue;
				}
				cprintf("VA: 0x%8x, PA: 0x%6x, pp_ref: %d, PTE_W: %d, PTE_U: %d\n",
						va, page2pa(pp), pp->pp_ref, ((*pte)&PTE_W)>>1, ((*pte)&PTE_U)>>2);
			} 
		}
		else
			cprintf("arguments not aligned!\n");
	}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
