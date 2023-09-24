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

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval) {
    virDomainPtr *activeDomains;
    virDomainInfo domainDetails;
    virDomainMemoryStatStruct memoryStats[14];

    int domainCount = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
    if (domainCount < 0) {
        fprintf(stderr, "Failed to list domains.\n");
        return;
    }

    long long int availableHostMemory = virNodeGetFreeMemory(conn) / 1024; // MB 

    const long long int HOST_MIN_UNUSED_MEMORY = 200;
    const long long int DOMAIN_MIN_UNUSED_MEMORY = 100;
    const long long int MAX_MEMORY_RELEASE = 50; 
    const long long int EXCESS_THRESHOLD = 50;

    long long int* additionalMemoryNeeded = calloc(domainCount, sizeof(long long int));
    long long int* excessMemory = calloc(domainCount, sizeof(long long int));
    long long int* currentMemory = calloc(domainCount, sizeof(long long int));

    for (int i = 0; i < domainCount; i++) {
        if (virDomainSetMemoryStatsPeriod(activeDomains[i], interval, VIR_DOMAIN_AFFECT_LIVE) < 0) {
            fprintf(stderr, "Failed to set memory stats period.\n");
            continue;
        }

        if (virDomainGetInfo(activeDomains[i], &domainDetails) < 0) {
            fprintf(stderr, "Failed to get domain info.\n");
            continue;
        }

        int statsCount = virDomainMemoryStats(activeDomains[i], memoryStats, 14, 0);
        if (statsCount < 0) {
            fprintf(stderr, "Failed to get memory stats.\n");
            continue;
        }

        long long int balloonMemory = 0, unusedDomainMemory = 0;
        for (int j = 0; j < statsCount; j++) {
            if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
                balloonMemory = memoryStats[j].val / 1024;
            else if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
                unusedDomainMemory = memoryStats[j].val / 1024;
        }

        if (unusedDomainMemory < DOMAIN_MIN_UNUSED_MEMORY) 
            additionalMemoryNeeded[i] = MIN(DOMAIN_MIN_UNUSED_MEMORY - unusedDomainMemory, domainDetails.maxMem / 1024 - balloonMemory);

        if (unusedDomainMemory > EXCESS_THRESHOLD && unusedDomainMemory - DOMAIN_MIN_UNUSED_MEMORY > MAX_MEMORY_RELEASE)
            excessMemory[i] = MIN(MAX_MEMORY_RELEASE, unusedDomainMemory - DOMAIN_MIN_UNUSED_MEMORY);

        currentMemory[i] = balloonMemory;
    }

    for (int i = 0; i < domainCount; i++) {
        for (int j = 0; j < domainCount; j++) {
            if (i != j && additionalMemoryNeeded[i] > 0 && excessMemory[j] > 0) {
                long long transferSize = MIN(additionalMemoryNeeded[i], excessMemory[j]);

                long long int newMemoryForDomainI = currentMemory[i] + transferSize;
                long long int newMemoryForDomainJ = currentMemory[j] - transferSize;

                if (virDomainSetMemory(activeDomains[i], newMemoryForDomainI * 1024) < 0 ||
                    virDomainSetMemory(activeDomains[j], newMemoryForDomainJ * 1024) < 0) {
                    fprintf(stderr, "Failed to set domain memory.\n");
                } else {
                    additionalMemoryNeeded[i] -= transferSize;
                    excessMemory[j] -= transferSize;
                }
            }
        }

        if (additionalMemoryNeeded[i] > 0 && availableHostMemory > HOST_MIN_UNUSED_MEMORY) {
            long long allocateSize = MIN(additionalMemoryNeeded[i], availableHostMemory - HOST_MIN_UNUSED_MEMORY);

            if (virDomainSetMemory(activeDomains[i], (currentMemory[i] + allocateSize) * 1024) < 0) {
                fprintf(stderr, "Failed to set domain memory.\n");
            } else {
                availableHostMemory -= allocateSize;
            }
        }
    }

    free(additionalMemoryNeeded);
    free(excessMemory);
    free(currentMemory);
    free(activeDomains);
}
