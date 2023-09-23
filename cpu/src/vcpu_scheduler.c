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

typedef struct {
    virDomainPtr domain;        // Reference to the domain
    unsigned long long prevCpuTime; // Previous CPU time 
    int vcpuNum;                // VCPU number 
} DomainInfo;

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

void CPUScheduler(virConnectPtr conn,int interval);
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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval) {
    int numDomains;
    int *activeDomains;
    int numPcpus = virNodeGetCPUMap(conn, NULL, NULL, 0);

    // Array to store each pCPU's utilization
    double *pCpuUtilizations = (double *)malloc(sizeof(double) * numPcpus);
    memset(pCpuUtilizations, 0, sizeof(double) * numPcpus);

    // Get all active running virtual machines
    numDomains = virConnectNumOfDomains(conn);
    activeDomains = (int *)malloc(sizeof(int) * numDomains);
    virConnectListDomains(conn, activeDomains, numDomains);

    DomainInfo *domainInfos = (DomainInfo *)malloc(sizeof(DomainInfo) * numDomains);
    unsigned long long currCpuTime;

    for (int k = 0; k < numDomains; k++) {
        domainInfos[k].domain = virDomainLookupByID(conn, activeDomains[k]);

        virVcpuInfoPtr vCpuInfo = malloc(sizeof(virVcpuInfo));
        virDomainGetVcpus(domainInfos[k].domain, vCpuInfo, 1, NULL, 0);

		domainInfos[k].vcpuNum = vCpuInfo->number;
		domainInfos[k].prevCpuTime = vCpuInfo->cpuTime;

        currCpuTime = vCpuInfo->cpuTime;
        double vcpuUsage = ((double)(currCpuTime - domainInfos[k].prevCpuTime) / (interval * 1e9)) * 100;
		domainInfos[k].prevCpuTime = currCpuTime;
		
        // Determine the current map between VCPU to PCPU
        unsigned char *currCpuMap = (unsigned char *)malloc(sizeof(unsigned char) * numPcpus);
        virDomainGetVcpuPinInfo(domainInfos[k].domain, 1, currCpuMap, numPcpus, VIR_DOMAIN_AFFECT_CURRENT);

        // Algorithm to find "the best" PCPU to pin each VCPU
        int currPCpu = -1;
        for (int j = 0; j < numPcpus; j++) {
            if (currCpuMap[j] == 1) { // The VCPU is pinned to this pCPU
                currPCpu = j;
                break;
            }
        }

        if (currPCpu >= 0) {
            pCpuUtilizations[currPCpu] += vcpuUsage;
        }

        free(vCpuInfo);
        free(currCpuMap);
    }

    // Calculate the mean utilization
    double meanUtilization = 0.0;
    for (int k = 0; k < numPcpus; k++) {
        meanUtilization += pCpuUtilizations[k];
    }
    meanUtilization /= numPcpus;

    // Calculate the standard deviation of utilization
    double variance = 0.0;
    for (int k = 0; k < numPcpus; k++) {
        variance += pow(pCpuUtilizations[k] - meanUtilization, 2);
    }
    double stddev = sqrt(variance / numPcpus);

    if (stddev > 0.05 * meanUtilization) {
        for (int k = 0; k < numDomains; k++) {
            int bestPcpu = 0;
            for (int j = 1; j < numPcpus; j++) {
                if (pCpuUtilizations[j] < pCpuUtilizations[bestPcpu]) {
                    bestPcpu = j;
                }
            }
            unsigned char currCpuMap = 1 << bestPcpu; // Pinning to bestPcpu !!
            virDomainPinVcpu(domainInfos[k].domain, domainInfos[k].vcpuNum, &currCpuMap, 1);
        }
    }

    free(activeDomains);
    free(domainInfos);
    free(pCpuUtilizations);
}
