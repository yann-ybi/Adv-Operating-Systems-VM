#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

// encapsulate memory statistics for each virtual machine
typedef struct {
    long long int allocatedMem;  // Total memory allocated to the VM
    long long int availableMem;  // Memory that remains unused in the VM
    long long int consumedMem;   // Memory that's actively used by the VM
} VMStats;

int is_exit = 0; // DO NOT MODIFY THE VARIABLE
size_t startingVM = 0;
long long int *priorConsumedMem = NULL;

void MemoryScheduler(virConnectPtr conn,int interval);

/**
 * Collect memory statistics for a given virtual machine instance
 * @param vmInstance - The virtual machine for which stats are collected
 * @param statsData - Pointer to VMStats structure to populate with statistics.
 * @param pollingTime - The interval for which memory stats are gathered
 */
void collectVMStats(virDomainPtr vmInstance, VMStats* statsData, int interval);

/**
 * Adjust memory allocation for a given virtual machine instance.
 * @param vmInstance - The virtual machine for which memory is adjusted
 * @param memAdjustment - Amount of memory to be adjusted (can be positive or negative).
 */
void modifyMemoryAllocation(virDomainPtr vmInstance, long long int memAdjustment);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open conn\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the conn
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval) {
    virDomainPtr* vmList;
    int activeVMs = virConnectListAllDomains(conn, &vmList, VIR_CONNECT_LIST_DOMAINS_RUNNING);
    VMStats* vmStatsCollection = calloc(activeVMs, sizeof(VMStats));

    unsigned int allVMPeaked = 1;
    long long int* neededMem = calloc(activeVMs, sizeof(long long int));
    long long int* excessMem = calloc(activeVMs, sizeof(long long int));

    if (!priorConsumedMem) priorConsumedMem = calloc(activeVMs, sizeof(long long int));

    // Collect memory stats for all active VMs
    for (size_t k = 0; k < activeVMs; k++) {
        collectVMStats(vmList[k], &vmStatsCollection[k], interval);

        // Initializes priorConsumedMem for VMs
        priorConsumedMem[k] = 0;
        // Determines if VMs need more memory or have excess memory
        if (priorConsumedMem[k] != 0) {
            if (vmStatsCollection[k].consumedMem > priorConsumedMem[k] + 10 && vmStatsCollection[k].availableMem < 200) {
                long long int adjustment = MIN(MAX(200 - vmStatsCollection[k].availableMem, 50), vmStatsCollection[k].allocatedMem);
                neededMem[k] = adjustment;
            } else if (vmStatsCollection[k].consumedMem <= priorConsumedMem[k] && vmStatsCollection[k].availableMem > 100) {
                excessMem[k] = MIN(50, vmStatsCollection[k].availableMem - 100);
                allVMPeaked = 0;
            }
        }
        priorConsumedMem[k] = vmStatsCollection[k].consumedMem;
    }

    // Adjusts memory allocations based on the VM needs
    long long int nodeFreeMem = virNodeGetFreeMemory(conn) / 1024;
    if (allVMPeaked && nodeFreeMem > 200) {
        modifyMemoryAllocation(vmList[startingVM], nodeFreeMem - 200);
    } else {
        for (size_t k = 0; k < activeVMs; k++) {
            if (neededMem[k] > 0) {
                modifyMemoryAllocation(vmList[k], neededMem[k]);
            } 
        }
        for (size_t k = 0; k < activeVMs; k++) {
            if (excessMem[k] > 0) {
                modifyMemoryAllocation(vmList[k], -excessMem[k]);
            }
        }
    }

    startingVM = (startingVM + 1) % activeVMs;
    free(vmStatsCollection);
    free(neededMem);
    free(excessMem);
}

void collectVMStats(virDomainPtr vmInstance, VMStats* statsData, int interval) {
    virDomainMemoryStatPtr memoryStats = malloc(14 * sizeof(virDomainMemoryStatStruct));
    virDomainSetMemoryStatsPeriod(vmInstance, interval, VIR_DOMAIN_AFFECT_LIVE);
    virDomainMemoryStats(vmInstance, memoryStats, 14, 0);

    for (size_t j = 0; j < 14; j++) {
        if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
            statsData->allocatedMem = memoryStats[j].val / 1024;
        }
        if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) {
            statsData->availableMem = memoryStats[j].val / 1024;
        }
    }

    statsData->consumedMem = statsData->allocatedMem - statsData->availableMem;
    free(memoryStats);
}

void modifyMemoryAllocation(virDomainPtr vmInstance, long long int memAdjustment) {
    virDomainInfo vmInfo;
    virDomainGetInfo(vmInstance, &vmInfo);
	long long int adjustedMemory = (vmInfo.memory / 1024) + memAdjustment;
	if (adjustedMemory > (vmInfo.maxMem / 1024)) {
		adjustedMemory = vmInfo.maxMem / 1024;
	}
	virDomainSetMemory(vmInstance, adjustedMemory * 1024);
}
