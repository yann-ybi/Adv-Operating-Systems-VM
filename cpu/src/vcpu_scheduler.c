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

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

const int NANOSECONDS_IN_A_SECOND = 1000000000;

struct VirtualMachineLoad {
	float usage;
	unsigned int index;
	unsigned int ipCPU;
	unsigned int iprevpCPU;
};

double *prevUsageList = NULL;
void CPUScheduler(virConnectPtr conn,int interval);
double convertSecondsToNanoseconds(int interval);
double computeDomainUtilization(double currUsage, double prevUsage, double timeInterval);
void updateDomainAndCPUUtilization(double* utilizationList_pCPU, struct VirtualMachineLoad* loadList, double* usageList, double* prevUsageList, double timeInterval, int numActiveDomains);
double calculateMean(double* data, int length);
double calculateStandardDeviation(double* data, int length, double mean);
int compareDomains(const void *a, const void *b);
unsigned int findMinIndex(const double *arr, int length);
void cleanup(virDomainPtr* domains, double* usageList, double* prevUsageList, double* utilizationList_pCPU, struct VirtualMachineLoad* loadList);
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
void CPUScheduler(virConnectPtr conn, int interval)
{
	virNodeInfo hostInfo;
	
	if (virNodeGetInfo(conn, &hostInfo) == -1)
		fprintf(stderr, "Error: Unable to retrieve host information.\n");

	int numPhysicalCPUs = VIR_NODEINFO_MAXCPUS(hostInfo);
	
    if (numPhysicalCPUs == -1) {
        fprintf(stderr, "Error: Unable to determine the number of physical CPUs.\n");
        return;
    }
	double* utilizationList_pCPU = calloc(numPhysicalCPUs, sizeof(double));
	if (!utilizationList_pCPU) {
		fprintf(stderr, "Error: Memory allocation failed for utilizationList_pCPU.\n");
		return;
	}

	virDomainPtr *domains;
	int numActiveDomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if (numActiveDomains < 0) 
		fprintf(stderr, "Error: Unable to fetch the list of active virtual domains.\n");

	struct VirtualMachineLoad *loadList = calloc(sizeof(struct VirtualMachineLoad), numActiveDomains);
	double *usageList = calloc(sizeof(double), numActiveDomains);

	for (size_t k = 0; k < numActiveDomains; k++) {
		virDomainInfo domainInfo;
		
		if (virDomainGetInfo(domains[k], &domainInfo) == -1) 
			fprintf(stderr, "Error: Unable to retrieve domain information.\n");
		
		int mapSize_pCPU = VIR_CPU_MAPLEN(numPhysicalCPUs);

		int numVirtualCPUs = domainInfo.nrVirtCpu;

		unsigned char *mapCPU = calloc(numVirtualCPUs, mapSize_pCPU);
		virVcpuInfoPtr info_vCPU = malloc(sizeof(virVcpuInfo) * numVirtualCPUs);

		if (virDomainGetVcpus(domains[k], info_vCPU, numVirtualCPUs, mapCPU, mapSize_pCPU) == -1)
			fprintf(stderr, "Error: Unable to retrieve the domain virtual CPUs information.\n");

		(loadList + k)->iprevpCPU = info_vCPU->cpu;
		usageList[k] = info_vCPU->cpuTime;

		free(info_vCPU);
		free(mapCPU);
	}

    if (!prevUsageList) {
        prevUsageList = usageList;
		usageList == NULL;
        cleanup(domains, usageList, NULL, NULL, loadList);
        return;
    }

	double timeInterval = convertSecondsToNanoseconds(interval);

	updateDomainAndCPUUtilization(utilizationList_pCPU, loadList, usageList, prevUsageList, timeInterval, numActiveDomains);

	double mean = calculateMean(utilizationList_pCPU, numPhysicalCPUs);
    double standDev = calculateStandardDeviation(utilizationList_pCPU, numPhysicalCPUs, mean);

	if (standDev <= 5 ) {
		cleanup(domains, usageList, prevUsageList, utilizationList_pCPU, loadList);
		return;
	}

	qsort(loadList, numActiveDomains, sizeof(struct VirtualMachineLoad), compareDomains);

    double *loadListpCPUs = calloc(sizeof(double), numPhysicalCPUs);

    for (size_t k = 0; k < numActiveDomains; k++) {
        unsigned int indexLeastLoaded = findMinIndex(loadListpCPUs, numPhysicalCPUs);
        *(indexLeastLoaded + loadListpCPUs) += (loadList + k)->usage;
        (loadList + k)->ipCPU = indexLeastLoaded;
    }

    for (size_t k = 0; k < numActiveDomains; k++) {
        int activeDomainIndex = (loadList + k)->index;
        int targetPhysicalCPU = (loadList + k)->ipCPU;

        virDomainInfo activeDomainInfo;
        if(virDomainGetInfo(domains[activeDomainIndex], &activeDomainInfo) == -1) 
            fprintf(stderr, "Error: Unable to retrieve domain information.\n");

        int numVirtualCPUs = activeDomainInfo.nrVirtCpu;
        int cpuMappingLength = VIR_CPU_MAPLEN(numPhysicalCPUs);

        for(int vcpu=0; vcpu < numVirtualCPUs; vcpu++) {
            unsigned char *cpuPinMap = calloc(numVirtualCPUs, cpuMappingLength);

            VIR_USE_CPU(cpuPinMap, targetPhysicalCPU);
            if (virDomainPinVcpu(domains[activeDomainIndex], vcpu, cpuPinMap, cpuMappingLength) == -1)
                fprintf(stderr, "Error: Unable to pin virtual CPU to physical CPU.\n");

            free(cpuPinMap);
        }

        virDomainFree(domains[activeDomainIndex]);
    }
}

