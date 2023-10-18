/**
 * Homework 3 - CS 446/646
 * File: sched.c
 * Author: JoJo Petersky
 * Last Revision: 2023-10-16
 */
// Required header files for pthread functionality,
// I/O operations, memory management, and system calls
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
// Merged from the supplied print_progress function
// Enables retrieval of thread ID for progress reporting
#include <sys/syscall.h>
// Merged from the supplied print_progress function
// Enables memory mapping operations for progress reporting
#include <sys/mman.h>
// Define limits of long long integers to ensure a standard
// range across platforms, imit random values for overflow safety
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define RANGE_LIMIT (LLONG_MAX / 2)
// Define size of the array and threshold for iteration,
// based on performance considerations and range limits
#define ARRAY_SIZE 2000000
#define ITERATION_THRESHOLD 1000000
// ANSI escape codes for terminal color manipulation
// Used to make the terminal output more user-friendly and distinguishable
#define ANSI_COLOR_GRAY    "\x1b[30m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[37m"
#define ANSI_COLOR_RESET   "\x1b[0m"
// Clear the terminal screen & move cursor to specific location
#define TERM_CLEAR() printf("\033[H\033[J") //
#define TERM_GOTOXY(x,y) printf("\033[%d;%dH", (y), (x))
// Struct for passing thread-specific data. Organizes all necessary
// variables for multithreaded array summation, including array
// segment information, synchronization constructs, and shared totals
typedef struct _thread_data_t {
    int localTid;
    int *dataStart;
    int dataSize;
    int numberOfThreads;
    pthread_mutex_t *lock;
    int iterationCount;
    long long int *totalSum;
    volatile int *shouldExit;
    pthread_barrier_t *startBarrier;
} thread_data_t;
// Function prototypes
void* arraySum(void *arg);
void print_progress(pid_t localTid, size_t value);
// Enter main function: core program logic and multithreading orchestration
int main(int argc, char *argv[]) {
    // Disable output buffering for immediate terminal updates
    setvbuf(stdout, NULL, _IONBF, 0);
    // Setup vars for tracking thread statuses, errors
    int retVal = 0, threadsCreated = 0, lockInitialized = 0;
    long long int *totalSum = (long long int *) calloc(1, sizeof(long long int));
    // Allocate mem for threads & thread data structs for dynamic thread count
    pthread_t *threads = NULL;
    thread_data_t *threadData = NULL;
    // Use current time to seed random generator for unpredictability
    srand(time(NULL));
    // Mutex lock for synchronized access to shared resources among threads
    pthread_mutex_t lock;
    // Confirm expected user input: program name + number of threads
    if (argc != 2) {
        printf("Usage: %s <number of threads>\n", argv[0]);
        return -1;
    }
    // Dynamic memory allocation for main data array & a termination flag
    int *data = (int *) malloc(ARRAY_SIZE * sizeof(int));
    volatile int *shouldExitFlag = calloc(1, sizeof(int));
    // Ensure memory allocation was successful
    if (!data || !shouldExitFlag) {
        perror("Failed to allocate memory for data structures");
        retVal = -1;
        goto cleanup;
    }
    // Populate data array with randomized numbers
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = (rand() % RANGE_LIMIT) - (RANGE_LIMIT / 2);
    }
    // Extract and validate number of threads input by user
    char *endptr;
    int numberOfThreads = strtol(argv[1], &endptr, 10);
    // Ensure valid thread count within range for performance and resources
    if (*endptr != '\0' || numberOfThreads <= 0) {
        fprintf(stderr, "Invalid number of threads. Must be greater than 1.\n");
        return -1;
    }
    // Memory allocation for managing thread state and metadata
    threads = (pthread_t *) malloc(numberOfThreads * sizeof(pthread_t));
    threadData = (thread_data_t *) malloc(numberOfThreads * sizeof(thread_data_t));
    // Validate memory allocation success for thread management
    if (!threads || !threadData) {
        perror("Failed to allocate memory for threads or thread data structures");
        retVal = -1;
        goto cleanup;
    }
    // Initialize mutex for synchronized access
    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("Mutex initialization failed");
        retVal = -1;
        goto cleanup;
    }
    lockInitialized = 1; // Track mutex initialization for later cleanup
    
    // Initialize barrier for starting threads simultaneously
    pthread_barrier_t startBarrier;
    if (pthread_barrier_init(&startBarrier, NULL, numberOfThreads) != 0) {
        perror("Barrier initialization failed");
        retVal = -1;
        goto cleanup;
    }
    // Determine the portion of the array each thread should process
    int portionSize = ARRAY_SIZE / numberOfThreads;
    // Initialize and launch threads, distributing data processing tasks
    for (int i = 0; i < numberOfThreads; i++) {
        // Track number of threads successfully created for error recovery
        threadsCreated++;
        // Assign thread-specific data for parallel processing
        threadData[i].localTid = i;
        threadData[i].dataStart = data + i * portionSize;
        threadData[i].dataSize = (i == numberOfThreads - 1) ? ARRAY_SIZE - i * portionSize : portionSize;
        threadData[i].lock = &lock;
        threadData[i].iterationCount = 0;
        threadData[i].totalSum = totalSum;
        threadData[i].shouldExit = shouldExitFlag;
        threadData[i].numberOfThreads = numberOfThreads;
        threadData[i].startBarrier = &startBarrier;
        // Commence the thread and manage potential thread creation errors
        if (pthread_create(&threads[i], NULL, arraySum, &threadData[i]) != 0) {
            perror("Thread creation failed");
            // Ensure graceful shutdown by signaling threads to terminate
            *shouldExitFlag = 1;
            // Wait for any already started threads to conclude before cleanup
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            retVal = -1;
            goto cleanup;
        }
    }
    // Ensure main waits for all threads to complete tasks before moving forward
    for (int i = 0; i < numberOfThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    // Deallocate resources and handle cleanup for any premature exits
    cleanup:
    // Safely free dynamically allocated memory for various data structures
    if (threads) {
        free(threads);
        threads = NULL;
    }
    if (threadData) {
        free(threadData);
        threadData = NULL;
    }
    if (data) {
        free(data);
        data = NULL;
    }
    if (shouldExitFlag) {
        free((int *)shouldExitFlag);
        shouldExitFlag = NULL;
    }
    if (totalSum) {
        free(totalSum);
        totalSum = NULL;
    }
    // Ensure proper destruction of synchronization constructs
    if (lockInitialized && pthread_mutex_destroy(&lock) != 0) {
        perror("Failed to destroy lock mutex");
        retVal = -1;
    }
    if (pthread_barrier_destroy(&startBarrier) != 0) {
        perror("Failed to destroy barrier");
        retVal = -1;
    }
    // Return indicative value of program execution success or failure
    return retVal;
}
// Thread function responsible for summing a segment of an array concurrently
void* arraySum(void *arg) {
    // Convert the argument to the appropriate struct pointer for clearer access
    thread_data_t *data = (thread_data_t *) arg;
    // Variables to store local thread sum and the maximum latency observed
    long long int threadSum = 0;
    long long int latency_max = LLONG_MIN;
    struct timespec start_time, end_time;
    // Synchronize thread start times to ensure concurrent execution
    pthread_barrier_wait(data->startBarrier);
    // Infinite loop to continuously sum array until external termination signal
    while (1) {
        // Reset the sum for each new iteration of summing
        threadSum = 0;
        // Capture the start time for this iteration to measure latency
        if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
            perror("Error: Failed to get start time using clock_gettime.");
            pthread_exit(NULL);
        }
        // Check for overflow conditions based on current sum and next value
        for (int i = 0; i < data->dataSize; i++) {
            if ((threadSum > 0 && data->dataStart[i] > LLONG_MAX - threadSum) ||
                (threadSum < 0 && data->dataStart[i] < LLONG_MIN - threadSum)) {
                printf("Local potential overflow detected in thread %d. Exiting...\n", data->localTid);
                *data->shouldExit = 1;
                pthread_exit(NULL); // Exit safely if overflow is detected
            }
            threadSum += data->dataStart[i]; // Add the current value to the sum
        }
        // Capture end time to compute the elapsed time for this iteration
        if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
            perror("clock_gettime end_time");
            pthread_exit(NULL);
        }
        // Calculate elapsed time in nanoseconds
        long long int elapsed_ns = (end_time.tv_sec - start_time.tv_sec) * 1e9 + (end_time.tv_nsec - start_time.tv_nsec);
        // Update the maximum latency if current latency is higher
        if (elapsed_ns > latency_max) {
            latency_max = elapsed_ns;
        }
        // If external signal is set, unlock and exit safely
        if (*data->shouldExit) {
            pthread_mutex_unlock(data->lock);
            pthread_exit(NULL);
        }
        // Lock to safely update the shared global sum
        pthread_mutex_lock(data->lock);
        // Check for external exit signal again while locked
        if (*data->shouldExit) {
            pthread_mutex_unlock(data->lock);
            // Exit after lock for safe termination w/o blocking other threads
            pthread_exit(NULL);
        }
        // Check for overflow for the global sum
        if ((*data->totalSum > 0 && threadSum > LLONG_MAX - *data->totalSum) ||
            (*data->totalSum < 0 && threadSum < LLONG_MIN - *data->totalSum)) {
            printf("Potential overflow detected in totalSum. Exiting...\n");
            *data->shouldExit = 1;
            pthread_mutex_unlock(data->lock);
            pthread_exit(NULL); // Exit if overflow while updating global sum
        } else {
            // Otherwise, add the local sum to the global sum
            *data->totalSum += threadSum;
        }
        // Only the first thread resets the global sum for the next iteration
        if (data->localTid == 0) {
            *data->totalSum = 0;
        }
        // Update iteration count & check against threshold for possible exit
        data->iterationCount++;
        if (data->iterationCount >= ITERATION_THRESHOLD) {
            pthread_mutex_unlock(data->lock); // Unlock before exiting
            pthread_exit(NULL); // Exit after reaching the threshold
        }
        // Unlock so other threads can update the shared sum
        pthread_mutex_unlock(data->lock);
        // Print progress with maximum latency observed in this iteration
        print_progress(data->localTid, latency_max);
        // Reset maximum latency for the next iteration
        latency_max = 0;
    }
    return NULL;
}
/**
 * print_progress function provided by instructor.
 *
 * The function displays a visual progress bar in the terminal
 * corresponding to the `value` argument for a thread with
 * the given `localTid`. Different threads can be visually
 * differentiated based on colors.
 *
 * @param localTid: The local thread ID, for positioning and color determination.
 * @param value: Current progress value for the thread (usually time or iteration count).
 */
