# OS-lab3

>In this lab, the terms *environment* and *process* are interchangeable - both refer to an abstraction that allows you to run a program. We introduce the term "environment" instead of the traditional term "process" in order to stress the point that JOS environments and UNIX processes provide different interfaces, and do not provide the same semantics.

environment in jos = process in unix

## struct Env

```c
struct Env {
	struct Trapframe env_tf;	// Saved registers
	struct Env *env_link;		// Next free Env
	envid_t env_id;			// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	enum EnvType env_type;		// Indicates special system environments
	unsigned env_status;		// Status of the environment
	uint32_t env_runs;		// Number of times environment has run

	// Address space
	pde_t *env_pgdir;		// Kernel virtual address of page dir
};
```

每个进程(env)对应一个 struct 来描述该进程以及辅助进程切换

## envs array

struct array的数组

mem_init() in kern/pmap.c 进行申请该数组空间

## Exercise 2

### env_init_percpu

好像设置了一些段寄存器如gs,fs等，暂时不知道具体干啥

### env_setup_vm

这个函数助教写了，大致看了下在干嘛

```c
static int
env_setup_vm(struct Env *e)
{
	int i;
	struct PageInfo *p = NULL;
	//alloc了一个physical page,返回physical address
	if (!(p = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;

	p->pp_ref++; 
    //看起来是把申请到的物理page作为一级页表
    //为什么要KADDR?
    //e->env_pgdir = page2pa(p)+KERNBASE
    //why do this up
	e->env_pgdir = KADDR(page2pa(p));
	memcpy((void*)(e->env_pgdir), (void*)kern_pgdir, PGSIZE);
	//改一些对二级页表的对应关系
	e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;

	return 0;
}

//总而言之就是设置了下new_env的pgdir(一级页表)
//似乎基本上继承了内核的页目录-页表映射

```

### env_alloc

找到一个空的Env

设置它的page_dir，env_id等等字段

```c
int
env_alloc(struct Env **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	struct Env *e;

	if (!(e = env_free_list))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);

	// Set the basic status variables.
	e->env_parent_id = parent_id;
	e->env_type = ENV_TYPE_USER;
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.  When
	// we switch privilege levels, the hardware does various
	// checks involving the RPL and the Descriptor Privilege Level
	// (DPL) stored in the descriptors themselves.
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_eip later.

	// commit the allocation
	env_free_list = e->env_link;
	*newenv_store = e;

	cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	return 0;
}
```

### region_alloc

助教也写了，大致看下干嘛

```c
static void
region_alloc(struct Env *e, void *va, size_t len)
{
	//算出申请va开始的len长度的内存对齐后的上界和下界   
	uint32_t VA = ROUNDDOWN((uint32_t)va, PGSIZE);
	uint32_t N = ROUNDUP((uint32_t)va + len, PGSIZE);

	for(uint32_t i = VA; i< N; i += PGSIZE){
        //看下这些页是否已经被申请，没被申请就申请一页并且更新页表
		if(page_lookup(e->env_pgdir, (void*)i, NULL))
			continue;
		struct PageInfo* pp = page_alloc(0);
		if(!pp) 
			panic("region_alloc: alloc env region failed.");
		page_insert(e->env_pgdir, pp, (void*)i, PTE_U|PTE_W);
	}
}

//总而言之，该函数将一个env的va开始的len长度内存给map了
//和pa map了
```

### load_icode

```c
static void
load_icode(struct Env *e, uint8_t *binary)
{
	// Hints:
	//  Load each program segment into virtual memory
	//  at the address specified in the ELF segment header.
	//  You should only load segments with ph->p_type == ELF_PROG_LOAD.
	//  Each segment's virtual address can be found in ph->p_va
	//  and its size in memory can be found in ph->p_memsz.
	//  The ph->p_filesz bytes from the ELF binary, starting at
	//  'binary + ph->p_offset', should be copied to virtual address
	//  ph->p_va.  Any remaining memory bytes should be cleared to zero.
	//  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
	//  Use functions from the previous lab to allocate and map pages.
	//
	//  All page protection bits should be user read/write for now.
	//  ELF segments are not necessarily page-aligned, but you can
	//  assume for this function that no two segments will touch
	//  the same virtual page.
	//
	//  You may find a function like region_alloc useful.
	//
	//  Loading the segments is much simpler if you can move data
	//  directly into the virtual addresses stored in the ELF binary.
	//  So which page directory should be in force during
	//  this function?
	//
	//  You must also do something with the program's entry point,
	//  to make sure that the environment starts executing there.
	//  What?  (See env_run() and env_pop_tf() below.)

	// LAB 3: Your code here.
	struct Elf *elf = (struct Elf*) binary;
	struct Proghdr* ph, *eph;

	if (elf->e_magic != ELF_MAGIC)
		panic("Not valid ELF file.");

	ph = (struct Proghdr *) ((uint8_t *) elf + elf->e_phoff);
		
	eph = ph + elf->e_phnum;
	
	lcr3(PADDR(e->env_pgdir));
	for (; ph < eph; ph++){
		if(!(ph->p_filesz <= ph->p_memsz))
			panic("Bad proghdr!");
			
		if(ph->p_type != ELF_PROG_LOAD) 
			continue;
		uint32_t va = ph->p_va;
	
		region_alloc(e, (void*)va,  ph->p_memsz);
		memcpy((void*)va, (void*)(binary + ph->p_offset), ph->p_memsz);
		memset((void*)va+ph->p_filesz, '\0', ph->p_memsz - ph->p_filesz);
	}

	// switch back to kern_pgdir
	lcr3(PADDR(kern_pgdir));
	
	// save the eip in trapframe
	e->env_tf.tf_eip = elf->e_entry;

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.

	// LAB 3: Your code here.
	region_alloc(e, (void*)(USTACKTOP - PGSIZE), PGSIZE);
	// note that stack top has been set by env_alloc()
}
```

