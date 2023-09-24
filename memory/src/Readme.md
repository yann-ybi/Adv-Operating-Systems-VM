# MemoryScheduler Algorithm

## Introduction:
The MemoryScheduler function is designed to dynamically adjust the memory allocation of active VMs running on a host system. The function takes into account various constraints to ensure optimal performance for both the VMs and the host

## Algorithm Overview:
Data Collection:

We fetch the list of all active VMs (domains)
For each VM, collect memory statistics such as the total balloon memory and the unused memory.
Calculate available host memory.

### Memory Need Assessment:
If a VM's unused memory is below a threshold (DOMAIN_MIN_UNUSED_MEMORY), calculate the additional memory it requires.
If a VM's unused memory exceeds an upper threshold (EXCESS_THRESHOLD), determine how much memory can be released without dropping below a safe threshold.

### Memory Reallocation:
Before any reallocation, ensure that the VM's and the host's memory after the adjustment will stay within safe boundaries.
For VMs needing additional memory:
First, try to reallocate memory from VMs that have excess memory to spare.
If still more memory is needed and the host can provide it without dropping below its own safety threshold, allocate from the host.
Safety Constraints:

A VM will not release memory if its unused memory is <= 100MB (DOMAIN_MIN_UNUSED_MEMORY).
Memory releases from VMs are gradual, no more than a fixed amount (MAX_MEMORY_RELEASE) at once.
The host will not allocate memory to VMs if it will be left with <= 200MB (HOST_MIN_UNUSED_MEMORY) of available memory.

## Detailed Steps:
### Initialization:
Connect to the host system's hypervisor.
Set memory statistic intervals for each VM for accurate data collection.
Initialize data structures for storing VM statistics and memory allocation needs.
Memory Assessment:

### For each VM, check its unused memory:
If below DOMAIN_MIN_UNUSED_MEMORY, compute how much more it requires without exceeding the VM's maximum memory limit.
If above EXCESS_THRESHOLD and the VM can release more than MAX_MEMORY_RELEASE without dropping below the safety threshold, determine the amount it can release.

### Memory Re-Allocation Loop:
For each VM pair (i, j) where VM i needs more memory and VM j has excess memory to release:
Transfer memory from j to i, ensuring the transfer amount respects the MAX_MEMORY_RELEASE constraint.
Update the VMs' current memory and their memory needs.
For VMs that still need more memory, allocate from the host if the host has enough memory to spare without violating the HOST_MIN_UNUSED_MEMORY constraint.
Finalization:

Free dynamically allocated memory to prevent memory leaks.

## Conclusion:
The MemoryScheduler function, when called periodically, ensures that VMs get the memory they require while maintaining a balance to ensure the stability and performance of both the VMs and the host system.
