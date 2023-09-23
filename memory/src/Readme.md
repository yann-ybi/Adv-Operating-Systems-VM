
## Project Instruction Updates:

1. Complete the function MemoryScheduler() in memory_coordinator.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./memory_coordinator <interval>`

### Notes:

1. Make sure that the VMs and the host has sufficient memory after any release of memory.
2. Memory should be released gradually. For example, if VM has 300 MB of memory, do not release 200 MB of memory at one-go.
3. Domain should not release memory if it has less than or equal to 100MB of unused memory.
4. Host should not release memory if it has less than or equal to 200MB of unused memory.
5. While submitting, write your algorithm and logic in this Readme.

Our goal is to ensure that virtual machines maintain a healthy amount of unused memory, thereby achieving an efficient memory usage pattern without compromising VM performance.

Algorithm:
We enumerate Active Domains - Connect to the local hypervisor (qemu:///system) and obtain a list of all active VMs (domains).

We set Memory Stats Period - For each active domain, set the STATS_PERIOD. This determines how frequently the VM's memory statistics will be updated.

We extract Memory Metrics - For each VM, we fetch two key memory statistics:

- Total Allocated Memory, the total memory currently allocated to the VM
- Unused Memory, the amount of memory that the VM currently isn't using.

Memory Management:
If a VM has more than 100MB of unused memory:
We calculate the amount of memory that can be released. We want to retain at least 100MB of unused memory in each VM
For gradual memory release, limit the memory release amount to 50MB at a time. This ensures that we don't disrupt the VM's operations by releasing a large amount of memory all at once.
Adjust the VM's total allocated memory by the amount determined to be released.
Host Memory Check: Finally, inspect the total free memory available on the host. If it's less than or equal to 200MB, output a warning message. This serves as a cautionary measure to ensure that the host isn't running into potential out-of-memory scenarios.

Why keep at least 100MB of unused memory in VMs?
This buffer ensures that sudden memory demands by applications within the VM can be met without requiring immediate memory ballooning operations.

Why release memory gradually?
Suddenly releasing a large chunk of memory could impact VM performance. Gradual release provides a balance, ensuring VM stability while also optimizing memory usage.

Why monitor host memory?
The host's available memory is crucial. If the host runs out of memory, it could start swapping, severely degrading performance for all VMs. A warning at the 200MB threshold provides a timely alert for necessary interventions.

Basically, the algorithm is designed to strike a balance between efficient memory utilization and VM performance. Regular monitoring and adaptive memory allocation enable VMs to operate seamlessly while ensuring optimal resource usage.

You can include this section in your README before the code snippets or after the project instructions, as you see fit.





