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
void MemoryScheduler(virConnectPtr conn, int interval)
{
    int k, numDomains;
    int *activeDomains;
    virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];

    // Get the list of active domains.
    numDomains = virConnectNumOfDomains(conn);
    activeDomains = malloc(sizeof(int) * numDomains);
    numDomains = virConnectListDomains(conn, activeDomains, numDomains);

    for (k = 0; k < numDomains; k++) {
        virDomainPtr domain = virDomainLookupByID(conn, activeDomains[k]);
        if (domain == NULL) {
            continue;
        }

        // Set the STATS_PERIOD for the domain
        virDomainSetMemoryStatsPeriod(domain, interval, 0);

        int nstats = virDomainMemoryStats(domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
        unsigned long totalMem = 0;
        unsigned long unusedMem = 0;

        for (int j = 0; j < nstats; j++) {
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
                totalMem = stats[j].val;
            } else if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) {
                unusedMem = stats[j].val;
            }
        }

        // Memory management
        if (unusedMem > 100) {
			 // Retain 100MB unused memory
            unsigned long memToRelease = unusedMem - 100; 
            if (memToRelease > 50) {  // Gradual release
                memToRelease = 50;
            }
            totalMem -= memToRelease;  // Reduce actual memory
            virDomainSetMemory(domain, totalMem);
        }

        virDomainFree(domain);
    }

    // Check host memory
    unsigned long freeMem = virNodeGetFreeMemory(conn);
    if (freeMem <= 200*1024*1024) {  // 200MB to bytes
        printf("Warning: Host has low memory. Consider actions.\n");
    }

    free(activeDomains);
}
