
Our goal is to ensure that virtual machines maintain a healthy amount of unused memory, thereby achieving an efficient memory usage pattern without compromising VM performance.

# Algorithm:
We enumerate Active Domains - Connect to the local hypervisor (qemu:///system) and obtain a list of all active VMs (domains).

We set Memory Stats Period - For each active domain, set the STATS_PERIOD. This determines how frequently the VM's memory statistics will be updated.

We extract Memory Metrics - For each VM, we fetch two key memory statistics:

- Total Allocated Memory, the total memory currently allocated to the VM
- Unused Memory, the amount of memory that the VM currently isn't using.

# Memory Management:
If a VM has more than 100MB of unused memory:
We calculate the amount of memory that can be released. We want to retain at least 100MB of unused memory in each VM
For gradual memory release, we limit the memory release amount to 50MB at a time. This ensures that we don't disrupt the VM's operations by releasing a large amount of memory all at once.
Adjust the VM's total allocated memory by the amount determined to be released.
Host Memory Check: Finally, we inspect the total free memory available on the host. If it's less than or equal to 200MB, output a warning message. This serves as a cautionary measure to ensure that the host isn't running into potential out-of-memory scenarios.

Why keep at least 100MB of unused memory in VMs?
This buffer ensures that sudden memory demands by applications within the VM can be met without requiring immediate memory ballooning operations.

Why release memory gradually?
Suddenly releasing a large chunk of memory could impact VM performance. Gradual release provides a balance, ensuring VM stability while also optimizing memory usage.

Why monitor host memory?
The host's available memory is crucial. If the host runs out of memory, it could start swapping, severely degrading performance for all VMs. A warning at the 200MB threshold provides a timely alert for necessary interventions.

Basically, the algorithm is designed to strike a balance between efficient memory utilization and VM performance. Regular monitoring and adaptive memory allocation enable VMs to operate seamlessly while ensuring optimal resource usage.
