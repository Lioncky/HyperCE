#include <WinNe.h>
#include "hyperce.h"
#include "vm.h"

#define VADFindVadPte(...) 0;dbk()

// -------------------------------------------------------

bool vm_init() {

	// 1. get wmware target process
	Nt::EnumProc(+[](PSYSTEM_PROCESS_INFORMATION_NS _) {
		if (_->ImageName.Length > 8 && nt::wcsstr(_->ImageName.Buffer, L"vmx.exe")) { // vmware-vmx.exe
			G->hPid = _->UniqueProcessId;
		}
		return true;
	});

	// 2. open target process
	if (!(G->hProc = nt::OpenProc(G->hPid, 2035711)))
		return false;

	// 3. enum handles and remap session
	Nt::EnumHandlesName(G->hProc, G->hPid, +[](PWSTR _, SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX* i) {

		if (nt::wcsstr(_, L".vmem")) {

			if (nt::ntMapFile(i->HandleValue, (PSTR)0x200000000, 0x200000000) >= 0) {
				nt::l->UserPtr = (PSTR)0x200000000;
				DDS("8G VMEM LOAD!");
			}
			return false;
		}
		return true;
	});

	// 4. vmx boom
	return nt::vmx_init();
}

BOOLEAN vm_main() {

	nt::syscall_hide();

	//nt::vmware_cpuid(tmp, "Intel(R) Core(TM) Ultra 9 285K                  ");

	//////////////////////////////////////////////////////////////////////////
	//																										//
	// 0. vmm Series API Init																	//
	//																										//
	//////////////////////////////////////////////////////////////////////////

	// 1. read vmx proc's vmem
	if (!vm_init()) {
		dbk("Failed to open process.\n");
		return FALSE;
	}

	if (1)
		return TRUE;

	// 4. 特征读取EPROCESS偏移
	if (G->MemoryKernelDirbase && G->SystemProcessEprocess)
		VMGetWinX64ProcessOffsetDefault();

	return G->OsVadRoot && G->SystemProcessEprocess;
}

// -------------------------------------------------------
BOOL VMReadHostRegion(PVOID buffer, ULONG64 lpPhyAddr, SIZE_T size) {
	return nt::vmx_rva(buffer, lpPhyAddr, size);
}

PSTR g_vmem;
BOOL VMReadHostRegion(PVOID buffer, ULONG64 lpPhyAddr, SIZE_T size);

BOOL VMReadHostRegionOri(PVOID buffer, ULONG64 lpPhyAddr, SIZE_T size) {
	// 
	// See doc/pcie.cc
	// 
	if (lpPhyAddr >= 0x100000000)
		lpPhyAddr = lpPhyAddr - 0x40000000;

	#if 0
	return Nt::ReadProc(hProc, g_vmem + lpPhyAddr, buffer, size);
	#endif
	return FALSE;
}

BOOLEAN VMReadPaged(PVOID buffer, DWORD64 DirectoryTableBase, DWORD64 va, DWORD64 pte, SIZE_T size)
{
	BOOLEAN bRet = FALSE;
	BOOLEAN bRead = TRUE;
	DWORD64 ppa = 0;
	VMM_PTE_TP PteTp = VMM_PTE_TP_NA;
	do
	{
		if (!buffer || !pte || !size || !va) {
			break;
		}

		PteTp = MemX64TransitionPaged(va, pte, DirectoryTableBase, 0, &ppa);
		switch (PteTp)
		{
		case VMM_PTE_TP_HARDWARE:
		case VMM_PTE_TP_TRANSITION: {
			if (!VMReadHostRegion(buffer, (ppa | (va & 0xFFF)), size)) {
				bRead = FALSE;
				break;
			}
			break;

		}
		case VMM_PTE_TP_DEMANDZERO:
			xmemo(buffer, size);
			break;
		case VMM_PTE_TP_COMPRESSED:
			dbk();
			break;
		case VMM_PTE_TP_PAGEFILE:
			dbk();
			break;
		default:
			break;
		}

		bRet = TRUE;
	} while (FALSE);

	return bRet;
}

BOOLEAN VMFindKernel() {
	char* buffer = Nt::MemPtr();

	for (DWORD64 i = (G->MemoryKernelEntry & ~0x1fffff) + 0x20000000;
		i > G->MemoryKernelEntry - 0x20000000; i -= 0x1000) {
		if (!VMReadVmVirtualAddr(buffer, G->MemoryKernelDirbase, i, 0x1000)) {
			continue;
		}
		if ((*(short*)(void*)(buffer) == IMAGE_DOS_SIGNATURE)) {
			int kdbg = 0, poolCode = 0;
			for (int u = 0; u < 0x1000; u++) {

				// InitKDBG节保存了PG保护的主要检测的逻辑
				if (!kdbg && *(DWORD64*)(void*)(buffer + u) == 0x4742444b54494e49) // *(DWORD64*)"INITKDBG";
					kdbg = 1;

				if (!poolCode && *(DWORD64*)(void*)(buffer + u) == 0x45444f434c4f4f50) // *(DWORD64*)"POOLCODE")
					poolCode = 1;

				if (kdbg & poolCode) {
					G->MemoryKernelBase = i;
					G->SystemProcessEprocess = (DWORD64)VMGetExportsFunAddr(G->MemoryKernelBase, "PsInitialSystemProcess", FALSE);
					G->PsLoadedModuleListPtr = VMGetExportsFunAddr(G->MemoryKernelBase, "PsLoadedModuleList", TRUE);
					DDS("NtOsKernel:%p\n g_SystemProcessEprocess %p\ng_PsLoadedModuleListPtr %p\n", 
						(void*)i, (void*)G->SystemProcessEprocess, (void*)G->PsLoadedModuleListPtr
					);
					return (G->SystemProcessEprocess && G->PsLoadedModuleListPtr) ? TRUE : FALSE;
				}
			}
		}
	}
	return FALSE;
}


