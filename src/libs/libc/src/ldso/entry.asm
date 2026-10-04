; Program-interpreter entry (ELF only - never linked into the dylib).
;
; The kernel maps this .so at AT_BASE and jumps here with RSP -> argc.
; Nothing is relocated yet - not even our own GOT - so this code obeys three
; rules until the C boot takes over:
;   1. Only RIP-relative, stack-relative and immediate addressing. No GOT,
;      no PLT, no calls except to local labels (PC32, fully static).
;   2. Our load base comes from AT_BASE on the stack. Both cavOS and Linux
;      provide it; without it there is no way to find our own EHDR.
;   3. Self-binding uses st_value only, never string comparison: this
;      library has no undefined symbols, so every GLOB_DAT/JMP_SLOT slot
;      resolves locally. A nonzero UND count below means someone added an
;      external dependency - diagnose loudly instead of crashing later.
;
; After self-binding the PLT is valid, so the final `call` reaches the C
; boot (__cavos_ldso_boot), which never returns: it jumps to the executable.

	bits 64

	global _dlstart
	extern __cavos_ldso_boot

	section .text

%define SYS_write	1
%define SYS_exit_group	231

%define AT_NULL	0
%define AT_BASE	7

%define PT_DYNAMIC	2

%define DT_NULL		0
%define DT_PLTRELSZ	2
%define DT_SYMTAB	6
%define DT_RELA		7
%define DT_RELASZ	8
%define DT_SYMENT	11
%define DT_JMPREL	23

%define R_X86_64_NONE		0
%define R_X86_64_64		1
%define R_X86_64_GLOB_DAT	6
%define R_X86_64_JUMP_SLOT	7
%define R_X86_64_RELATIVE	8

; Locals below the saved stack pointer (rbp-relative once framed).
%define L_BASE		0	; our load base (from AT_BASE)
%define L_RELA		8	; runtime address of RELA table
%define L_RELASZ	16	; RELA size in bytes
%define L_JMPREL	24	; runtime address of JMPREL table
%define L_PLTRELSZ	32	; JMPREL size in bytes
%define L_SYMTAB	40	; runtime address of dynsym table
%define L_SYMENT	48	; dynsym entry size
%define L_UND		56	; self-UND counter (must stay zero)
%define L_SIZE		64

_dlstart:
	mov rdi, rsp
	and rsp, -16
	push rdi			; [rsp] = entry sp; rsp%16==8
	sub rsp, L_SIZE		; rsp%16==0: correct for the later call
	mov rbp, rsp
	mov qword [rbp+L_UND], 0

	; ---- walk the stack to the aux vector ----
	mov rsi, [rbp+L_SIZE]	; orig sp -> argc
	mov ecx, [rsi]		; argc
	lea rsi, [rsi+8]	; argv
	lea rsi, [rsi+rcx*8+8]	; skip argv + NULL -> envp
.env_skip:
	mov rax, [rsi]
	add rsi, 8
	test rax, rax
	jnz .env_skip		; rsi -> auxv
.aux_next:
	mov rax, [rsi]		; type
	mov rdx, [rsi+8]	; value
	add rsi, 16
	cmp rax, AT_BASE
	je .have_base_check
	test rax, rax
	jnz .aux_next
	; No AT_BASE entry at all: direct execution, use the fallback below.
.fb_fallback:
	; Fall back to locating our own image by walking back
	; from our own code to the ELF magic, bounded so a weird layout fails
	; in .fatal instead of faulting in the search.
	lea rax, [rel .fb_anchor]
	and rax, -0x1000
	mov ecx, 64
.fb_loop:
	cmp dword [rax], 0x464c457f	; ELF magic?
	jne .fb_next
	cmp word [rax+18], 62		; x86_64?
	jne .fb_next
	cmp word [rax+16], 3		; ET_DYN (we are a shared object)?
	jne .fb_next
	jmp .fb_found
.fb_next:
	sub rax, 0x1000
	dec ecx
	jnz .fb_loop
	lea rsi, [rel msg_no_base]
	mov rdx, msg_no_base_len
	call .fatal
.fb_anchor:
	; rax = our EHDR (>= base page). Our base = ehdr - first LOAD vaddr.
	; Our EHDR is at file offset 0, so walk our PHDRs for the first LOAD.
.fb_found:
	mov r10, [rax+0x20]	; e_phoff
	add r10, rax
	movzx ecx, word [rax+0x38]	; e_phnum
.fb_phdr:
	test ecx, ecx
	jz .fatal_dynamic
	cmp dword [r10], 1	; PT_LOAD
	je .fb_load
	add r10, 56
	dec ecx
	jmp .fb_phdr
.fb_load:
	mov rdx, [r10+16]	; p_vaddr of first LOAD
	sub rax, rdx		; base = ehdr - vaddr
	mov rdx, rax
	jmp .have_base
.have_base_check:
	; AT_BASE present but zero means the same thing: direct execution.
	test rdx, rdx
	jz .fb_fallback
.have_base:
	mov [rbp+L_BASE], rdx
	mov rbx, rdx		; rbx = base for the rest

	; ---- find our PT_DYNAMIC through our own EHDR ----
	; Elf64_Ehdr: e_phoff @0x20, e_phnum @0x38(u16). Phdr: 56B, p_type @0.
	movzx ecx, word [rbx+0x38]
	mov rax, [rbx+0x20]
	lea r10, [rbx+rax]
.phdr_next:
	test ecx, ecx
	jz .fatal_dynamic
	cmp dword [r10], PT_DYNAMIC
	je .have_dynamic
	add r10, 56
	dec ecx
	jmp .phdr_next
