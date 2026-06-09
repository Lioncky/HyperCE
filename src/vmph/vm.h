
#define VM_X64_PTE_IS_VALID(pte)    (pte & 0x01)

#define ADDRESS_CALC_ALIGNMENT(address) ((address & 0xFFF)? PAGE_SIZE-(address & 0xFFF):PAGE_SIZE) 
#define ADDRESS_CALC(address,size) (size > PAGE_SIZE ? ADDRESS_CALC_ALIGNMENT(address) : size)

#define MEM_IS_KERNEL_ADDR_X64(va)  (((va) & 0xffff800000000000) == 0xffff800000000000)
#define VM_EPROCESS64_MAX_SIZE 0x800 

typedef enum VMM_PTE_TP {
	VMM_PTE_TP_NA = 0,
	VMM_PTE_TP_HARDWARE = 1,
	VMM_PTE_TP_TRANSITION = 2,
	VMM_PTE_TP_PROTOTYPE = 3,
	VMM_PTE_TP_DEMANDZERO = 4,
	VMM_PTE_TP_COMPRESSED = 5,
	VMM_PTE_TP_PAGEFILE = 6,
	VMM_PTE_TP_FILE_BACKED
} VMM_PTE_TP, * PVMM_PTE_TP;

typedef struct _NT_PROCESS_DATA
{
	DWORD ProcessSize;
	DWORD64 MemoryKernelDirbase; //CR3
	DWORD64 MemoryKernelEntry;   //UEFI KernelEntry:hal!HalpLMStub
	DWORD64 MemoryKernelBase;    //NT
	DWORD64 SystemProcessEprocess; //System进程的EPROCESS
	PVOID PsLoadedModuleListPtr;

}NT_PROCESS_DATA, * PNT_PROCESS_VERSION_DATA;

//目标进程的数据
typedef struct _VM_PROCESS_DATA
{
	DWORD64 PEB;
	DWORD PEB32;      // WoW64 only
	DWORD64 DestProcessEprocess;
	ULONGLONG  DestProcessCr3;
	DWORD64 VadRoot;

}VM_PROCESS_DATA, * PVM_PROCESS_DATA;

BOOLEAN VMNtKernelDataInit();
BOOL VMEnumProcess(BOOL(*lpCallBack)(PSTR));
VOID VMTraverseVadTree(PVOID VadRoot, ULONG Level, BOOL(*lpCallBack)(PSTR, UINT));

DWORD64 VMTranslatePhyAddress(DWORD64 directoryTableBase, DWORD64 virtualAddress, PDWORD64 ppte);
VOID* VMGetExportsFunAddr(DWORD64 ModuleBaseAddr, PCSTR FunName, BOOLEAN IsFun);
BOOLEAN VMReadVmVirtualAddr(PVOID TargetBuffer, DWORD64 DirectoryTableBase, DWORD64 VirtualAddress, SIZE_T Size);
DWORD64 VMReadVirtualPtr(PVOID VirtualAddress);
BOOL VMReadVirtualEx(PVOID VirtualAddress, PVOID lpDst, UINT Size);
template <typename T> BOOL VMReadVirtual(PVOID VirtualAddress, T& lpDst) { return VMReadVirtualEx(VirtualAddress, &lpDst, sizeof(T)); }
VMM_PTE_TP MemX64TransitionPaged(_In_ DWORD64 va, _In_ DWORD64 pte, _In_ DWORD64 DirectoryTableBase, _In_ DWORD64 Flags, _Out_ PDWORD64 ppa);

BOOLEAN VMGetWinX64ProcessOffset();
BOOLEAN VMGetWinX64ProcessOffsetDefault();

#define VM_FLAG_NOVAD  0x00000001  // 不嵌套VAD了

#define PTE_SWIZZLE_MASK                               0x0         //Todo:暂时不支持
#define PTE_SWIZZLE_BIT                                0x10        // nt!_MMPTE_SOFTWARE.SwizzleBit
#define MEM_X64_PTE_IS_HARDWARE(pte)                  (pte & 0x01)
#define MEM_X64_MEMMAP_DISPLAYBUFFER_LINE_LENGTH      89

#define MEM_X64_PTE_PAGE_FILE_NUMBER(pte)          ((pte >> 12) & 0x0f)
#define MEM_X64_PTE_PAGE_FILE_OFFSET(pte)          ((pte >> 32) ^ (!(pte & PTE_SWIZZLE_BIT) ? PTE_SWIZZLE_MASK : 0))

#define MEM_X64_PTE_IS_FILE_BACKED(pte)            (((pte & 0xffff80000000000f) == 0xffff800000000000))

/*
nt!_MMPTE_PROTOTYPE
+ 0x000 Valid            : Pos 0, 1 Bit
+ 0x000 DemandFillProto : Pos 1, 1 Bit
+ 0x000 HiberVerifyConverted : Pos 2, 1 Bit
+ 0x000 ReadOnly : Pos 3, 1 Bit
+ 0x000 SwizzleBit : Pos 4, 1 Bit
+ 0x000 Protection : Pos 5, 5 Bits
+ 0x000 Prototype : Pos 10, 1 Bit
+ 0x000 Combined : Pos 11, 1 Bit
+ 0x000 Unused1 : Pos 12, 4 Bits
+ 0x000 ProtoAddress : Pos 16, 48 Bits
*/
#define MEM_X64_PTE_PROTOTYPE(pte)                    (((pte & 0x8000000000070401) == 0x8000000000000400) ? ((pte >> 16) | 0xffff000000000000) : 0)

/*
nt!_MMPTE_TRANSITION
+ 0x000 Valid            : Pos 0, 1 Bit
+ 0x000 Write : Pos 1, 1 Bit
+ 0x000 Spare : Pos 2, 1 Bit
+ 0x000 IoTracker : Pos 3, 1 Bit
+ 0x000 SwizzleBit : Pos 4, 1 Bit
+ 0x000 Protection : Pos 5, 5 Bits
+ 0x000 Prototype : Pos 10, 1 Bit
+ 0x000 Transition : Pos 11, 1 Bit
+ 0x000 PageFrameNumber : Pos 12, 36 Bits
+ 0x000 Unused : Pos 48, 16 Bits
*/
#define MEM_X64_PTE_TRANSITION(pte)                   (((pte & 0x0c01) == 0x0800) ? ((pte & 0xffffdffffffff000) | 0x005) : 0)
#define MEM_X64_PTE_IS_TRANSITION(H, pte, iPML)       ((((pte & 0x0c01) == 0x0800) && (iPML == 1) && (H->vmm.tpSystem == VMM_SYSTEM_WINDOWS_X64)) ? ((pte & 0xffffdffffffff000) | 0x005) : 0)
#define MEM_X64_PTE_IS_VALID(pte, iPML)               (pte & 0x01)

