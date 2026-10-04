; Every import of libc.dylib is emitted as a lazy binding, which makes lld
; generate a stub helper that jumps through a "dyld_stub_binder" slot. The
; helper is the only reason that name has to exist at all, and it is only ever
; reached if an unpatched stub is called.
;
; It never runs on cavOS. The loader resolves lazy binds eagerly at load time,
; the same way it resolves every other import, so the stubs are already patched
; with final addresses before _start executes. This body only has to exist so
; the link closes.
;
; The symbol is spelled without a leading underscore on purpose: unlike C
; identifiers (which Mach-O prefixes with one), lld looks this name up under
; the exact spelling above. NASM's macho64 backend emits global names
; verbatim, so no underscore tricks are needed - verify with
; `llvm-nm` if this is ever touched.
;
; Assembled only into the Mach-O half of the build; the ELF link has no use
; for it (it is kept out of the .so object list in the Makefile).

	bits 64

	global dyld_stub_binder

	section .text

dyld_stub_binder:
	ret
