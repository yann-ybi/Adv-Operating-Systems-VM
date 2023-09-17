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

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

// Main scheduler to distribute memory across all active domains in the system at regular intervals.
void MemoryScheduler(virConnectPtr conn, int interval);

//  Gathers the memory usage statistics for a single domain
void GatherDomainMemoryStats(virDomainPtr domain, struct domainMemUsage* memoryUsage);

// Analyzes the memory demand for a domain based on its current and previous memory usage
void AnalyzeDomainMemoryDemand(struct domainMemUsage* currentUsage, struct domainMemUsage* previousUsage, long* memoryChangeRate, int* needsMoreMemory, int* isIdle, long minMemoryWorkload, long unusedMemoryThreshold);

// Reclaims memory from domains that have excess unused memory
void HandleMemoryReclaimation(virDomainPtr domain, struct domainMemUsage* memoryUsage, int memoryStep, long minimumActualMemory, long minimumUnusedMemory);

// Allocates additional memory to domains that require it, based on available host memory
void AllocateMemoryToDomains(virDomainPtr* domains, struct domainMemUsage* memoryUsages, int domainCount, int* needsMoreMemoryFlags, long hostMinMemory, int memoryStep, long maxAllowedMemory);

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
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

void GatherDomainMemoryStats(virDomainPtr domain, struct domainMemUsage* memoryUsage) {
    virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
    unsigned int numStats = virDomainMemoryStats(domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);

    if (numStats  == -1) {
        printf("Error getting memory stats for domain.\n");
        return;
    }

    for (size_t k = 0; k < numStats ; k++) {
        if (stats[k].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) {
            memoryUsage->unused = stats[k].val / 1024;
        }
        if (stats[k].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
            memoryUsage->actual = stats[k].val / 1024;
        }
    }
}

void AnalyzeDomainMemoryDemand(struct domainMemUsage* currentUsage, struct domainMemUsage* previousUsage, long* memoryChangeRate, int* needsMoreMemory, int* isIdle, long minMemoryWorkload, long unusedMemoryThreshold) {
    long changeInUnused = (previousUsage->unused - currentUsage->unused);
    long changeInActual = (previousUsage->actual - currentUsage->actual);

    if ((changeInUnused > 5 && changeInActual == 0) || (changeInActual < 0 && previousUsage->memDemandingBool)) {
        currentUsage->memDemandingBool = 1;
        *memoryChangeRate = changeInUnused;
        if (currentUsage->unused < minMemoryWorkload) {
            *needsMoreMemory = 1;
        }
    }

    if (currentUsage->unused > unusedMemoryThreshold && previousUsage->unused > unusedMemoryThreshold && changeInUnused < 5) {
        currentUsage->memFreeCount = previousUsage->memFreeCount + 1;
        if (currentUsage->memFreeCount == 3) {
            *isIdle = 1;
            currentUsage->memFreeCount = 0;
        }
    } else {
        currentUsage->memFreeCount = 0;
    }
}

void HandleMemoryReclaimation(virDomainPtr domain, struct domainMemUsage* memoryUsage, int memoryStep, long minimumActualMemory, long minimumUnusedMemory) {
    if (memoryUsage->memDemandingBool == 0 && memoryUsage->actual > minimumActualMemory && memoryUsage->unused > minimumUnusedMemory) {
        int memDecrement = MIN(memoryStep, memoryUsage->unused - minimumUnusedMemory);
        if (virDomainSetMemory(domain, MAX((memoryUsage->actual - memDecrement), minimumActualMemory) * 1024) == -1) {
            printf("Failed to reclaim memory from domain.\n");
        }
    }
}

void AllocateMemoryToDomains(virDomainPtr* domains, struct domainMemUsage* memoryUsages, int domainCount, int* needsMoreMemoryFlags, long hostMinMemory, int memoryStep, long maxAllowedMemory) {
    long memAvailableTotal = (virNodeGetFreeMemory(domains[0]) / 1024) / 1024; // Convert to MB
    if (memAvailableTotal > hostMinMemory) {
        unsigned long long memPartition = (memAvailableTotal - hostMinMemory) / domainCount;
        memPartition = MIN(memPartition, memoryStep);
        for (size_t i = 0; i < domainCount; i++) {
            if (needsMoreMemoryFlags[i] && memoryUsages[i].actual <= maxAllowedMemory) {
                if (virDomainSetMemory(domains[i], MIN(memoryUsages[i].actual + memPartition, maxAllowedMemory) * 1024) == -1) {
                    printf("Failed to allocate memory to domain[%ld].\n", i);
                }
            }
        }
    }
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval) {
    int memoryStep = 100, minimumActualMemory = 200, unusedMemoryThreshold = 400, minMemoryWorkload = 2 * 100, maxAllowedMemory = 2048, hostMinMemory = 200;
    virDomainPtr* domains;
    unsigned int domainFilterFlags = VIR_CONNECT_LIST_DOMAINS_RUNNING;
    int domainCount = virConnectListAllDomains(conn, &domains, domainFilterFlags);

    if (domainCount < 0) {
        printf("Failed to get list of domains.\n");
        return;
    }

    struct domainMemUsage* currUsage = calloc(sizeof(struct domainMemUsage), domainCount);
    int needMoreMemoryFlags[domainCount], isIdle[domainCount];
    long memoryChangeRate[domainCount];

    for (size_t i = 0; i < domainCount; i++) {
        virDomainSetMemoryStatsPeriod(domains[i], interval, VIR_DOMAIN_AFFECT_LIVE);
        GatherDomainMemoryStats(domains[i], &currUsage[i]);

        needMoreMemoryFlags[i] = 0;
        memoryChangeRate[i] = 0;
        isIdle[i] = 0;
        currUsage[i].memDemandingBool = 0;

        if (prevUsage) {
            AnalyzeDomainMemoryDemand(&currUsage[i], &prevUsage[i], &memoryChangeRate[i], &needMoreMemoryFlags[i], &isIdle[i], minMemoryWorkload, unusedMemoryThreshold);
        }

        if (isIdle[i] && currUsage[i].actual > minimumActualMemory) {
            HandleMemoryReclaimation(domains[i], &currUsage[i], memoryStep, minimumActualMemory, 100);
        }
    }

    if (prevUsage) {
        int domainCountNeedMem = 0;
        for (size_t i = 0; i < domainCount; i++) {
            domainCountNeedMem += needMoreMemoryFlags[i];
        }

        if (domainCountNeedMem) {
            HandleMemoryReclaimation(domains[domainCountNeedMem], &currUsage[domainCountNeedMem], memoryStep, minimumActualMemory, 100);
            AllocateMemoryToDomains(domains, currUsage, domainCount, needMoreMemoryFlags, hostMinMemory, memoryStep, maxAllowedMemory);
        }
    } else {
        prevUsage = currUsage;
        return;
    }

    free(prevUsage);
    prevUsage = currUsage;
}