这个函数把目标elf文件的各个段都加载进内存，并设置env的入口(env_eip)，并且为stack分配第一个页

各个段均加载在elf文件指定的虚拟内存处

eip设置成elf文件的entry, 这里是/obj/user/hello.asm中的start: 0x800020



### exercise2 done successfully

要补充的三个函数:

* env_init
* env_create
* env_run

都很简单，只要理解助教写好的几个函数以及mem_init()就行了

#### env_init

```c
void
env_init(void)
{
	// Set up envs array
	// LAB 3: Your code here.
	int i;

	if(!envs){
		panic("envs not set yet!");
	}
	for(i=0;i<NENV-1;i++){
		envs[i].env_type = ENV_TYPE_USER;
		envs[i].env_parent_id = 0;
		memset(&(envs[i].env_tf),0,sizeof(struct Trapframe));
		envs[i].env_id = 0;
		envs[i].env_link = &envs[i+1];
	}
	envs[NENV-1].env_id = 0;
	envs[NENV-1].env_link = NULL;
	env_free_list = envs;

	//panic("env_init not yet implemented");
	// Per-CPU part of the initialization
	env_init_percpu();
}
```

这个函数的功能是，在mem_init已经alloc了envs的空间之后，初始化每个env，并且将它们从头到尾以链表形式连接起来

#### env_create

```c
void
env_create(uint8_t *binary, enum EnvType type)
{
	// LAB 3: Your code here.
	struct Env *new_env_ptr=0;
	int res;
	res = env_alloc(&new_env_ptr,0);
	if(res<0){
		panic("env_alloc: %e", res);
		return;
	}
	new_env_ptr->env_type = type;
	load_icode(new_env_ptr,binary);
	//panic("env_create not yet implemented");
}	
```

这个函数更简单，功能是申请一个env，并且设置其type，且要将binary装载进该env结构，所以只要用下 env_alloc 和 load_icode ，以及注意一下打印异常信息就行了

#### env_run

```c
env_run(struct Env *e)
{
	// Step 1: If this is a context switch (a new environment is running):
	//	   1. Set the current environment (if any) back to
	//	      ENV_RUNNABLE if it is ENV_RUNNING (think about
	//	      what other states it can be in),
	//	   2. Set 'curenv' to the new environment,
	//	   3. Set its status to ENV_RUNNING,
	//	   4. Update its 'env_runs' counter,
	//	   5. Use lcr3() to switch to its address space.
	// Step 2: Use env_pop_tf() to restore the environment's
	//	   registers and drop into user mode in the
	//	   environment.

	// Hint: This function loads the new environment's state from
	//	e->env_tf.  Go back through the code you wrote above
	//	and make sure you have set the relevant parts of
	//	e->env_tf to sensible values.

	// LAB 3: Your code here.
	if(curenv && (curenv->env_status == ENV_RUNNING)){
		curenv->env_status = ENV_RUNNABLE;
	}
	curenv = e;
	e->env_status = ENV_RUNNING;
	e->env_runs++;
	lcr3(PADDR(e->env_pgdir));

	env_pop_tf(&(e->env_tf));
	//panic("env_run not yet implemented");
}
```

嗯...lab本身的hint实在是过于完整，没啥好补充了，直接看代码里的注释就行

lcr3是load cr3的函数，是将env的页表基地址load进cr3

e->env_tf中是相对内核页表映射而言的虚拟内存，之所以这么设置应该是方便由内核利用虚拟内存进行管理

