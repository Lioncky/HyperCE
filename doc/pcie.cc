unsigned __int64 sub_1A8CB0()
{
  unsigned int v0; // ebx
  unsigned int v1; // eax
  int v2; // ebx
  unsigned int pciHole; // ebx
  unsigned int v5; // [rsp+20h] [rbp-18h] BYREF

  v0 = readConfigInt_9CA50(0, "pciHole.start");
  v1 = readConfigInt_9CA50(0, "pciHole.dynStart");
  dword_13B9C78 = v1;
  if ( v1 )
  {
    if ( v0 && v0 < v1 )
      v1 = v0;
    v0 = v1;
  }
  if ( !v0 )
  {
    if ( get_launch_data("pciHole.use1GB") )
    {
      v2 = -1024;
    }
    else
    {
      v2 = -256;
      if ( get_launch_data("pciHole.use512MB") )
        v2 = -512;
    }
    v0 = v2 + 4096;
  }
  pciHole = v0 & 0xFFFFFFFC;
  if ( (unsigned __int8)sub_161D30(&v5, 0LL, 0LL) && v5 >= 4 && v5 < pciHole )
  {
    pciHole = v5 & 0xFFFFFFFC;
    sub_5FE1E0("Automatically adjusting PCI hole to %u MB for passthrough RMRR\n", v5 & 0xFFFFFFFC);
  }
  if ( pciHole < 0x100 )
    sub_5FFC90("Large PCI hole leaves < 256 MB low memory, may not be enough for guest OS boot.\n");
  if ( pciHole < 4 )
    sub_92CF0("VERIFY %s:%d\n", "bora\\devices\\misc\\chipset.c", 756);
  if ( pciHole > 0xFE0 )
    sub_92CF0("VERIFY %s:%d\n", "bora\\devices\\misc\\chipset.c", 762);
  _mm_lfence();
  return (unsigned __int64)pciHole << 8;
}

//  pciHole.start = "3172"  pciHole.end = "4096"
/*
优先级：
1. pciHole.start 手动设置
2. pciHole.dynStart 动态计算
3. 都没有则默认：
   - 默认8G pciHole.use1GB  = TRUE → 4096 - 1024 = 3072 MB (3GB)
   - pciHole.use512MB = TRUE → 4096 - 512  = 3584 MB (3.5GB)

最后 v3 << 8 是转换单位 MB → 实际地址
3072 MB << 8 = 0xC0000000  ← 正好是 3GB
*/