double convertSecondsToNanoseconds(int interval) {
	return interval * NANOSECONDS_IN_A_SECOND;
}

double computeDomainUtilization(double currUsage, double prevUsage, double timeInterval) {
	return (currUsage - prevUsage) * 100 / timeInterval;
}

void updateDomainAndCPUUtilization(double* utilizationList_pCPU, struct VirtualMachineLoad* loadList, double* usageList, double* prevUsageList, double timeInterval, int numActiveDomains) {
    for (size_t k = 0; k < numActiveDomains; k++) {
        loadList[k].usage = computeDomainUtilization(usageList[k], prevUsageList[k], timeInterval);
        utilizationList_pCPU[loadList[k].iprevpCPU] += loadList[k].usage;
    }
}

double calculateMean(double* data, int length) {
    double sum = 0.0;
    for (int i = 0; i < length; ++i) {
        sum += data[i];
    }
    return sum / length;
}

double calculateStandardDeviation(double* data, int length, double mean) {
    double variance = 0.0;
    for (int i = 0; i < length; ++i) 
        variance += pow(data[i] - mean, 2);

    return sqrt(variance / length);
}

int compareDomains(const void *a, const void *b) {
    struct VirtualMachineLoad *firstDomain = (struct VirtualMachineLoad *)a;
    struct VirtualMachineLoad *seconDomain = (struct VirtualMachineLoad *)b;

    return (firstDomain->usage > seconDomain->usage) ? -1 : (firstDomain->usage < seconDomain->usage) ? 1 : 0;
}

unsigned int findMinIndex(const double *arr, int length) {
    unsigned int index = 0;

    for (int k = 1; k < length; k++)
        if (arr[k] < arr[index]) index = k;

    return index;
}

void cleanup(virDomainPtr* domains, double* usageList, double* prevUsageList, double* utilizationList_pCPU, struct VirtualMachineLoad* loadList) {
    if (domains) free(domains);
    if (usageList) free(usageList);
    if (prevUsageList) free(prevUsageList);
    if (utilizationList_pCPU) free(utilizationList_pCPU);
    if (loadList) free(loadList);
}