但是cr3需要的应该是实际的物理地址，所以需要一个PADDR转换



## Exercise3

### 异常和中断的分类

### interupts

* maskable int
    * 0-31
* NMI: none maskable int
    * 32-255

#### exceptions

* Faults
    * 提前预计异常的产生，并且发出report，
* Traps
    * 
* Aborts

### iret

The [IRET](https://pdos.csail.mit.edu/6.828/2018/readings/i386/IRET.htm) instruction is used to exit from an interrupt procedure. [IRET](https://pdos.csail.mit.edu/6.828/2018/readings/i386/IRET.htm) is similar to [RET](https://pdos.csail.mit.edu/6.828/2018/readings/i386/RET.htm) except that [IRET](https://pdos.csail.mit.edu/6.828/2018/readings/i386/IRET.htm) increments ESP by an extra four bytes (because of the flags on the stack) and moves the saved flags into the EFLAGS register. The IOPL field of EFLAGS is changed only if the CPL is zero. The IF flag is changed only if CPL <= IOPL

### IDT

Interrupt Descriptor Table

The interrupt descriptor table (IDT) associates each interrupt or exception identifier with a descriptor for the instructions that service the associated event

The IDT may reside anywhere in physical memory. As [Figure 9-1](https://pdos.csail.mit.edu/6.828/2018/readings/i386/s09_04.htm#fig9-1) shows, the processor locates the IDT by means of the IDT register (IDTR)

The instructions [LIDT](https://pdos.csail.mit.edu/6.828/2018/readings/i386/LGDT.htm) and [SIDT](https://pdos.csail.mit.edu/6.828/2018/readings/i386/SGDT.htm) operate on the IDTR. Both instructions have one explicit operand: the address in memory of a 6-byte area

![img](https://pdos.csail.mit.edu/6.828/2018/readings/i386/fig9-3.gif)

descriptor似乎包含一个指向instruct/exception handle procedure的地址

似乎是先指向GDT表

IDT的功能描述

* the value to load into the instruction pointer (`EIP`) register, pointing to the kernel code designated to handle that type of exception.
* the value to load into the code segment (`CS`) register, which includes in bits 0-1 the privilege level at which the exception handler is to run. (In JOS, all exceptions are handled in kernel mode, privilege level 0.)

### GDT

嘛玩意，不知道，里边有个TSS descriptor, 似乎指向了interrupt procedure 的尾部(???)

### 9.8 Exception Conditions

exercise中这一部分讲了几个中断号对应的中断的遭遇条件

https://pdos.csail.mit.edu/6.828/2018/readings/i386/s09_08.htm

## Exercise 4

### struct Trapfram

```c
struct PushRegs {
	/* registers as pushed by pusha */
	uint32_t reg_edi;
	uint32_t reg_esi;
	uint32_t reg_ebp;
	uint32_t reg_oesp;		/* Useless */
	uint32_t reg_ebx;
	uint32_t reg_edx;
	uint32_t reg_ecx;
	uint32_t reg_eax;
} __attribute__((packed));

struct Trapframe {
	struct PushRegs tf_regs;
	uint16_t tf_es;
	uint16_t tf_padding1;
	uint16_t tf_ds;
	uint16_t tf_padding2;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding3;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding4;
} __attribute__((packed));
```

tf_regs可以用pusha来构建

tf_es:

tf_padding1

tf_ds

tf_trapno:

### struct Gatedesc

IDT的表项，里面有handler的代码段选择子，偏移，等信息

```c
struct Gatedesc {
	unsigned gd_off_15_0 : 16;   // low 16 bits of offset in segment
	unsigned gd_sel : 16;        // segment selector
	unsigned gd_args : 5;        // # args, 0 for interrupt/trap gates
	unsigned gd_rsv1 : 3;        // reserved(should be zero I guess)
	unsigned gd_type : 4;        // type(STS_{TG,IG32,TG32})
	unsigned gd_s : 1;           // must be 0 (system)
	unsigned gd_dpl : 2;         // descriptor(meaning new) privilege level
	unsigned gd_p : 1;           // Present
	unsigned gd_off_31_16 : 16;  // high bits of offset in segment
};
```



### trap

```c
void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	cprintf("Incoming TRAP frame at %p\n", tf);

	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		assert(curenv);

		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// Return to the current environment, which should be running.
	assert(curenv && curenv->env_status == ENV_RUNNING);
	env_run(curenv);
}
```

要求所有interrupt的entry在压栈完trapframe之后全部跳转到trap

trap里的 trap_dispatch 调用了 print_trapframe 打印了trap的相关信息，包括trap种类以及一些寄存器的值。随后会destroy当前的env

trap会重新run这个env