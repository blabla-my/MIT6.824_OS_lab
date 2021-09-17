// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800
extern volatile pde_t uvpd[];
extern volatile pte_t uvpt[];
extern void _pgfault_upcall(void);
//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	//cprintf("curenv: %x\n",sys_getenvid()); 
	//cprintf("pgfault: fault_va: %x, eip: %x,is_write: %d\n",(uint32_t)addr,utf->utf_eip, err&FEC_WR);
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pte_t pte;
	// 检查addr是否对应一个物理页，并且检查pgfault是不是由write COW引起的
	if( !(uvpd[PDX(addr)] & PTE_P) )
		panic("no mapping.\n");
	if ( !((pte= uvpt[PGNUM(addr)]) & PTE_P) )
		panic("no mapping.\n");
	if( !((pte & PTE_COW) && (err & FEC_WR)) )
		panic("not write || copy-on-write.\n");

	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	// 申请一个新页面，map到PFTEMP
	if ((r = sys_page_alloc(sys_getenvid(), PFTEMP, PTE_U|PTE_W|PTE_P)) < 0)
		panic("pgfault: %e\n",r);
	// 把旧页面copy到PFTEMP
	void* pgva = ROUNDDOWN(addr,PGSIZE);
	memcpy((void*)PFTEMP,pgva,PGSIZE);
	// 再把旧页面映射到PFTEMP对应的物理页上
	r = sys_page_map( sys_getenvid(), PFTEMP, sys_getenvid(), pgva, PTE_U|PTE_W|PTE_P );
	if(r<0) 
		panic("pgfault: %e\n",r);
	// unmap PFTEMP
	r = sys_page_unmap(sys_getenvid(), PFTEMP);
	if(r<0) 
		panic("pgfault: %e\n",r);
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	// LAB 4: Your code here.
	int r;
	pte_t pte;
	void *va = (void*)(pn<<PGSHIFT);
	if (!(uvpd[PDX(va)]))
		return -E_INVAL;
	pte = uvpt[pn];
	if( !pte )
		return -E_INVAL;

	int perm = PTE_U | PTE_P ;
	if ( (pte & PTE_W) || ( pte & PTE_COW ) ){
		perm |= PTE_COW;
	}
	if (( r = sys_page_map(sys_getenvid(),va,envid,va,perm)) < 0)
		return r;
	// remap parenat
	if (perm & PTE_COW){
		if (( r= sys_page_map(sys_getenvid(),va,sys_getenvid(),va,PTE_U | PTE_P | PTE_COW) ) <0 ){
			return r;
		}
	}
	//panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{	
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t child = sys_exofork();
	int r;
	if(child < 0){
		panic("fork: %e\n",child);
	}
	//what child does
	if(child == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
	}
	//what father does
	else{
		uint32_t addr = UTEXT;
		pte_t pte;
		pde_t pde;
		for(; addr != UTOP-PGSIZE; addr += PGSIZE){
			if (!(pde = uvpd[PDX(addr)]))
				continue;
			if (!(pte = uvpt[PGNUM(addr)]))
				continue;
			if ((pde & PTE_P) || (pte & PTE_P)){
				if((r=duppage(child,PGNUM(addr)))<0){
					panic("duppage error: %e\n",r);
				}
			}
		}
		r = sys_page_alloc(child,(void*)(UXSTACKTOP-PGSIZE),PTE_U|PTE_P|PTE_W); 
		if(r<0){
			panic("fork: cannot alloc Uxstack\n");
		}
		r = sys_env_set_pgfault_upcall(child,_pgfault_upcall);
		if(r<0){
			panic("fork: cannot set pgfault upcall\n");
		}
		r = sys_env_set_status(child,ENV_RUNNABLE);
		if(r<0){
			panic("fork: cannot set_env_status.\n");
		}
	}
	return child;
	//panic("fork not implemented");

}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
