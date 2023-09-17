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

double *previousDomainCPUUsage = NULL;

struct DomainPerformance 
{
    int currDomainIndex;
    float cpuUsagePercentage;
    int assignedPhysicalCPU;
    int previousAssignedPhysicalCPU;
};

// Function to compare two domain loads. Returns a negative number, zero, or a positive number.
int compareDomainPerformance(const void *a, const void *b);

// Function to find the index of the minimum value in an array of doubles
int findMinIndex(double *array, int size);

// Handler for the signal; updates the global 'is_exit' flag
void signalHandler();

// Schedules the CPU, checking CPU usage and setting pins at regular intervals
void CPUScheduler(virConnectPtr conn, int interval);

// Gathers information about the host, like the number of pCPUs
void getHostCPUInfo(virConnectPtr conn, int *numberOfPhysicalCPUs);

// Gathers information about each running domain and updates the domainLoad structures
void retrieveRunningDomainInfo(virConnectPtr conn, int domainCount, virDomainPtr *domains, struct DomainPerformance *domainPerformanceData, int numberOfPhysicalCPUs);

// Calculates CPU utilization for domains and for each physical CPU
void computeCPUUtilization(struct DomainPerformance *domainPerformanceData, double *currentDomainUsage, double elapsedTime, int domainCount, double *physicalCPUUtilization);

// Pins domains to specific CPUs based on the domain loads
void assigdomainCountsToCPUs(virDomainPtr *domains, struct DomainPerformance *domainPerformanceData, int domainCount, int numberOfPhysicalCPUs);

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

void signalHandler() {
    printf("Caught Signal");
    is_exit = 1;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval) {

    double elapsedTime = interval * pow(10, 9);
    int numberOfPhysicalCPUs;
    getHostCPUInfo(conn, &numberOfPhysicalCPUs);

    virDomainPtr *domains;
    unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING;
    int domainCount = virConnectListAllDomains(conn, &domains, flags);
    if (domainCount < 0) {
        printf("Failed to get list of domains.\n");
        return;
    }

    double *currentDomainUsage = calloc(domainCount, sizeof(double));
    struct DomainPerformance *domainPerformanceData = calloc(domainCount, sizeof(struct DomainPerformance));

    retrieveRunningDomainInfo(conn, domainCount, domains, domainPerformanceData, numberOfPhysicalCPUs);

    if (previousDomainCPUUsage == NULL) {
        previousDomainCPUUsage = currentDomainUsage;
        free(domains);
        free(domainPerformanceData);
        return;
    }

    double *physicalCPUUtilization = calloc(numberOfPhysicalCPUs, sizeof(double));
    computeCPUUtilization(domainPerformanceData, currentDomainUsage, elapsedTime, domainCount, physicalCPUUtilization);
    
    double sum = 0.0, mean = 0.0, SD = 0.0;
    for (int k = 0; k < numberOfPhysicalCPUs; ++k) {
        sum += physicalCPUUtilization[k];
    }
    mean = sum / numberOfPhysicalCPUs;
    for (int k = 0; k < numberOfPhysicalCPUs; ++k) {
        SD += pow(physicalCPUUtilization[k] - mean, 2);
    }
    SD = sqrt(SD / numberOfPhysicalCPUs);
    printf("Standard Deviation: %f\n", SD);

    if (SD > 5) {
        assigdomainCountsToCPUs(domains, domainPerformanceData, domainCount, numberOfPhysicalCPUs);
    }

    free(physicalCPUUtilization);
    free(domainPerformanceData);
    free(previousDomainCPUUsage);
    free(domains);
    previousDomainCPUUsage = currentDomainUsage;
}

void getHostCPUInfo(virConnectPtr conn, int *numberOfPhysicalCPUs) {

    virNodeInfo hostInfo;

    if (virNodeGetInfo(conn, &hostInfo) != 0) {
        fprintf(stderr, "Error retrieving host information.\n");
        return;
    }

    *numberOfPhysicalCPUs = VIR_NODEINFO_MAXCPUS(hostInfo);
    printf("Total physical CPUs as per virNodeGetInfo: %d\n", *numberOfPhysicalCPUs);
}

