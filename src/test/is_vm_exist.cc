#include <intrin.h>
#include <WinNe.h>
//#pragma comment (lib,"vcruntime")
extern "C" {

	extern bool inline CheckVM();
	extern long long inline CheckCpuidTiming();
	extern bool inline ExecVMREAD(unsigned long long field,  long long* out);
}
unsigned vt_cpu_id() {
	return 0;
}
bool is_vm_exist_lg() {

	int cp[4]{ 1 };
	nt::cpuid(cp);
	if (cp[1] & (1 << 5)) {
		nt::lg("VT-x supported!");
	}
	else {
		return false;
	}

	cp[0] = 0x40000000;
	nt::cpuid(cp);
	{
		nt::lg("Hypervisor: %s", cp + 1);
	}
	cp[0] = 0x40000001;

	nt::cpuid(cp);
	{
		nt::lg("Hypervisor: %s", cp + 1);
	}

	cp[0] = 1;
	nt::cpuid(cp);
	if (cp[2] >> 31) // HYPERV_HYPERVISOR_PRESENT_BIT
	{
		nt::lg("Hypervisor detected!");
	}
	return true;
}
bool is_vm_exist()
{
	is_vm_exist_lg();
	// 0x0296b4e74-694898292 
	// 0x1c5086d59-7600631129
	long long tmp, ll = CheckCpuidTiming();
	bool vm_exist = 0;
	__try {
		ExecVMREAD(0, &tmp); // call vmread
		//CheckVM(); // call vmread
		vm_exist = 1;
	}
	__except (1) {
		vm_exist = 0;
		nt::lg("vm-__except");
	}
	nt::lg("vm:%d %lld", vm_exist, ll ); 

	//// CPUID timing probe
	//int cpuInfo[4];

	//unsigned long long t1 = __rdtsc();
	//__cpuid(cpuInfo, 1);
	//unsigned long long t2 = __rdtsc();

	//if ((t2 - t1) > 3000)
	//	return true;
	
	return false;
}

