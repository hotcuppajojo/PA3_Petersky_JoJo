
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#define ARRAY_SIZE 2000000
#define ITERATION_THRESHOLD 10000
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)

typedef struct _thread_data_t {
    int localTid;
    int *dataStart;  // Starting pointer for this thread's portion
    int dataSize;    // Size of the portion this thread will sum
    pthread_mutex_t *lock;
    pthread_mutex_t *countLock;
    pthread_mutex_t *exitLock;
    int *iterationCount;
    long long int *totalSum;
    int *errorFlag;
    int *shouldExit;
} thread_data_t;

void print_progress(int local_tid, int value);
void* arraySum(void *arg);

int main(int argc, char *argv[]) {
    int retVal = 0;
    int errorFlag = 0;
    pthread_t *threads = NULL;
    thread_data_t *threadData = NULL;
    srand(time(NULL));
    
    if (argc != 2) {
        printf("Usage: %s <number of threads>\n", argv[0]);
        return -1;
    }

    int *data = (int *) malloc(ARRAY_SIZE * sizeof(int));
    if (!data) {
        perror("Failed to allocate memory for data");
        retVal = -1;
        goto cleanup;
    }
    
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = rand();  // Fill the data array with random integers.
    }

    long long int totalSum = 0;
    pthread_mutex_t lock;
    pthread_mutex_t countLock;
    pthread_mutex_t exitLock;
    int iterationCount = 0;
    
    int numberOfThreads = atoi(argv[1]);
    threads = (pthread_t *) malloc(numberOfThreads * sizeof(pthread_t));
    threadData = (thread_data_t *) malloc(numberOfThreads * sizeof(thread_data_t));

    if (!threads || !threadData) {
        perror("Failed to allocate memory for threads or thread data");
        retVal = -1;
        goto cleanup;
    }

    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("Mutex initialization failed");
        retVal = -1;
        goto cleanup;
    }
    
    if (pthread_mutex_init(&countLock, NULL) != 0) {
        perror("Count mutex initialization failed");
        retVal = -1;
        goto cleanup;
    }
    
    if (pthread_mutex_init(&exitLock, NULL) != 0) {
        perror("Exit mutex initialization failed");
        retVal = -1;
        goto cleanup;
    }
    
    int portionSize = ARRAY_SIZE / numberOfThreads;

    for (int i = 0; i < numberOfThreads; i++) {
        threadData[i].localTid = i;
        threadData[i].dataStart = data + i * portionSize;
        threadData[i].dataSize = (i == numberOfThreads - 1) ? ARRAY_SIZE - i * portionSize : portionSize;
        threadData[i].lock = &lock;
        threadData[i].countLock = &countLock;
        threadData[i].exitLock = &exitLock;
        threadData[i].iterationCount = &iterationCount;
        threadData[i].totalSum = &totalSum;
        threadData[i].errorFlag = &errorFlag;
        threadData[i].shouldExit = 0;
        
        if (pthread_create(&threads[i], NULL, arraySum, &threadData[i]) != 0) {
            perror("Thread creation failed");
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            retVal = -1;
            goto cleanup;
        }
    }

    for (int i = 0; i < numberOfThreads; i++) {
        pthread_join(threads[i], NULL);
    }

    if (errorFlag == -1) {
        printf("Error detected in one of the threads.\n");
        retVal = -1;
    }

cleanup:
    pthread_mutex_destroy(&countLock);
    pthread_mutex_destroy(&lock);
    free(data);
    free(threads);
    free(threadData);
    return retVal;
}

void* arraySum(void *arg) {
    thread_data_t *data = (thread_data_t *) arg;
    long long int threadSum = 0;
    long latency_max = 0;
    int localIterationCount = 0;
    int currentBatchSize = 10;
    long latency_threshold = 100000; // initial latency threshold (100 μs)
    long totalLatency = 0;
    int numLatencyObservations = 0;

    while (!(*data->shouldExit)) {
        struct timespec start, end;

        int batchSize = data->dataSize / currentBatchSize;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        for (int i = 0; i < batchSize; i++) {
            if (threadSum > LLONG_MAX - data->dataStart[i] || threadSum < LLONG_MIN + data->dataStart[i]) {
                perror("Overflow or underflow detected in thread summation");
                *data->errorFlag = -1;
                pthread_mutex_unlock(data->lock);
                pthread_exit(NULL);
            }
            threadSum += data->dataStart[i];
        }
        
        pthread_mutex_lock(data->lock);
        if ((*data->totalSum) > LLONG_MAX - threadSum || (*data->totalSum) < LLONG_MIN + threadSum) {
            perror("Overflow or underflow detected in total summation");
            *data->errorFlag = -1;
            pthread_mutex_unlock(data->lock);
            pthread_exit(NULL);
        }
        (*data->totalSum) += threadSum;
        pthread_mutex_unlock(data->lock);
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        long latency = ((end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec)) / currentBatchSize;
        
        latency_max = latency > latency_max ? latency : latency_max;
        print_progress(data->localTid, latency_max);
        
        totalLatency += latency;
        numLatencyObservations++;
        
        localIterationCount++;
        
        if (localIterationCount % currentBatchSize == 0) {
            pthread_mutex_lock(data->countLock);
            (*data->iterationCount) += currentBatchSize;
            if (*data->iterationCount >= ITERATION_THRESHOLD || *data->errorFlag == -1) {
                pthread_mutex_lock(data->exitLock);
                *(data->shouldExit) = 1;
                pthread_mutex_unlock(data->exitLock);
            }
            pthread_mutex_unlock(data->countLock);
            pthread_mutex_lock(data->exitLock);
            if (data->shouldExit) {
                pthread_mutex_unlock(data->exitLock);
                break;
            }
            pthread_mutex_unlock(data->exitLock);
            // Adjust batch size based on latency
            if (latency_max > latency_threshold) {
                currentBatchSize++;  // Increase batch size
            } else {
                currentBatchSize = currentBatchSize > 1 ? currentBatchSize - 1 : 1;  // Decrease batch size but ensure it's at least 1
            }
            
            // Periodically adjust the latency threshold based on past latencies
            if (numLatencyObservations % 10 == 0) { // Every 10 iterations, update the threshold
                latency_threshold = totalLatency / numLatencyObservations;
                // Reset for next set of observations
                totalLatency = 0;
                numLatencyObservations = 0;
            }
            
            // Adjusting the thread's portion to be summed in the next iteration
            data->dataStart += batchSize;
            data->dataSize -= batchSize;
            
            if (data->dataSize == 0) {
                // If this thread has summed its entire portion, break out of the loop
                break;
            }
        }
    }
    return NULL;
}

void print_progress(int local_tid, int value) {
    const int barWidth = 50; // Define the width of the progress bar
    const int maxPossibleLatency = 100000; // An assumed maximum latency for visualization, adjust as needed
    
    int position = barWidth * value / maxPossibleLatency;
    if (position > barWidth) position = barWidth; // Ensure position does not exceed barWidth

    printf("[%d] [", local_tid);
    for (int i = 0; i < barWidth; ++i) {
        if (i < position) printf("=");
        else if (i == position) printf(">");
        else printf(" ");
    }
    printf("] %d ns\n", value);
}
