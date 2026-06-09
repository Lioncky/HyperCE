#pragma once
#ifndef _NT_VAD_H_
# define _NT_VAD_H_

// 从 VAD 标志解析保护属性
inline ULONG GetProtectFromVad(PMMVAD_SHORT pVad) {
	return ((pVad->u0) & 0xFF00) >> 8;
}

// 获取 VAD 起始地址（支持 64 位）
inline ULONG64 GetVadStart(PMMVAD_SHORT pVad) {
	ULONG64 start = pVad->StartingVpn;
	start |= ((ULONG64)pVad->StartingVpnHigh) << 32;
	return start << PAGE_SHIFT;
}

// 获取 VAD 结束地址（不包含）
inline ULONG64 GetVadEnd(PMMVAD_SHORT pVad) {
	ULONG64 end = pVad->EndingVpn;
	end |= ((ULONG64)pVad->EndingVpnHigh) << 32;
	return (end + 1) << PAGE_SHIFT;
}

// 获取 VAD 区域大小
inline ULONG64 GetVadSize(PMMVAD_SHORT pVad) {
	return GetVadEnd(pVad) - GetVadStart(pVad);
}
//
//ULONG GetProtectFromVad(PMMVAD_SHORT pVad);
//ULONG GetTypeFromVad(PMMVAD pVad);
//VOID GetVadInfo(PMMVAD pVad, PVAD_INFO Info);
//VOID EnumVadTree(PRTL_BALANCED_NODE Node, PVAD_CALLBACK Callback, PVOID Context);
#endif // _NT_VAD_H_