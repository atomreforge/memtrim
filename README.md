# memtrim
a simple C tool to spare ram space. designed for windows

## implementation
the core of the program is `NtSetSystemInformation`, a native api in `ntdll.dll` that serves as a bridge between user mode and kernel mode.

```C
PFN_NtSetSystemInformation ntSet = 
    (PFN_NtSetSystemInformation)GetProcAddress(ntdll, "NtSetSystemInformation");
```

## usage
|step|command value|operation|
|:---|:---|:---|
|empty working sets|`MEM_EMPTY_WORKINGSETS` (2)|strip inactive physical pages from all process working sets
|flush modified pages|`MEM_FLUSH_MODIFIED` (3)|forces all dirty pages in the modified list to be written to disk|
|purge standby list|`MEM_PURGE_STANDBY` (4)|directly discards or zeroes pages in the standby list|