void retrieveRunningDomainInfo(virConnectPtr conn, int domainCount, virDomainPtr *domains, struct DomainPerformance *domainPerformanceData, int numberOfPhysicalCPUs) {

    for (size_t k = 0; k < domainCount; k++) {
        virDomainInfo currDomainInfo;
        if (virDomainGetInfo(domains[k], &currDomainInfo) == -1) {
            printf("Failed to get domain info for domain # %ld\n", k);
            continue;
        }

        int virtualCPUs = currDomainInfo.nrVirtCpu;
        int cpuMapLength = VIR_CPU_MAPLEN(numberOfPhysicalCPUs);
        unsigned char *cpuMapping = calloc(virtualCPUs, cpuMapLength);
        virVcpuInfoPtr virtualCPUInfo = malloc(sizeof(virVcpuInfo) * virtualCPUs);

        if (virDomainGetVcpus(domains[k], virtualCPUInfo, virtualCPUs, cpuMapping, cpuMapLength) == -1) {
            printf("Failed to get vcpu info for domain # %ld\n", k);
        }

        domainPerformanceData[k].cpuUsagePercentage = virtualCPUInfo->cpuelapsedTime;
        domainPerformanceData[k].previousAssignedPhysicalCPU = virtualCPUInfo->cpu;

        free(virtualCPUInfo);
        free(cpuMapping);
    }
}

void computeCPUUtilization(struct DomainPerformance *domainPerformanceData, double *currentDomainUsage, double elapsedTime, int domainCount, double *physicalCPUUtilization) {
    for (size_t k = 0; k < domainCount; k++) {
        domainPerformanceData[k].cpuUsagePercentage = (currentDomainUsage[k] - previousDomainCPUUsage[k]) * 100 / elapsedTime;
        physicalCPUUtilization[domainPerformanceData[k].previousAssignedPhysicalCPU] += domainPerformanceData[k].cpuUsagePercentage;
    }
}

void assigdomainCountsToCPUs(virDomainPtr *domains, struct DomainPerformance *domainPerformanceData, int domainCount, int numberOfPhysicalCPUs) {
    qsort(domainPerformanceData, domainCount, sizeof(struct DomainPerformance), compareDomainPerformance);

    double *cpuUsageArray = calloc(numberOfPhysicalCPUs, sizeof(double));

    for (size_t k = 0; k < domainCount; k++) {
        int smallestBucketIndex = findMinIndex(cpuUsageArray, numberOfPhysicalCPUs);
        cpuUsageArray[smallestBucketIndex] += domainPerformanceData[k].cpuUsagePercentage;
        domainPerformanceData[k].assignedPhysicalCPU = smallestBucketIndex;
    }

    for (size_t k = 0; k < domainCount; k++) {
        int currDomainIndex = domainPerformanceData[k].currDomainIndex;
        int targetPhysicalCPU = domainPerformanceData[k].assignedPhysicalCPU;

        virDomainInfo currDomainInfo;
        if (virDomainGetInfo(domains[currDomainIndex], &currDomainInfo) == -1) {
            printf("Failed to get domain info for domain # %d\n", currDomainIndex);
            continue;
        }

        int virtualCPUs = currDomainInfo.nrVirtCpu;
        int cpuMapLength = VIR_CPU_MAPLEN(numberOfPhysicalCPUs);
        unsigned char *cpuMapping = calloc(virtualCPUs, cpuMapLength);

		// Set the particular physical CPU as active for the domain
        VIR_CPU_SET(targetPhysicalCPU, cpuMapping);

        if (virDomainPinVcpu(domains[currDomainIndex], 0, cpuMapping, cpuMapLength) == -1) {
            fprintf(stderr, "Failed to pin VCPU for domain # %d.\n", currDomainIndex);
        }

        free(cpuMapping);
    }

    free(cpuUsageArray);
}

int compareDomainPerformance(const void *a, const void *b) {
    struct DomainPerformance *firstDomain = (struct DomainPerformance *)a;
    struct DomainPerformance *secondDomain = (struct DomainPerformance *)b;

    return (secondDomain->cpuUsagePercentage - firstDomain->cpuUsagePercentage);
}

int findMinIndex(double *array, int size) {

	if (size <= 0 || !array) return -1;
	
    int minIndex = 0;
    double minValue = array[0];

    for (int k = 1; k < size; k++) {
        if (array[k] < minValue) {
            minValue = array[k];
            minIndex = k;
        }
    }

    return minIndex;
}
 