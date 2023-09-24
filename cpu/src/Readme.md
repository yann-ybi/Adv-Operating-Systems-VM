### Implementation of a CPU scheduler for virtual machines using the libvirt library. 

Balancing CPU loads across different VMs by adjusting the VM-to-physical CPU mapping dynamically.

# Initialize:

Connect to the QEMU hypervisor using the libvirt API.
Listen for interrupt signals and terminate the program gracefully upon receiving them

# Algorithm:

We repeatedly run the CPUScheduler function at a user-defined interval to check CPU usage and adjust CPU pins.
There is a sleep for the duration of the interval before repeating the function.
CPUScheduler Logic:

a. Gathering Data:
- We retrieve all running VMs.
- We get information about the host (number of physical CPUs)
- For each VM, we gather current CPU usage and existing CPU mapping
- If it's the first iteration, we store the current usage values for comparison in the next interval and return.

b. Compute Utilization:
- We transform the VCPU time into VCPU usage in % form for each VM.
- We track the overall CPU utilization.

c. Balance Check:
- We calculate the mean and standard deviation of CPU utilizations across all physical CPUs.
- If the standard deviation is below 5% , we consider the loads balanced and return without making changes.

d. Rebalancing:
- If imbalances are detected:
- We sort VMs based on CPU usage.
- We reassign VMs to physical CPUs in a manner that balances the load.
- We update the physical CPU to which each VM's VCPUs are pinned.

# Cleanup:

After each iteration of CPUScheduler, we deallocate any dynamically allocated resources.
We close the connection to the QEMU hypervisor.

Supporting Functions:
convertSecondsToNanoseconds: Converts a duration in seconds to nanoseconds.

computeDomainUtilization: Calculates the utilization of a VM's CPU as a percentage using current and previous CPU times and the time interval.

updateDomainAndCPUUtilization: Updates the utilization list for physical CPUs and the load list for VMs.

calculateMean: Computes the mean of an array of data.

calculateStandardDeviation: Calculates the standard deviation of an array of data based on the mean.

compareDomains: A comparison function used for sorting VMs based on their CPU usage.

findMinIndex: Finds the index of the minimum value in an array.

cleanup: Deallocates dynamically allocated resources after each iteration.