.have_dynamic:
	; r10 -> PT_DYNAMIC phdr; p_vaddr @16.
	mov rax, [r10+16]
	add rax, rbx		; rax = runtime DYNAMIC address
.dyn_next:
	mov rcx, [rax]		; d_tag (i64)
	test rcx, rcx
	jz .dyn_done
	mov rdx, [rax+8]	; d_val
	cmp rcx, DT_RELA
	je .save_rela
	cmp rcx, DT_RELASZ
	je .save_relasz
	cmp rcx, DT_JMPREL
	je .save_jmprel
	cmp rcx, DT_PLTRELSZ
	je .save_pltrelsz
	cmp rcx, DT_SYMTAB
	je .save_symtab
	cmp rcx, DT_SYMENT
	je .save_syment
	add rax, 16
	jmp .dyn_next
.save_rela:
	add rdx, rbx
	mov [rbp+L_RELA], rdx
	add rax, 16
	jmp .dyn_next
.save_relasz:
	mov [rbp+L_RELASZ], rdx
	add rax, 16
	jmp .dyn_next
.save_jmprel:
	add rdx, rbx
	mov [rbp+L_JMPREL], rdx
	add rax, 16
	jmp .dyn_next
.save_pltrelsz:
	mov [rbp+L_PLTRELSZ], rdx
	add rax, 16
	jmp .dyn_next
.save_symtab:
	add rdx, rbx
	mov [rbp+L_SYMTAB], rdx
	add rax, 16
	jmp .dyn_next
.save_syment:
	mov [rbp+L_SYMENT], rdx
	add rax, 16
	jmp .dyn_next
.dyn_done:

	; ---- apply RELA then JMPREL with one shared loop ----
	; Rela[24B]: r_offset @0, r_info @8, r_addend @16.
	mov r12, [rbp+L_RELA]
	mov rax, [rbp+L_RELASZ]
	lea r13, [r12+rax]
	call .apply_range
	mov r12, [rbp+L_JMPREL]
	mov rax, [rbp+L_PLTRELSZ]
	lea r13, [r12+rax]
	call .apply_range

	cmp qword [rbp+L_UND], 0
	jne .fatal_self_und

	; ---- hand over to C; PLT is valid now ----
	mov rdi, rbx		; base
	mov rsi, [rbp+L_SIZE]	; entry sp
	mov rsp, rbp
	add rsp, L_SIZE+8
	; PLT call through our own (now bound) slot: the symbol is defined in
	; this library, and entry.asm's self-binding pass already resolved it.
	call __cavos_ldso_boot wrt ..plt
	hlt			; boot jumps to the executable, never returns

; Apply relocs in [r12, r13). Uses rbx (base), rbp (locals). Local call, safe.
.apply_range:
	cmp r12, r13
	jae .range_done
	mov r8, [r12]		; r_offset (link address)
	mov r9, [r12+8]		; r_info
	mov r10, [r12+16]	; r_addend
	mov eax, r9d		; r_type
	cmp eax, R_X86_64_RELATIVE
	je .rel_relative
	cmp eax, R_X86_64_GLOB_DAT
	je .rel_sym
	cmp eax, R_X86_64_JUMP_SLOT
	je .rel_sym
	cmp eax, R_X86_64_64
	je .rel_sym
	cmp eax, R_X86_64_NONE
	je .rel_next
	; Unsupported self reloc: nothing here should need more than the above.
	lea rsi, [rel msg_self_reloc]
	mov rdx, msg_self_reloc_len
	; careful: this call happens with a live range loop; .fatal never returns.
	call .fatal
.rel_relative:
	lea r11, [rbx+r10]
	mov [rbx+r8], r11
	jmp .rel_next
.rel_sym:
	shr r9, 32			; symbol index
	test r9d, r9d
	jz .sym_null		; index 0: *loc = addend
	; sym = symtab + idx * syment.
	mov rax, r9
	imul rax, [rbp+L_SYMENT]
	mov r11, [rbp+L_SYMTAB]
	add r11, rax
	cmp word [r11+6], 0	; st_shndx == SHN_UNDEF?
	je .rel_und
	mov r11, [r11+8]	; st_value
	add r11, rbx
	add r11, r10
	mov [rbx+r8], r11
	jmp .rel_next
.sym_null:
	mov [rbx+r8], r10
	jmp .rel_next
.rel_und:
	inc qword [rbp+L_UND]
.rel_next:
	add r12, 24
	jmp .apply_range
.range_done:
	ret

.fatal_dynamic:
	lea rsi, [rel msg_no_dynamic]
	mov rdx, msg_no_dynamic_len
	call .fatal
.fatal_self_und:
	lea rsi, [rel msg_self_und]
	mov rdx, msg_self_und_len
	call .fatal

; Fatal error: raw write(2, rsi, rdx) then exit_group(127). Never returns.
.fatal:
	mov rax, SYS_write
	mov rdi, 2
	syscall
	mov rax, SYS_exit_group
	mov rdi, 127
	syscall
	hlt

	section .rodata

msg_no_base:
	db "hmake-ldso: no AT_BASE on stack, cannot find own image", 10
msg_no_base_len equ $ - msg_no_base
msg_no_dynamic:
	db "hmake-ldso: own PT_DYNAMIC not found", 10
msg_no_dynamic_len equ $ - msg_no_dynamic
msg_self_reloc:
	db "hmake-ldso: unsupported self relocation type", 10
msg_self_reloc_len equ $ - msg_self_reloc
msg_self_und:
	db "hmake-ldso: self has undefined symbols (external dependency added?)", 10
msg_self_und_len equ $ - msg_self_und
