// Raw clone(2) with child trampoline, shared by the ELF and Mach-O halves.
//
// The kernel switches the child to `stack` itself, so this must be assembly:
// C code continued after the syscall could touch spill slots on the parent's
// stack, which is unmapped garbage once RSP has moved. The child path calls
// __cavos_thread_entry (which reads everything through TP and never returns)
// using only the new stack.
#if defined(__APPLE__)
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif

.text
.globl CNAME(__cavos_clone)
CNAME(__cavos_clone):
	// rdi=flags rsi=stack rdx=ptid rcx=ctid r8=tls
	// syscall form wants the 4th arg in r10, not rcx.
	mov %rcx, %r10
	mov $56, %eax		// SYS_clone
	syscall
	test %rax, %rax
	jnz 1f
	// Child: RSP is already the new stack. No parent-stack access past
	// this point; the entry reads start/arg through TP and exits.
	call CNAME(__cavos_thread_entry)
	// Unreachable: entry never returns. Emergency exit if it ever does.
	mov $60, %eax		// SYS_exit
	mov $127, %edi
	syscall
	ud2
1:
	ret

// Unmap a stack mapping, then exit the thread, touching no stack in
// between: the mapping being unmapped IS the caller's stack (detached
// thread self-free). Implemented here because C cannot survive the unmap.
.globl CNAME(__cavos_unmap_and_exit)
CNAME(__cavos_unmap_and_exit):
	// rdi=base rsi=size
	mov $11, %eax		// SYS_munmap
	syscall
	mov $60, %eax		// SYS_exit
	xor %edi, %edi
	syscall
	ud2