// uefi启动的系统，0x1000-0x100000的物理地址存了一个结构体PROCESSOR_START_BLOCK，而且都在页的开头。
//其中_KPROCESSOR_STATE中有system的cr3
//参考：http://standa-note.blogspot.com/2020/03/initializing-application-processors-on.html
//参考：http://publications.alex-ionescu.com/Recon/ReconBru%202017%20-%20Getting%20Physical%20with%20USB%20Type-C,%20Windows%2010%20RAM%20Forensics%20and%20UEFI%20Attacks.pdf
BOOLEAN VMNtKernelDataInitOri() {
	
	char* buffer = nt::tmp();

	for (DWORD64 i = 0; i < 10; i++) {
		VMReadHostRegion(buffer, i * 0x10000, 0x10000);

		for (DWORD64 o = 0; o < 0x10000; o += 0x1000) {
			if (0x00000001000600E9 ^
				(0xffffffffffff00ff & *(DWORD64*)(void*)(buffer + o))) //START BYTES
				continue;
			if (0xfffff80000000000 ^
				(0xfffff80000000000 & *(DWORD64*)(void*)(buffer + o + 0x70))) //KERNEL ENTRY
				continue;
			if (0xffffff0000000fff & *(DWORD64*)(void*)(buffer + o + 0xa0)) // PML4
				continue;

			G->MemoryKernelDirbase = *(DWORD64*)(void*)(buffer + o + 0xa0);
			G->MemoryKernelEntry = *(DWORD64*)(void*)(buffer + o + 0x70);

			DAS("NT CR3 %llX MemoryKernelEntry %llX", G->MemoryKernelDirbase, G->MemoryKernelEntry);
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN VMGetWinX64ProcessOffsetDefault() {
	auto* p = G;

	p->OsDirectoryTableBase = 0x28,
	p->OsUniqueProcessId = 0x440,
	p->OsImageFileNameOffset = 0x5A8,
	p->OsActiveProcessLinks = 0x448,
	p->OsVadRoot = 0x7D8;

	return TRUE;
}

BOOLEAN VMGetWinX64ProcessOffset()
{
	BOOLEAN bRet = FALSE;
	BOOLEAN bIsTRUE = FALSE;
	static BYTE SystemProcessData[VM_EPROCESS64_MAX_SIZE];
	static BYTE TempProcessData[VM_EPROCESS64_MAX_SIZE];
	do
	{
		if (!(VMReadVmVirtualAddr(SystemProcessData,
			G->MemoryKernelDirbase,
			G->SystemProcessEprocess,
			VM_EPROCESS64_MAX_SIZE))) {
			break;
		}

		//SignalState为TRUE表示此时该进程已被结束(SignalState目前是固定的)
		if (*(PDWORD)(SystemProcessData + 0x4))
			break;
		
		//DirectoryTableBase(目前是固定的)
		if (0xffff800000000000 & *(PDWORD64)(SystemProcessData + 0x28))
			break;
		
		G->OsDirectoryTableBase = 0x28;

		//得到ImageFileNameOffset
		for (int i = 0; i < VM_EPROCESS64_MAX_SIZE - 8; i += 8)
		{
			if (*(PDWORD64)(SystemProcessData + i) == 0x00006D6574737953) //system
			{
				G->OsImageFileNameOffset = i;
				break;
			}
		}

		if (0 == G->OsImageFileNameOffset)
			break;
		

		//得到PID和ActiveProcessLinks 
		for (int i = 0; i < VM_EPROCESS64_MAX_SIZE - 8; i += 8)
		{
			if (*(PDWORD64)(SystemProcessData + i) == 4)
			{
				//这个时候PID有可能是正确，所以再进行验证
				if (0xffff000000000000 !=
					(0xffff000000000000 &
						*(PDWORD64)(SystemProcessData + i + 8)))
				{
					//如果ActiveProcessLinks地址不对,则继续
					continue;
				}
				// NextEprocessAddr=System.Eprocess.ActiveProcessLinks.Flink-Offset
				// NextEprocessAddr.ActiveProcessLinks.Blink-offset=System.Eprocess(证明偏移对了)
				//否则偏移是错的
				DWORD64 TempEprocess = *(PDWORD64)(SystemProcessData + i + 8) - i - 8;
				if (!VMReadVmVirtualAddr(TempProcessData,
					G->MemoryKernelDirbase,
					TempEprocess,
					VM_EPROCESS64_MAX_SIZE))
				{
					continue;
				}
				if ((*(PDWORD64)(TempProcessData + i + 16) - i - 8) != G->SystemProcessEprocess) {
					continue;
				}
				G->OsUniqueProcessId = i;
				G->OsActiveProcessLinks = i + 8;

				bIsTRUE = TRUE;
			}
		}

		if (!bIsTRUE) 
			break;
		
		//得到VAD偏移
		/*通过搜索ExitStatus值来查找VadRoot的偏移量，假定该值被设置为:0x00000103,
		并且在VadRoot之前存在 - 12(VISTA) / -4(Win7 + )值可能是某些系统上的'VadHint';
		扫描回0x40以找到任何相同的匹配(将假定为vadroot)。*/
		{
			DWORD idx = 0;
			for (idx = 0x140 + G->OsImageFileNameOffset; idx < 0x7f0; idx += 8) {

				bIsTRUE = MEM_IS_KERNEL_ADDR_X64(*(PDWORD64)(SystemProcessData + idx)) &&
					((*(PDWORD)(SystemProcessData + idx - 4) == 0x00000103) ||
						(*(PDWORD)(SystemProcessData + idx - 12) == 0x00000103));

				if (bIsTRUE) {
					break;
				}

			}
			//没找到
			if (!bIsTRUE) {
				break;
			}

			G->OsVadRoot = idx;
			for (idx = G->OsVadRoot - 8; idx > G->OsVadRoot - 0x40; idx -= 8) {
				if (*(PDWORD64)(SystemProcessData + idx) == *(PDWORD64)(SystemProcessData + G->OsVadRoot)) {
					G->OsVadRoot = idx;
					break;
				}
			}

		}

		bRet = TRUE;
		//PEB和后续用到了再实现。

	} while (FALSE);

	return bRet;
}

BOOLEAN VMReadVadSubsectPath(PVOID VadSubsection, WCHAR* lpDst) {

	if (!VadSubsection || !lpDst)
		return FALSE;

	VadSubsection = (PVOID)VMReadVirtualPtr(VadSubsection);
	if (!VadSubsection)
		return FALSE;

	union {
		CONTROL_AREA ca;
		FILE_OBJECT fo;
	};

	if (!VMReadVirtual(VadSubsection, ca))
		return FALSE;

	if (!VMReadVirtual((PVOID)(ca.FilePointer.Value & ~0xFll), fo))
		return FALSE;

	if (!fo.FileName.Length) {
		*lpDst = 0;
		return TRUE;
	}

	return VMReadVirtualEx(fo.FileName.Buffer, lpDst, fo.FileName.Length + 2);
}

//BOOLEAN VMFindVmProcessData(CHAR* ProcessName, VM_PROCESS_DATA* VmProcessData) {
BOOL VMEnumProcess(BOOL(*lpCallBack)(PSTR)) {
	UINT64 VadRoot, CR3, PID, SystemProcessActiveProcessLinksAddr;
	PSTR buf, ImageFileName; BOOLEAN status = FALSE; INT i = 0; 
	LIST_ENTRY* p, TempList = { 0 };
	
	buf = nt::valloc(0x1000);
	if (!buf)
		return FALSE;

	SystemProcessActiveProcessLinksAddr = G->SystemProcessEprocess + G->OsActiveProcessLinks;
	p = (LIST_ENTRY*)(G->SystemProcessEprocess + G->OsActiveProcessLinks);
	do {

		//if (i == 69)
		//	i = i;

		xmemset(&TempList, 0, sizeof(LIST_ENTRY));
		if (!VMReadVmVirtualAddr(&TempList, G->MemoryKernelDirbase,
			(DWORD64)p, sizeof(LIST_ENTRY))) {
			DDF("Failed to read LIST_ENTRY at %p\n", p);
			break;
		}

		if (!VMReadVmVirtualAddr(
			buf, G->MemoryKernelDirbase,
			((DWORD64)p - G->OsActiveProcessLinks) +
			0,
			VM_EPROCESS64_MAX_SIZE)) {
			DDF("Failed to read ImageFileName at %p\n", p);
			p = TempList.Flink;
			continue;
		}

		ImageFileName = buf + G->OsImageFileNameOffset;
		CR3 = *(UINT64*)(buf + G->OsDirectoryTableBase);
		VadRoot = *(UINT64*)(buf + G->OsVadRoot);
		PID = *(UINT64*)(buf + G->OsUniqueProcessId);
		// DDS("%d*[%d]%X %llX %s", ++i, PID, CR3, VadRoot, ImageFileName);

		if (lpCallBack && !lpCallBack(buf))
			break;

		if (!TempList.Flink)
			p = p;

		p = TempList.Flink;
	} while (p && p != (LIST_ENTRY*)SystemProcessActiveProcessLinksAddr);

	nt::vfree(buf);
	return status;
}

VOID VMTraverseVadTree(PVOID VadRoot, ULONG Level, BOOL(*lpCallBack)(PSTR, UINT)) {
	if (VadRoot == NULL) return;

	MMVAD Vad;
	if (!VMReadVirtual(VadRoot, Vad)) {
		DbgPrint("Failed to read VAD at %p\n", VadRoot);
		return;
	}

	// 先遍历左子树
	if (Vad.VadNode.Left) {
		VMTraverseVadTree(Vad.VadNode.Left, Level + 1, lpCallBack);
	}

	// 处理当前节点
	//ULONG64 pt = GetProtectFromVad(&Vad.Core);
	//ULONG64 start = GetVadStart(&Vad.Core);
	//ULONG64 end = GetVadEnd(&Vad.Core);
	//ULONG64 size = end - start;

	//if (Vad.Subsection) {

	//	VMReadVadSubsectPath(Vad.Subsection);
	//	auto pCA = (PVOID)VMReadVirtualPtr(Vad.Subsection);

	//	VMReadVirtual(pCA, ca);
	//	VMReadVirtual((PVOID)(ca.FilePointer.Value & ~0xFll), fo);
	//	if (fo.FileName.Length > 0)
	//		VMReadVirtualEx(fo.FileName.Buffer, fullPath, fo.FileName.Length);
	//	fullPath[fo.FileName.Length >> 1] = 0;
	//}

	//if (Level == 0) {
	//	dbk();
	//}
	if (lpCallBack && !lpCallBack((PSTR)&Vad, Level))
		return;

	//DbgPrint("[%u] %016llX - %016llX (%u KB) Ref:%d %08X %s %p %p %S",
	//	Level, start, end, size / 1024, Vad.Core.ReferenceCount,
	//	pt, nt::show_vad_protect(pt), Vad.Subsection, Vad.FileObject, fullPath
	//);

	// 遍历右子树
	if (Vad.VadNode.Right) {
		VMTraverseVadTree(Vad.VadNode.Right, Level + 1, lpCallBack);
	}
}


VOID* VMGetExportsFunAddr(DWORD64 ModuleBaseAddr, PCSTR FunName, BOOLEAN IsFun) {
	void* ret = NULL;
	IMAGE_DOS_HEADER dosHeader = { 0 };
	IMAGE_NT_HEADERS64 ntHeaders = { 0 };
	IMAGE_DATA_DIRECTORY* dataDirectory = NULL;
	char* exportsBuffer = NULL;
	do {
		VMReadVmVirtualAddr(&dosHeader, G->MemoryKernelDirbase, ModuleBaseAddr, sizeof(IMAGE_DOS_HEADER));
		if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
			DDF("%s DOS signature does not match\n", FunName);
			break;
		}

		VMReadVmVirtualAddr(&ntHeaders, G->MemoryKernelDirbase,
			ModuleBaseAddr + dosHeader.e_lfanew, sizeof(IMAGE_NT_HEADERS64));
		if (ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
			DDF("NT header signature does not match\n");
			break;
		}

		dataDirectory =
			ntHeaders.OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_EXPORT;

		exportsBuffer = nt::valloc(dataDirectory->Size + 1);
		memset(exportsBuffer, 0, dataDirectory->Size + 1);
		IMAGE_EXPORT_DIRECTORY* exportsDirectory =
			(IMAGE_EXPORT_DIRECTORY*)exportsBuffer;

		if (!VMReadVmVirtualAddr(exportsBuffer, G->MemoryKernelDirbase,
			ModuleBaseAddr + dataDirectory->VirtualAddress,
			dataDirectory->Size)) {
			DDF("Failed to read exports directory\n");
			break;
		}
		exportsBuffer[dataDirectory->Size] = 0;
		if (!exportsDirectory->NumberOfNames || !exportsDirectory->AddressOfNames) {
			DDF("Zero exports found\n");
			break;
		}

		DWORD exportOffset = dataDirectory->VirtualAddress;
		DWORD* names =
			(DWORD*)(void*)(exportsBuffer + exportsDirectory->AddressOfNames -
				exportOffset);
		if (exportsDirectory->AddressOfNames - exportOffset +
			exportsDirectory->NumberOfNames * sizeof(DWORD) >
			dataDirectory->Size) {
			DDF("Boundary check fail (1)\n");
			break;
		}

		USHORT* ordinals =
			(USHORT*)(void*)(exportsBuffer +
				exportsDirectory->AddressOfNameOrdinals -
				exportOffset);
		if (exportsDirectory->AddressOfNameOrdinals - exportOffset +
			exportsDirectory->NumberOfNames * sizeof(USHORT) >
			dataDirectory->Size) {
			DDF("Boundary check fail (2)\n");
			break;
		}

		DWORD* functions =
			(DWORD*)(void*)(exportsBuffer + exportsDirectory->AddressOfFunctions -
				exportOffset);
		if (exportsDirectory->AddressOfFunctions - exportOffset +
			exportsDirectory->NumberOfFunctions * sizeof(DWORD) >
			dataDirectory->Size) {
			DDF("Boundary check fail (3)\n");
			break;
		}
		for (DWORD i = 0; i < exportsDirectory->NumberOfNames; i++) {
			if (names[i] > dataDirectory->Size + exportOffset ||
				names[i] < exportOffset ||
				ordinals[i] > exportsDirectory->NumberOfNames)
				continue;

			char* exportName = (exportsBuffer + names[i] - exportOffset);
			DWORD64 exportAddress = ModuleBaseAddr + functions[ordinals[i]];
			if (nt::strcmp(exportName, FunName) == 0) {
				if (IsFun) {
					ret = (void*)exportAddress;
				}
				else {
					VMReadVmVirtualAddr(&ret, G->MemoryKernelDirbase, exportAddress,
						sizeof(DWORD64));
				}
				break;
			}
		}

	} while (FALSE);

	if (exportsBuffer) 
		nt::vfree(exportsBuffer);
	
	return ret;
}

//
DWORD64 VMReadVirtualPtr(PVOID VirtualAddress) {
	return VMReadVmVirtualAddr(&VirtualAddress, G->MemoryKernelDirbase, (DWORD64)VirtualAddress, sizeof(DWORD64)) ? (DWORD64)VirtualAddress : 0;
}
BOOL VMReadVirtualEx(PVOID VirtualAddress, PVOID lpDst, UINT Size) {
	return VMReadVmVirtualAddr(lpDst, G->MemoryKernelDirbase, (DWORD64)VirtualAddress, Size);
}
BOOLEAN VMReadVmVirtualAddr(PVOID TargetBuffer, DWORD64 DirectoryTableBase,
	DWORD64 VirtualAddress, SIZE_T Size) {
	BOOLEAN bRet = FALSE;
	BOOLEAN f = TRUE;
	DWORD64 PhyAddr = 0;
	int AlignmentCount = 0;
	DWORD64 va = VirtualAddress;
	DWORD64 tb = (DWORD64)TargetBuffer;
	SIZE_T sz = 0;
	DWORD64 pte = 0;
	do
	{
		if (TargetBuffer == NULL || DirectoryTableBase == 0 ||
			VirtualAddress == 0 || Size == 0) {
			break;
		}

		AlignmentCount = (((VirtualAddress & 0xFFF) + (Size & 0xFFF)) > PAGE_SIZE ? 1 : 0) + (((Size + 0xFFF) & ~0xFFF) / 0x1000);
		for (int i = 0; i < AlignmentCount; i++)
		{
			PhyAddr = VMTranslatePhyAddress(DirectoryTableBase, va, &pte);

			if (!PhyAddr) {
				f = FALSE;
				break;
			}

			// PhyAddr为有效PTE，直接读取,否则则是无效PTE，进入页处理去读取
			//得到虚拟地址的PTE，但这个PTE只决定当前粒度(4k,2m,1g)是有效的
			//假如虚拟地址为：fffff804`4ae53400，size为0x1000，假设当前的PTE粒度为4K的情况
			//那fffff804`4ae53000-fffff804`4ae53FFF是有效的，剩下的0x1000-0x400是跨页的，需要重新判断PTE
			sz = ADDRESS_CALC(va, Size);
			if (VM_X64_PTE_IS_VALID(pte)) {
				//如果PTE有效，直接读取物理地址
				VMReadHostRegion((void*)(tb), PhyAddr, sz);
			}
			else {
				//如果PTE无效，说明当前粒度的页无效，需要进入页处理去读取
				//比如当前粒度为2M，但虚拟地址所在的2M页无效，那么就需要进入页处理去读取，直到找到有效的PTE为止
				VMReadPaged((void*)(tb), DirectoryTableBase, va, PhyAddr, sz);
			}


			tb += sz;
			va += sz;
			Size -= sz;

		}
		if (!f) {
			break;
		}

		bRet = TRUE;

	} while (FALSE);

	return bRet;
}
DWORD64 VMTranslatePhyAddress(
	DWORD64 directoryTableBase,
	DWORD64 virtualAddress,
	PDWORD64 ppte)
{
	if (!directoryTableBase || !ppte|| !virtualAddress)
		return 0;

	*ppte = 0;

	constexpr DWORD64 PAGE_MASK = 0x000FFFFFFFFFF000ULL;

	//
	// canonical check
	//
	LONG64 sign = (LONG64)virtualAddress >> 47;
	if (sign != 0 && sign != -1)
		return 0;

	//
	// CR3 mask
	//
	directoryTableBase &= PAGE_MASK;

	DWORD64 PML4E = 0;
	DWORD64 PDPTE = 0;
	DWORD64 PDE = 0;
	DWORD64 PTE = 0;

	WORD pml4 = (virtualAddress >> 39) & 0x1FF;
	WORD pdpt = (virtualAddress >> 30) & 0x1FF;
	WORD pd = (virtualAddress >> 21) & 0x1FF;
	WORD pt = (virtualAddress >> 12) & 0x1FF;

	//
	// ===== PML4 =====
	//
	if (!VMReadHostRegion(
		&PML4E,
		directoryTableBase + pml4 * 8,
		sizeof(PML4E)))
		return 0;

	if (!VM_X64_PTE_IS_VALID(PML4E))
		return 0;

	//
	// ===== PDPT =====
	//
	if (!VMReadHostRegion(
		&PDPTE,
		(PML4E & PAGE_MASK) + pdpt * 8,
		sizeof(PDPTE)))
		return 0;

	if (!VM_X64_PTE_IS_VALID(PDPTE))
		return 0;

	//
	// ===== 1GB PAGE =====
	//
	if (PDPTE & (1ULL << 7))
	{
		*ppte = PDPTE;

		return (PDPTE & 0x000FFFFFC0000000ULL)
			| (virtualAddress & 0x3FFFFFFFULL);
	}

	//
	// ===== PD =====
	//
	if (!VMReadHostRegion(
		&PDE,
		(PDPTE & PAGE_MASK) + pd * 8,
		sizeof(PDE)))
		return 0;

	if (!VM_X64_PTE_IS_VALID(PDE))
		return 0;

	//
	// ===== 2MB PAGE =====
	//
	if (PDE & (1ULL << 7))
	{
		*ppte = PDE;

		return (PDE & 0x000FFFFFFFE00000ULL)
			| (virtualAddress & 0x1FFFFFULL);
	}

	//
	// ===== PT =====
	//
	if (!VMReadHostRegion(
		&PTE,
		(PDE & PAGE_MASK) + pt * 8,
		sizeof(PTE)))
		return 0;

	if (!VM_X64_PTE_IS_VALID(PTE))
		return 0;

	*ppte = PTE;

	//
	// ===== 4KB PAGE =====
	//
	return (PTE & PAGE_MASK)
		| (virtualAddress & 0xFFF);
}


DWORD64 MemX64Prototype(_In_ DWORD64 pte, _In_ DWORD64 DirectoryTableBase)
{
	DWORD64 PtePage = 0;
	do
	{
		if (!pte || !DirectoryTableBase) {
			break;
		}
		if (!VMReadVmVirtualAddr(&PtePage, DirectoryTableBase, MEM_X64_PTE_PROTOTYPE(pte), 8)) {
			break;
		}

		if (!PtePage) {
			break;
		}

		//如果是原型PTE，则是Subsection Pte,这里暂时没有处理
		//否则就是PTE的Hard/Transition/PageFile 交给外面去处理
		if (MEM_X64_PTE_PROTOTYPE(PtePage)) {
			PtePage = 0;
			break;
		}

	} while (FALSE);

	return PtePage;
}

//从虚拟内存读取“分页”页面
VMM_PTE_TP MemX64TransitionPaged(_In_ DWORD64 va, _In_ DWORD64 pte, _In_ DWORD64 DirectoryTableBase, _In_ DWORD64 Flags, _Out_ PDWORD64 ppa)
{
	DWORD PfNumber = 0;
	DWORD PfOffset = 0;
	DWORD PteTp = VMM_PTE_TP_NA;
	do
	{
		//初始化
		*ppa = 0;
		/*
		有效PTE(硬件PTE)
		PTE设置了有效位，MMU就发挥了作用，并执行到物理地址的转换,这里就不会是分页内存
		*/
		if (MEM_X64_PTE_IS_HARDWARE(pte))
		{
			PteTp = VMM_PTE_TP_HARDWARE;
			*ppa = pte & 0x0000fffffffff000;
			break;
		}
		/*无效pte(软件PTE)--------------------------------------------------------------------------------------------------------------------------------------------
		4种无效PTE和1种特殊无效PTE(原型PTE)

		如果在地址转换过程中遇到的PTE的有效位为零，则PTE表示无效的页，在引用时将引发内存管理异常或页错误。
		MMU忽略PTE的剩余位，因此操作系统可以使用这些位来存储关于页面的信息，这将有助于解决页面错误。
		下面列出了四种无效pte及其结构。这些通常被称为软件pte，因为它们是由内存管理器而不是MMU解释的*/
		/*无效PTE有四种：
		1、在分页文件中
		2、要求0页面
		3、页面转移中
		4、未知，需要检查VAD树
		*/

		/* 原型状态
		   1、相同的物理页面可以在许多不同的进程之间共享。这很容易做到，因为您可以只让多个 PTE 引用同一个物理页面。操作系统的问题是如何协调共享页面的修剪。
		由于有许多对同一物理页面的引用，如果操作系统需要例如将物理页面重新定位到页面文件中，它将需要搜索和更新所有这些引用。
		由于效率非常低，因此 Windows 解决方案是使用一种“符号链接”PTE 将共享页面定向到另一个 PTE - 称为原型 PTE。
		因此，我们只需要更新原型 PTE，所有引用共享内存的 PTE 都将自动更新。

		  2、与硬件PTE 不同，原型 PTE 是内部内存管理器数据结构，CPU 从不使用它来执行虚拟到物理地址转换。它们仅由 Windows 页面错误处理程序用于解决共享页面上的页面错误。

		原型PTE:原型PTE都是4KB的页面，
		根据原型PTE的描述,共享页面可处于下列6种状态之一°
		（l）活动／有效（active／valid）因为其他进程可以访问, 所以该页面位于物理内存中
		（2）转换（transltion目标页面位于内存中的待命列表或修改列表（或不位于这两 个列表中的任—个）内。
		（3）已修改不写出（modihed no write）目标页面位于内存中,且位于已修改不写出列表内）。
		（4）要求零（demandzero）目标页面应当用一个零页面来满足。
		（5）页面文件（pageFile）目标页面驻留在页面文件内
		（6）映射文件（mappedFile）。目标页面驻留在映射文件内
		*/
		if (MEM_X64_PTE_PROTOTYPE(pte)) {
			PteTp = VMM_PTE_TP_PROTOTYPE;
			pte = MemX64Prototype(pte, DirectoryTableBase);
			if (MEM_X64_PTE_IS_HARDWARE(pte)) {
				*ppa = pte & 0x0000fffffffff000;
				break;
			}
		}

		/*过渡状态
		  Windows有一个“工作集修整器” - 一个从进程的工作集中删除页面的组件（工作集在POSIX中更广为人知的是驻留集，但本质上是进程可以访问的所有页面的集合，而不会出错。
	   修整器尝试将页面删除到页面文件中，以增加系统中可用物理页面的总数。但是，不是立即将页面写入页面文件，而是首先将页面置于转换状态。
	   这允许稍后将页面写入页面文件，同时在内存中仍包含有效数据，以防进程稍后需要该页面（它可以快速错误地返回到工作集中）。
	   因此，转换中的页面包含有效数据，但是当进程访问它时，硬件会将pageerror分页到操作系统处理程序中，该处理程序将简单地将页面标记为有效。
	   如果“转换”标志处于打开状态，而“原型”标志处于关闭状态，则页面处于“转换”状态
	   */
		if (MEM_X64_PTE_TRANSITION(pte)) {
			PteTp = VMM_PTE_TP_TRANSITION;
			pte = MEM_X64_PTE_TRANSITION(pte);
			if ((pte & 0x0000fffffffff000)) {
				*ppa = pte & 0x0000fffffffff000;
			}
			break;
		}

		//Todo:目前没有实现读写Read/Write pagefile.sys，但这里先写上
		PfNumber = MEM_X64_PTE_PAGE_FILE_NUMBER(pte);
		PfOffset = MEM_X64_PTE_PAGE_FILE_OFFSET(pte);

		/*VAD原型PTE和VAD硬件PTE

		 VAD原型PTE
		如果硬件 PTE 看起来像原型 PTE（即具有 Valid = 0，Prototype = 1），并且 ProtoAddress 等于特殊值0xFFFFFFFF0000则标记 VAD 原型。
		在这种情况下，我们必须找到与所讨论的虚拟地址相对应的VAD区域。然后，MMVAD 结构包含一系列与整个 VAD 范围相对应的 PTE。
		然后，我们计算原始虚拟地址与VAD区域的相对偏移量，以找到其相应的PTE。

		例如，假设我们尝试解析地址0x10000：
		遍历页表，我们确定VAD原型PTE（即ProtoAddress = 0xFFFFFFFF0000）。
		我们在过程VAD中搜索感兴趣的区域。假设我们找到一个从0x8000到0x20000的区域。
		此区域的_MMVAD对象有一个 FirstPrototypePte 成员（假设它指向 0xFFFF1000000）。
		因此，我们想要的 PTE 位于 （0x10000 - 0x8000） / 0x1000 + 0xFFFF1000000
		我们从该 PTE 解析物理地址。

		 Vad硬件PTE
		如果 PTE 完全为 0，则表示应咨询 VAD。当指向 PTE 的 PDE 无效（即整个页表无效）时，这种情况似乎也是如此。在这种情况下，我们需要以与上述VAD原型PTE相同的方式检查VAD。
		这种状态似乎与上述状态相同。*/

		if (va && !MEM_IS_KERNEL_ADDR_X64(va) && (!pte || (PfOffset == 0xffffffff) && !(Flags & VM_FLAG_NOVAD))) {
			pte = VADFindVadPte(va);
			if (!pte) {
				break;
			}
			if (MEM_X64_PTE_IS_HARDWARE(pte)) {
				PteTp = VMM_PTE_TP_HARDWARE;
				*ppa = pte & 0x0000fffffffff000;
				break;
			}
			PteTp = MemX64TransitionPaged(va, pte, DirectoryTableBase, Flags | VM_FLAG_NOVAD, ppa);
			break;
		}

		if (!pte) {
			break;
		}

		// 要求零（demandzero）目标页面应当用一个零页面来满足[ nt!_MMPTE_SOFTWARE ]
		//（无效PTE的第2种情况和原型PTE的第4种情况）
		if (!PfNumber && !PfOffset) {
			PteTp = VMM_PTE_TP_DEMANDZERO;
			break;
		}

		//WIN11文件支持的内存(目前不支持)
		if (MEM_X64_PTE_IS_FILE_BACKED(pte)) {
			PteTp = VMM_PTE_TP_FILE_BACKED;
			break;
		}

		//接下来就是win10的压缩内存和虚拟内存(PageFile.sys)
	} while (FALSE);

	return (VMM_PTE_TP)PteTp;
}


/*

226C9840000:
sub rsp,28
mov rcx,226C9840200
mov rdx,6 // SECTION_MAP_READ | SECTION_MAP_WRITE,
mov r8,226C9840120
lea rax, [rsp+20]
mov [rax], 4
mov [rax+8],08000000
mov [rax+10],0
call NtCreateSection
add rsp,28
ret


226C9840110:

// OBJECT_ATTRIBUTES
226C9840120:
dq 30 0 226C9840180 40 0 0

// UNICODE STRING
226C9840180:
dw 2C 00 30 00 00 00 00 00
dw '\BaseNamedObjects\VMEM'



			nt::EnumHandlesName(hProc, hPid, +[](PWSTR _, SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX* i) {
				if (nt::wcsstr(_, L".vmem")) {
					DAS("%S", _);
					return true;
				}
				if (!nt::wcsstr(_, L"\\Sessions\\"))
					return true;

				HANDLE hDup = i->HandleValue; // i->HandleValue = 0;
				DAS("%X %S", hDup, _);

				ULONG returnLen;
				SECTION_IMAGE_INFORMATION sii = { 0 };
				if (RR(nt::ntQuerySection(hDup, SectionImageInformation,
					&sii, sizeof(sii), &returnLen))) {

					DDS("%p %llX %X", sii.TransferAddress, sii.ImageFileSize, sii.CommittedStackSize);

					if (sii.ImageFileSize == 0x200000000) {
						DDS("Found Section! Handle: 0x%X, Type: %S",
							i->HandleValue, L"");
						// 找到了！
						return false;  // 停止枚举
					}
				}
				return true;
			});
*/


#if 1

#else
LR"(??\E:\VMACHINE\Windows 10 x64\564dd19c-2ea1-cc75-b9f7-beedebe279f7.vmem)";
LR"(\Device\HarddiskVolume7\VMACHINE\Windows 10 x64\564dd19c-2ea1-cc75-b9f7-beedebe279f7.vmem)";

auto aaa = *(PVOID*)(ptr + 85);
auto aaa1 = *(PVOID*)(ptr + 0x44);

	MEMORY_BASIC_INFORMATION mbi;	// GetMappedFileNameA
																	// \Device\HarddiskVolume7\VMACHINE\Windows 10 x64\564dd19c-2ea1-cc75-b9f7-beedebe279f7.vmem
	if (0)Nt::EnumProcMemory(hProc, PAGE_RW, +[](MEMORY_BASIC_INFORMATION& mbi, PVOID p) {

		UNICODER _;
		nt::QueryProcMemoryName(hProc, mbi.BaseAddress, _);
		//DDS("%p %llX %S", mbi.BaseAddress, mbi.RegionSize, _.Buffer);

		if (mbi.State == MEM_COMMIT && mbi.AllocationProtect == PAGE_READWRITE && nt::wcsstr(_.Buffer + 68, L".vmem") &&// mbi.RegionSize == 0x200000000 &&
			_.Length > sizeof(L"564dd19c-2ea1-cc75-b9f7-beedebe279f7") + sizeof(LR"(\Device\HarddiskVolume7\VMACHINE\)")) {

			HANDLE hSection = NULL;
			DDS("Find the memory region at 0x%p %llX %S",
				mbi.BaseAddress, mbi.RegionSize, _.Buffer
			);

			nt::l->UserPtr = 0;// 200000000~3FFFFFFFF

			#if 1

			int ok = nt::vmx_init_physical_memory(&nt::l->UserPtr, mbi.RegionSize, &_);
			if (ok == -1) {
				ok = nt::vmx_init_physical_memory(&nt::l->UserPtr, mbi.RegionSize, &_);
			}

			dbi(ok);
			
			#else
			
			UNICODERW _1(LR"(\??\E:\VMACHINE\Windows 10 x64\564dd19c-2ea1-cc75-b9f7-beedebe279f7.vmem)");

			// 2. 用 Section 名称打开共享内存
			NTSTATUS status;
			OBJECT_ATTRIBUTES oa;
			IO_STATUS_BLOCK ioStatus;
			HANDLE hSection1, hFile = NULL;
			InitializeObjectAttributes(&oa, &_, OBJ_CASE_INSENSITIVE, NULL, NULL);

			// 3. 打开文件
			status = nt::ntCreateFile(
				&hFile,
				GENERIC_READ | GENERIC_WRITE ,
				&oa,
				&ioStatus,
				NULL,           // 无分配大小
				FILE_ATTRIBUTE_NORMAL,
				FILE_SHARE_READ | FILE_SHARE_WRITE, // 30
				FILE_OPEN,      // 打开已存在的文件
				0,              // 无特殊标志
				NULL,           // 无 EaBuffer
				0               // EaLength
			);
			sizeof("\\BaseNamedObjects\\VMEM");
			oa.ObjectName = NULL;
			status = nt::ntCreateSection(
				&hSection,
				SECTION_MAP_READ | SECTION_MAP_WRITE,
				&oa,
				(PLONGLONG)&mbi.RegionSize,  // 使用内存区域的大小
				PAGE_READWRITE,
				SEC_COMMIT,
				hFile          // 关联文件
			);

			if (NT_SUCCESS(status)) {
				status = nt::ntMapViewOfSection(hSection, HPROC,
					&nt::l->UserPtr,
					0,
					mbi.RegionSize,
					NULL,
					&mbi.RegionSize,
					ViewShare,
					0,
					PAGE_READWRITE
				);
			}
			#endif

			g_vmem = (char*)mbi.BaseAddress;
			xmemcps(p, &mbi);
			return FALSE;
		}
		return TRUE;

		}, &mbi);

#endif