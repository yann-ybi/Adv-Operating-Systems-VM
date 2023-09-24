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
	virDomainInfoPtr domainDetails = malloc(sizeof(virDomainInfo));
	virDomainMemoryStatPtr memoryStats = malloc(14 * sizeof(virDomainMemoryStatStruct));

	int domainCount = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

	long long int availableHostMemory = virNodeGetFreeMemory(conn) / 1024;  // in MB

	// Arrays to store memory metrics for each VM
	long long int* additionalMemoryNeeded = calloc(domainCount, sizeof(long long int));
	long long int* excessMemory = calloc(domainCount, sizeof(long long int));
	long long int* currentMemory = calloc(domainCount, sizeof(long long int));

	for (int i = 0; i < domainCount; i++) 
	{
		virDomainSetMemoryStatsPeriod(activeDomains[i], interval, VIR_DOMAIN_AFFECT_LIVE);
		virDomainGetInfo(activeDomains[i], domainDetails);
		virDomainMemoryStats(activeDomains[i], memoryStats, 14, 0);

		long long int balloonMemory, unusedDomainMemory, activeDomainMemory;
		for (int j = 0; j < 14; j++) 
		{
			if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) 
				balloonMemory = memoryStats[j].val / 1024;

			if (memoryStats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) 
				unusedDomainMemory = memoryStats[j].val / 1024;				
		}

		activeDomainMemory = balloonMemory - unusedDomainMemory;

		// Calculate additional memory needed/excess based on constraints
		if (unusedDomainMemory < 200) 
			additionalMemoryNeeded[i] = MIN(200 - unusedDomainMemory, domainDetails->maxMem / 1024 - balloonMemory);

		if (unusedDomainMemory > 100) 
			excessMemory[i] = MIN(50, unusedDomainMemory - 100);

		currentMemory[i] = balloonMemory;
	}

	// Allocate or release memory for VMs
	for (int i = 0; i < domainCount; i++) 
	{
		for (int j = 0; j < domainCount; j++) 
		{
			if (i != j && additionalMemoryNeeded[i] > 0 && excessMemory[j] > 0) 
			{
				long long transferSize = MIN(additionalMemoryNeeded[i], excessMemory[j]);
				virDomainSetMemory(activeDomains[i], (currentMemory[i] + transferSize) * 1024);
				virDomainSetMemory(activeDomains[j], (currentMemory[j] - transferSize) * 1024);
				additionalMemoryNeeded[i] -= transferSize;
				excessMemory[j] -= transferSize;
			}
		}

		if (additionalMemoryNeeded[i] > 0 && availableHostMemory > 200) 
		{
			long long allocateSize = MIN(additionalMemoryNeeded[i], availableHostMemory - 200);
			virDomainSetMemory(activeDomains[i], (currentMemory[i] + allocateSize) * 1024);
			availableHostMemory -= allocateSize;
		}
	}

	free(domainDetails);
	free(memoryStats);
	free(additionalMemoryNeeded);
	free(excessMemory);
	free(currentMemory);
	free(activeDomains);
}
