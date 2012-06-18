/*
 * trap.c - x86 fault handling
 */

#include "dune.h"
#include "cpu-x86.h"

static dune_pgflt_cb pgflt_cb = NULL;
static dune_syscall_cb syscall_cb = NULL;
static dune_bkpt_cb bkpt_cb = NULL;

static inline unsigned long read_cr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2, %0\n\t" : "=r" (val));
	return val;
}

void dune_register_pgflt_handler(dune_pgflt_cb cb)
{
	pgflt_cb = cb;
}

void dune_register_bkpt_handler(dune_bkpt_cb cb)
{
	bkpt_cb = cb;
}

void dune_register_syscall_handler(dune_syscall_cb cb)
{
	syscall_cb = cb;
}

static bool addr_is_mapped(void *va)
{
	int ret;
	ptent_t *pte;

	ret = dune_vm_lookup(pgroot, va, CREATE_NONE, &pte);
	if (ret)
		return 0;

	if (!(*pte & PTE_P))
		return 0;

	return 1;
}

#define STACK_DEPTH 12

static void dune_dump_stack(struct dune_tf *tf)
{
	int i;
	unsigned long *sp = (unsigned long *) tf->rsp;

	// we use dune_printf() because this might
	// have to work even if libc doesn't.
	dune_printf("dune: Dumping Stack Contents...\n");
	for (i = 0; i < STACK_DEPTH; i++) {
		if (!addr_is_mapped(&sp[i])) {
			dune_printf("dune: reached unmapped addr\n");
			break;
		}
		dune_printf("dune: RSP%+-3d 0x%016lx\n", i * sizeof(long),
			   sp[i]);
	}
}

void dune_dump_trap_frame(struct dune_tf *tf)
{
	// we use dune_printf() because this might
	// have to work even if libc doesn't.
	dune_printf("dune: --- Begin Trap Dump ---\n");
	dune_printf("dune: RIP 0x%016llx\n", tf->rip);
	dune_printf("dune: CS 0x%02x SS 0x%02x\n", tf->cs, tf->ss);
	dune_printf("dune: ERR 0x%08lx RFLAGS 0x%08lx\n", tf->err, tf->rflags);
	dune_printf("dune: RAX 0x%016lx RCX 0x%016lx\n", tf->rax, tf->rcx);
	dune_printf("dune: RDX 0x%016lx RBX 0x%016lx\n", tf->rdx, tf->rbx);
	dune_printf("dune: RSP 0x%016lx RBP 0x%016lx\n", tf->rsp, tf->rbp);
	dune_printf("dune: RSI 0x%016lx RDI 0x%016lx\n", tf->rsi, tf->rdi);
	dune_printf("dune: R8  0x%016lx R9  0x%016lx\n", tf->r8, tf->r9);
	dune_printf("dune: R10 0x%016lx R11 0x%016lx\n", tf->r10, tf->r11);
	dune_printf("dune: R12 0x%016lx R13 0x%016lx\n", tf->r12, tf->r13);
	dune_printf("dune: R14 0x%016lx R15 0x%016lx\n", tf->r14, tf->r15);
	dune_dump_stack(tf);
	dune_printf("dune: --- End Trap Dump ---\n");
}

void dune_syscall_handler(struct dune_tf *tf)
{
	if (syscall_cb) {
		syscall_cb(tf);
	} else {
		dune_printf("missing handler for system call - #%d\n", tf->rax);
		dune_dump_trap_frame(tf);
		dune_die();
	}
}

void dune_trap_handler(int num, struct dune_tf *tf)
{
	switch (num) {
	case T_PGFLT:
		if (pgflt_cb) {
			pgflt_cb(read_cr2(), tf->err, tf);
		} else {
			dune_printf("unhandled page fault %lx %lx\n",
				   read_cr2(), tf->err);
			dune_dump_trap_frame(tf);
			dune_die();
		}
		break;

	case T_BRKPT:
		if (bkpt_cb) {
			bkpt_cb(tf);
		} else {
			dune_printf("unhandled breakpoint\n");
			dune_dump_trap_frame(tf);
			dune_die();
		}
		break;
		
	case T_NMI:
	case T_DBLFLT:
	case T_GPFLT:
		dune_printf("fatal exception %d, code %lx - dying...\n",
			   num, tf->err);
		dune_dump_trap_frame(tf);
		dune_die();
		break;

	default:
		dune_printf("unhandled exception %d\n", num);
		dune_dump_trap_frame(tf);
		dune_die();
	}
}