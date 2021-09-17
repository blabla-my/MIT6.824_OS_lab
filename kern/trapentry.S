/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn''t call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn''t push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
	TRAPHANDLER_NOEC(DIVIDE_ENTRY,T_DIVIDE)		//0
	TRAPHANDLER_NOEC(DEBUG_ENTRY,T_DEBUG)		//1
	TRAPHANDLER_NOEC(NMI_ENTRY,T_NMI)		//2
	TRAPHANDLER_NOEC(BRKPT_ENTRY,T_BRKPT)  		//3
	TRAPHANDLER_NOEC(OFLOW_ENTRY,T_OFLOW)		//4
	TRAPHANDLER_NOEC(BOUND_ENTRY,T_BOUND)		//5
	TRAPHANDLER_NOEC(ILLOP_ENTRY,T_ILLOP)		//6
	TRAPHANDLER_NOEC(DEVICE_ENTRY,T_DEVICE)		//7

	TRAPHANDLER(DBLFLT_ENTRY,T_DBLFLT)			//8

	TRAPHANDLER_NOEC(COPROC_ENTRY,T_COPROC)	//9

	TRAPHANDLER(TSS_ENTRY,T_TSS)				//10
	TRAPHANDLER(SEGNP_ENTRY,T_SEGNP)			//11
	TRAPHANDLER(STACK_ENTRY,T_STACK)			//12
	TRAPHANDLER(GPFLT_ENTRY,T_GPFLT)			//13
	TRAPHANDLER(PGFLT_ENTRY,T_PGFLT)			//14
	TRAPHANDLER(RES_ENTRY,T_RES)		//15
	TRAPHANDLER_NOEC(FPERR_ENTRY,T_FPERR)		//16

	TRAPHANDLER_NOEC(ALIGN_ENTRY,T_ALIGN)		//17
	TRAPHANDLER_NOEC(MCHK_ENTRY,T_MCHK)			//18
	TRAPHANDLER_NOEC(SIMDERR_ENTRY,T_SIMDERR)	//19

	TRAPHANDLER_NOEC(SYSCALL_ENTRY,T_SYSCALL)		//48
/*
 * Lab 3: Your code here for _alltraps
 */

	.globl _alltraps;
	.align 2;
	_alltraps:
	pushl %ds
	pushl %es
	pushal
	pushl $GD_KD
	popl %ds
	pushl $GD_KD
	popl %es
	pushl %esp
	call trap