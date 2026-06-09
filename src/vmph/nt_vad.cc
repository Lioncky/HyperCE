#include <WinNe.h>
#include "hyperce.h"
#include "nt_vad.h"


// 判断内存类型
ULONG GetTypeFromVad(PMMVAD pVad) {
	// 有 FileObject 且非空 -> 镜像映射（通常是 EXE/DLL）
	if (pVad->FileObject != NULL) {
		return MEM_IMAGE;
	}

	// 有 Subsection 但没有 FileObject -> 映射内存（共享内存）
	if (pVad->Subsection != NULL) {
		return MEM_MAPPED;
	}

	// 其他情况 -> 私有内存（堆、栈等）
	return MEM_PRIVATE;
}

// 获取完整 VAD 信息
VOID GetVadInfo(PMMVAD pVad, PVAD_INFO Info) {
	RtlZeroMemory(Info, sizeof(VAD_INFO));

	Info->StartAddress = GetVadStart(&pVad->Core);
	Info->EndAddress = GetVadEnd(&pVad->Core);
	Info->RegionSize = Info->EndAddress - Info->StartAddress;
	Info->Protection = GetProtectFromVad(&pVad->Core);
	Info->Type = GetTypeFromVad(pVad);
	Info->State = MEM_COMMIT;  // VAD 中的都是已提交内存
	Info->ReferenceCount = pVad->Core.ReferenceCount;
	Info->FileObject = pVad->FileObject;

	// 解析访问权限
	Info->IsReadable = (Info->Protection & (PAGE_READONLY | PAGE_READWRITE |
		PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
	Info->IsWriteable = (Info->Protection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
	Info->IsExecutable = (Info->Protection & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
		PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// 递归枚举 VAD 树（中序遍历）
VOID EnumVadTree(PRTL_BALANCED_NODE Node, PVAD_CALLBACK Callback, PVOID Context) {
	if (Node == NULL) return;

	// 获取 VAD 节点
	PMMVAD pVad = CONTAINING_RECORD(Node, MMVAD, Core.VadNode);

	// 遍历左子树
	EnumVadTree(Node->Left, Callback, Context);

	// 处理当前节点
	if (Callback) {
		VAD_INFO Info;
		GetVadInfo(pVad, &Info);
		Callback(&Info, Context);
	}

	// 遍历右子树
	EnumVadTree(Node->Right, Callback, Context);
}
