# Memory Scheduler for VMs

The Memory Scheduler is designed to dynamically adjust the allocated memory of virtual machines (VMs) based on their actual needs. It ensures efficient memory utilization while protecting both the host and VMs from running out of memory.

## Initialization:

Connect to the local hypervisor (qemu:///system).
Fetch the list of all active VMs/domains.
Determine the available memory on the host.
Gathering VM Memory Metrics:

## Algorithm:

### For each active VM:
We set the memory stats period to collect detailed statistics.
We fetch the current memory details like balloon memory and unused memory.
We calculate the actual memory in use.
We determine if the VM has excess memory (above a 100MB threshold) that can be released.
We determine if the VM needs additional memory (below a 200MB threshold).

### Memory Redistribution Among VMs:
For VMs that require additional memory:
We look for other VMs that have excess memory.
We reallocate the surplus memory from these VMs to the ones in need.
We ensure that the transfer of memory does not exceed 50MB at once, to achieve a gradual memory release.

### Allocating Free Memory from Host:
If the VMs still require additional memory and the host has more than a 200MB threshold of available memory:
We allocate the required memory from the host to the VM, ensuring the host retains at least 200MB.

### Clean-Up:
Release all dynamically allocated resources.

The Memory Scheduler effectively manages memory distribution among VMs, ensuring optimal performance for both VMs and the host. The algorithm ensures fairness, efficiency, and system stability.
