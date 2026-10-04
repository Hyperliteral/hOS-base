// Every import of libc.dylib is emitted as a lazy binding, which makes lld
// generate a stub helper that jumps through a "dyld_stub_binder" slot. The
// helper is the only reason that name has to exist at all, and it is only ever
// reached if an unpatched stub is called.
//
// It never runs on cavOS. The loader resolves lazy binds eagerly at load time,
// the same way it resolves every other import, so the stubs are already patched
// with final addresses before _start executes. This body only has to exist so
// the link closes.
//
// The name is spelled out in asm because Mach-O prefixes C identifiers with an
// underscore: a plain C definition here would be "_dyld_stub_binder", and lld
// looks the symbol up under the exact spelling above.
//
// Assembled only into the Mach-O half of the build; the ELF link has no use
// for it (it is kept out of the .so object list in the Makefile).
	.globl dyld_stub_binder
dyld_stub_binder:
	retq
