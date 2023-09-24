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
size_t startingVM = 0;
long long int *priorConsumedMem = NULL;

void MemoryScheduler(virConnectPtr conn,int interval);

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
    virDomainPtr *activeDomains;
    virDomainInfoPtr domainInfo = malloc(sizeof(virDomainInfo));
    virDomainMemoryStatPtr memoryStats = malloc(14 * sizeof(virDomainMemoryStatStruct));

    int domainCount = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

    if (!priorConsumedMem) priorConsumedMem = calloc(domainCount, sizeof(long long int));

    long long int *currentAllocations = calloc(domainCount, sizeof(long long int)), 
                  *demands = calloc(domainCount, sizeof(long long int)), 
                  *surplus = calloc(domainCount, sizeof(long long int));
    
    int allDomainsMaxed = 1;

    for (int i = 0; i < domainCount; i++) {
        virDomainSetMemoryStatsPeriod(activeDomains[i], interval, VIR_CONNECT_AFFECT_LIVE);
        virDomainGetInfo(activeDomains[i], domainInfo);
        virDomainMemoryStats(activeDomains[i], memoryStats, 14, 0);

        long long int currAlloc, unused, inUse;
        for (int j = 0; j < 14; j++) {
            if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
                currAlloc = memoryStats[j].val / 1024;
            else if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
                unused = memoryStats[j].val / 1024;
        }

        inUse = currAlloc - unused;
        if (priorConsumedMem[i]) {
            demands[i] = inUse > priorConsumedMem[i] + 10 && unused < 200 
                       ? MIN(MAX(200 - unused, 50), domainInfo->maxMem / 1024 - currAlloc) : 0;
            surplus[i] = inUse <= priorConsumedMem[i] && unused > 100 ? MIN(50, unused - 100) : 0;
            if (!surplus[i]) allDomainsMaxed = 0;
        }

        currentAllocations[i] = currAlloc;
        priorConsumedMem[i] = inUse;
    }

    for (int i = 0; i < domainCount; i++) {
        int demandingIdx = (i + startingVM) % domainCount;
        if (!demands[demandingIdx]) continue;

        if (!allDomainsMaxed) {
            for (int j = 0; j < domainCount; j++) {
                int supplyIdx = (j + startingVM) % domainCount;
                if (!demands[demandingIdx] || !surplus[supplyIdx]) continue;

                long long int transfer = MIN(surplus[supplyIdx], demands[demandingIdx]);
                virDomainSetMemory(activeDomains[supplyIdx], (currentAllocations[supplyIdx] - transfer) * 1024);
                virDomainSetMemory(activeDomains[demandingIdx], (currentAllocations[demandingIdx] + transfer) * 1024);
                demands[demandingIdx] -= transfer;
                surplus[supplyIdx] -= transfer;
            }
        } else {
            long long int freeMem = virNodeGetFreeMemory(conn) / 1024;
            if (freeMem <= 200) continue;

            long long int transfer = MIN(50, MIN(demands[demandingIdx], freeMem - 200));
            virDomainSetMemory(activeDomains[demandingIdx], (currentAllocations[demandingIdx] + transfer) * 1024);
            demands[demandingIdx] -= transfer;
        }
    }

    startingVM = (startingVM + 1) % domainCount;
    free(domainInfo);
    free(memoryStats);
    free(currentAllocations);
    free(demands);
    free(surplus);
}