void print_progress(pid_t localTid, size_t value) {
        pid_t tid = syscall(__NR_gettid);

        TERM_GOTOXY(0,localTid+1);

    char prefix[256];
        size_t bound = 100;
        sprintf(prefix, "%d: %ld (ns) \t[", tid, value);
    const char suffix[] = "]";
    const size_t prefix_length = strlen(prefix);
    const size_t suffix_length = sizeof(suffix) - 1;
    char *buffer = calloc(bound + prefix_length + suffix_length + 1, 1);
    size_t i = 0;

    strcpy(buffer, prefix);
    for (; i < bound; ++i)
    {
        buffer[prefix_length + i] = i < value/10000 ? '#' : ' ';
    }
    strcpy(&buffer[prefix_length + i], suffix);
        
        if (!(localTid % 7))
            printf(ANSI_COLOR_WHITE "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else if (!(localTid % 6))
            printf(ANSI_COLOR_BLUE "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else if (!(localTid % 5))
            printf(ANSI_COLOR_RED "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else if (!(localTid % 4))
            printf(ANSI_COLOR_GREEN "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else if (!(localTid % 3))
            printf(ANSI_COLOR_CYAN "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else if (!(localTid % 2))
            printf(ANSI_COLOR_YELLOW "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else if (!(localTid % 1))
            printf(ANSI_COLOR_MAGENTA "\b%c[2K\r%s\n" ANSI_COLOR_RESET, 27, buffer);
        else
            printf("\b%c[2K\r%s\n", 27, buffer);

    fflush(stdout);
    free(buffer);
}
