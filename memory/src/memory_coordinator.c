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

struct domainMemUsage{
	unsigned long long actual;
	unsigned long long unused;
	int memDemandingBool;
	int memFreeCount;
};

struct domainMemUsage *prevUsage = NULL;

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
void MemoryScheduler(virConnectPtr conn, int interval)
{

	int statPeriod = interval;
	int memoryIncrement = 100;	 // MB
	long unusedMin = 100;		 // MB
	long actualMin = 200;		 // MB
	long maxMem = 2048;			 // MB
	long hostMin = 200;			 // MB
	long unusedThreadhold = 400; // MB
	long memMinWork = unusedMin *2; // MB

	// get list of domain
	virDomainPtr *domainList;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING;

	int nDomain = virConnectListAllDomains(conn, &domainList, flags); // return the list of running domains

	if (nDomain < 0)
	{
		printf("Failed to get list of domains.\n");
		return;
	}

	struct domainMemUsage *memUsage = calloc(sizeof(struct domainMemUsage), nDomain);

	for (size_t i = 0; i < nDomain; i++)
	{
		// Set Stat Period
		if (virDomainSetMemoryStatsPeriod(domainList[i], statPeriod, VIR_DOMAIN_AFFECT_LIVE) == -1)
		{
			printf("Failed to set stats period for domain[%ld].\n", i);
		}

		virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int nr_stats;
		nr_stats = virDomainMemoryStats(domainList[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		if (nr_stats == -1)
		{
			printf("Error getting memory stats for domain[%ld].\n", i);
		}

		// printf("Domain[%ld] -->",i);
		for (size_t j = 0; j < nr_stats; j++)
		{
			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
			{
				(memUsage + i)->unused = stats[j].val / 1024;
				// printf("unused %llu--", (memUsage+i)->unused);
			}

			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
			{
				(memUsage + i)->actual = stats[j].val / 1024;
				// printf("actual %llu--", (memUsage+i)->actual);
			}
		}
	}

	if (prevUsage == NULL)
	{

		for (size_t i = 0; i < nDomain; i++)
		{
			(memUsage + i)->memDemandingBool = 0;
			(memUsage+i)->memFreeCount = 0;
		}

		prevUsage = memUsage;
		memUsage = NULL;

		// printf("return from prevUsage\n");
		// printf("\n");
		return;
	}

	// printf("Continue with current usage\n");
	int needMemoryBoolean[nDomain];
	long memoryConsumptionRate[nDomain];
	int idleDomain[nDomain];

	for (size_t i = 0; i < nDomain; i++)
	{
		needMemoryBoolean[i] = 0;
		memoryConsumptionRate[i] = 0;
		idleDomain[i] = 0;
		(memUsage + i)->memDemandingBool = 0;
	}

	for (size_t i = 0; i < nDomain; i++)
	{

		printf("Domain[%ld]---prev: actual[%llu]/unused[%llu]---curr: actual[%llu]/unused[%llu]\n", i, (prevUsage + i)->actual, (prevUsage + i)->unused, (memUsage + i)->actual, (memUsage + i)->unused);
		// Determine if domain is demanding memory
		long dUnused = ((prevUsage + i)->unused - (memUsage + i)->unused) / 1;
		long dActual = ((prevUsage + i)->actual - (memUsage + i)->actual) / 1;

		if ((dUnused > 5 && dActual == 0) || (dActual < 0 && (prevUsage + i)->memDemandingBool == 1))
		{
			(memUsage + i)->memDemandingBool = 1;
			memoryConsumptionRate[i] = dUnused; // Save current consumption rate
			printf("Domain[%ld] is demanding memory\n", i);

			// printf("Delta usage for domain[%ld]: %llu --->",i,deltaUsage);
			if (memUsage[i].unused < memMinWork)
			{
				needMemoryBoolean[i] = 1;
				// printf("Domain[%ld] needs memory\n",i);
			}
		}

		// Determine if domain's process releasing memory
		if ((memUsage + i)->unused > unusedThreadhold && (prevUsage + i)->unused > unusedThreadhold && dUnused < 5)
		{
			(memUsage + i)->memFreeCount = (prevUsage + i)->memFreeCount + 1;
			if ((memUsage + i)->memFreeCount == 3)
			{
				//set flag to reclaimed memory
				idleDomain[i] = 1;

				//reset count
				(memUsage + i)->memFreeCount = 0;

				printf("Domain[%ld] is releasing memory\n", i);
			}
		}
		else
		{
			(memUsage + i)->memFreeCount = 0;
		}
	}

	// check if there is domain releasing mem
	int idleDomainTotal = 0;
	for (size_t i = 0; i < nDomain; i++)
	{
		idleDomainTotal += idleDomain[i];
	}

	if (idleDomainTotal > 0)
	{
		for (size_t i = 0; i < nDomain; i++)
		{
			if (idleDomain[i] == 1 && memUsage[i].actual > actualMin)
			{
				int memDecrement = MIN(memoryIncrement, memUsage[i].unused - unusedMin);
				if (virDomainSetMemory(domainList[i], MAX(memUsage[i].actual - memDecrement, actualMin) * 1024) == -1)
				{
					printf("Failed to reclaimed memory from domain[%ld].\n", i);
				}
				else
				{
					printf("Reclaimed from idle memory domain[%ld]\n", i);
				}
			}
		}
	}

	// check if there is a need for memory
	int nDomainNeedMem = 0;
	for (size_t i = 0; i < nDomain; i++)
	{
		nDomainNeedMem += needMemoryBoolean[i];
	}

	if (nDomainNeedMem > 0)
	{
		// reclaiming memory
		// unsigned long long memReclaimedRate[nDomain];
		// get Host Free Memory

		for (size_t i = 0; i < nDomain; i++)
		{
			if (memUsage[i].memDemandingBool == 0 && prevUsage[i].memDemandingBool == 0 && memUsage[i].actual > actualMin && memUsage[i].unused > unusedMin)
			{
				int memDecrement = MIN(memoryIncrement, memUsage[i].unused - unusedMin);
				if (virDomainSetMemory(domainList[i], MAX((memUsage[i].actual - memDecrement), actualMin) * 1024) == -1)
				{
					printf("Failed to reclaimed memory from domain[%ld].\n", i);
				}
			}
		}

		long memAvailableTotal = (virNodeGetFreeMemory(conn) / 1024) / 1024; // Convert to MB


		if (memAvailableTotal > hostMin)
		{
			// Check partition vs need vs max host
			unsigned long long memPartition = (memAvailableTotal - hostMin) / nDomainNeedMem;

			printf("Host Memory[%lu] --- MemToAllocate[%ld]\n", memAvailableTotal, (memAvailableTotal - hostMin));

			long maxConsumption = 0;
			for (size_t i = 0; i < nDomain; i++)
			{

				maxConsumption = MAX(maxConsumption,memoryConsumptionRate[i]);
				//printf("Max Consumption [%ld] vs Current Consumption [%ld]\n", maxConsumption, memoryConsumptionRate[i]);
			}

			memoryIncrement = MAX(memoryIncrement,1.5*maxConsumption);

			//printf("Memory Increment: %d\n", memoryIncrement);

			memPartition = MIN(memPartition, memoryIncrement);

			//printf("Memory Partition: %lld\n", memPartition);

			for (size_t i = 0; i < nDomain; i++)
			{
				if (needMemoryBoolean[i] == 1 && memUsage[i].actual <= maxMem)
				{
					if (virDomainSetMemory(domainList[i], MIN(memUsage[i].actual + memPartition, maxMem) * 1024) == -1)
					{
						printf("Failed to allocated memory from domain[%ld].\n", i);
					}
				}
			}
		}
	}
	free(prevUsage);
	prevUsage = memUsage;
	memUsage = NULL;
	printf("\n");
}