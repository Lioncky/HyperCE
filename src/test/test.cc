#define NX_G
#include "hyperce.h"

#include <../drv/access/Driver/syscall.h>

// 此函数仅供调试使用，
// 正式发布时会被自动删除
void debug_test() {

}

//  [5/28/2026 Administrator]
void debug_test1() {
	
	#if 1
	
	#else
	
	// settings.version 
	// HKEY_CURRENT_USER\Software\VMware, Inc.\GuestGLDriver\Application Settings
	//const char *ptr =  nt::CpuQName();
	UINT64 lpDst;
	PSTR lpSrc = (PSTR)0x140000000;
	SIZE_T sz = 0xC67D000;
	PSTR ap = nt::valloc(sz);
	EXREADVIRTUALMEMORY_ARGS args{
		(HANDLE)13508,
		(PVOID)0x140000000,
		ap,
		sz
	};
	nt::WriteDown("E:\\2.img", ap, sz);
	nt::syscall(SyscallExReadVirtualMemory, &args);
	//NtWaitforDebugger();
	//int args[4]{0x80000002};
	//nt::cpuid(args);
	//is_vm_exist();
	return;

	LPVOID lpEprocess = Nt::QueryEProcess(ntpid());

	UINT64 uBuffer = 222;
	PVOID krnl = (PVOID)Nt::GetKrnl();
	auto ret = nt::hy_read_memory(
		NtCurrentProcessId(),
		(void*)krnl, // 
		G->page,
		0x1000
	);
	ret = nt::hy_read_memory(
		0x1Fc8,
		(void*)0x401000, // 
		G->page,
		0x1000
	);

	nt::nop();
	//auto* ptr = nt::hypercall('V2PA', (long long)G);
	////ptr = nt::hypercall('V2PA', nt::GetKrnl());

	//long long aaa = 999;
	////nt::hycall('0000', 0);
	//PHYPER_VMRD p = G->as_vmrd();
	//p->pid_read = NtCurrentProcessId();
	//p->virtual_address = &aaa;
	//p->virtual_size = 8;
	//p->result_read_size = 0;

	//nt::hycall(p);

	ew::ntTerminateProcess(HPROC, 0);
	#endif
}


#ifdef _X86_
# error "x86 is not supported, please compile as x64"
#else
extern "C" { float  _fltused; }; // Only User32's api will be export
#endif