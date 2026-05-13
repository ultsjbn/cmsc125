/*
 * CMSC 125 - Dining Philosophers Problem
 * Final Project for CMSC 125 (Operating Systems)
 * Dyoco, Ito, Lopez, Novesteras
 * May 14, 2026
 *
 * Compile: gcc dining.c -o dining -pthread
 * Run:     ./dining
 *
 * Compatibility note:
 *   macOS does not support unnamed POSIX semaphores (sem_init/sem_destroy).
 *   This implementation uses named semaphores (sem_open/sem_close/sem_unlink)
 *   which are supported on both macOS and Linux.
 *
 * Deadlock Prevention Strategies Used:
 *   #1 Room Semaphore (N-1 Rule):
 *        At most N-1 philosophers may attempt to eat at once.
 *        This guarantees at least one philosopher can always acquire
 *        both chopsticks, making circular wait impossible.
 *
 *   #2 Asymmetric Chopstick Ordering (Resource Hierarchy):
 *        Even-numbered philosophers pick up LEFT then RIGHT.
 *        Odd-numbered philosophers pick up RIGHT then LEFT.
 *        This breaks the symmetry that causes circular wait.
 *
 * Together, these two strategies provide a formally correct
 * guarantee against deadlock. Either one alone would be sufficient;
 * both are included for demonstration and defense in depth.
 */

/* ------------ Headers ------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>      /* For O_CREAT, O_EXCL flags used by sem_open() */
#include <sys/stat.h>   /* For mode constants used by sem_open()         */

/* ------------ Configuration ------------ */

/* Number of philosophers (and chopsticks) at the table */
#define NUM_PHILOSOPHERS 5

/* How many meals each philosopher eats before leaving */
#define MEALS_PER_PHILOSOPHER 3

/* Maximum thinking time in seconds (actual = 1 to THINK_TIME_MAX) */
#define THINK_TIME_MAX 4

/* Maximum eating time in seconds (actual = 1 to EAT_TIME_MAX) */
#define EAT_TIME_MAX 3

/* Compile-time guard: TIME_MAX values must be at least 1 to avoid
 * undefined behavior from rand() % 0                                */
#if THINK_TIME_MAX < 1 || EAT_TIME_MAX < 1
#error "THINK_TIME_MAX and EAT_TIME_MAX must each be at least 1."
#endif

/* ------------ Named Semaphore Name Templates ------------ */
/*
 * Named semaphores live in the OS namespace and require unique string names.
 * We use "/chopstick_N" for each chopstick and "/dining_room" for the room.
 * These names must begin with '/' on both Linux and macOS.
 *
 * We unlink them at startup (in case a previous run crashed and left them
 * behind) and again at cleanup to remove them from the OS namespace.
 */
#define SEM_ROOM_NAME       "/dining_room"
#define SEM_CHOPSTICK_FMT   "/chopstick_%d"

/* ------------ State Enum ------------ */
/*
 * Represents the current lifecycle state of each philosopher.
 * Used for the state table display only — not used for synchronization.
 */
typedef enum {
    THINKING,   /* Philosopher is thinking, not competing for chopsticks */
    HUNGRY,     /* Philosopher wants to eat, waiting to enter room        */
    EATING,     /* Philosopher holds both chopsticks and is eating        */
    DONE        /* Philosopher has finished all meals                     */
} State;

/* ------------ Shared Synchronization Primitives ------------ */

/*
 * chopstick[i]: Named semaphore pointer representing the chopstick between
 * philosopher i and philosopher (i+1) % N. Initialized to 1 (available).
 * Only one philosopher may hold a given chopstick at a time.
 */
sem_t *chopstick[NUM_PHILOSOPHERS];

/*
 * room: Counting semaphore initialized to N-1.
 * A philosopher must acquire this before picking up any chopstick.
 * Ensures at most N-1 philosophers compete for chopsticks at once,
 * making circular wait — a necessary condition for deadlock — impossible.
 */
sem_t *room;

/*
 * log_mutex: Protects both console output and philosopher_state[].
 * A single mutex for both prevents interleaved output AND eliminates
 * the data race on philosopher_state[] reads and writes.
 */
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * philosopher_state[]: Tracks each philosopher's current state.
 * Must only be read or written while holding log_mutex.
 */
State philosopher_state[NUM_PHILOSOPHERS];

/* ------------ ANSI Color Codes ------------ */
static const char *COLORS[] = {
    "\033[1;31m",   /* Philosopher 0: Red     */
    "\033[1;32m",   /* Philosopher 1: Green   */
    "\033[1;33m",   /* Philosopher 2: Yellow  */
    "\033[1;34m",   /* Philosopher 3: Blue    */
    "\033[1;35m",   /* Philosopher 4: Magenta */
};
static const char *RESET = "\033[0m";

/* ------------ Helper: Timestamp ------------ */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%H:%M:%S", t);
}

/* ------------ Helper: Log a Message ------------ */
static void log_msg(int id, const char *msg) {
    char ts[16];
    get_timestamp(ts, sizeof(ts));
    pthread_mutex_lock(&log_mutex);
    printf("[%s] %sPhilosopher %d%s | %s\n",
           ts, COLORS[id], id, RESET, msg);
    pthread_mutex_unlock(&log_mutex);
}

/* ------------ Helper: Set State (thread-safe) ------------ */
/*
 * Updates philosopher_state[id] under log_mutex protection.
 * All writes to philosopher_state[] go through here so there is
 * exactly one enforced locking point, eliminating the data race
 * that occurs when threads write state directly without a lock.
 *
 * NOTE: Do not call while already holding log_mutex —
 * pthread mutexes are not reentrant and will self-deadlock.
 */
static void set_state(int id, State s) {
    pthread_mutex_lock(&log_mutex);
    philosopher_state[id] = s;
    pthread_mutex_unlock(&log_mutex);
}

/* ------------ Helper: Print State Table ------------ */
/*
 * Prints all philosopher states in a formatted table.
 * Acquires log_mutex before reading philosopher_state[] to prevent
 * a data race with concurrent set_state() calls.
 *
 * NOTE: Must NEVER be called while already holding log_mutex.
 */
static void print_state_table(void) {
    pthread_mutex_lock(&log_mutex);
    printf("\n  +------ State Table ------+\n");
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        const char *label;
        switch (philosopher_state[i]) {
            case THINKING: label = "THINKING"; break;
            case HUNGRY:   label = "HUNGRY  "; break;
            case EATING:   label = "EATING  "; break;
            case DONE:     label = "DONE    "; break;
            default:       label = "UNKNOWN "; break;
        }
        printf("  | %sPhilosopher %d%s : %s |\n",
               COLORS[i], i, RESET, label);
    }
    printf("  +-------------------------+\n\n");
    pthread_mutex_unlock(&log_mutex);
}

/* ------------ Helper: Unlink Named Semaphores ------------ */
/*
 * Removes all named semaphores from the OS namespace.
 * Called at startup to clear stale semaphores from a previous
 * crashed run, and again at shutdown for clean teardown.
 * Failures at startup are ignored — the semaphore may not exist yet.
 */
static void unlink_semaphores(void) {
    char name[32];
    sem_unlink(SEM_ROOM_NAME);
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        snprintf(name, sizeof(name), SEM_CHOPSTICK_FMT, i);
        sem_unlink(name);
    }
}

/* ------------ Helper: Open Named Semaphores ------------ */
/*
 * Creates and initializes all named semaphores via sem_open().
 * O_CREAT | O_EXCL ensures fresh creation (not reuse of stale ones).
 *
 * sem_open() arguments:
 *   name    — unique string in the OS namespace (must start with '/')
 *   flags   — O_CREAT: create if absent | O_EXCL: fail if already exists
 *   0644    — permissions: owner read/write, group/others read
 *   value   — initial semaphore count
 *
 * Returns SEM_FAILED on error; we exit immediately with a message.
 */
static void open_semaphores(void) {
    char name[32];

    room = sem_open(SEM_ROOM_NAME, O_CREAT | O_EXCL, 0644,
                    NUM_PHILOSOPHERS - 1);
    if (room == SEM_FAILED) {
        perror("sem_open (room)");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        snprintf(name, sizeof(name), SEM_CHOPSTICK_FMT, i);
        chopstick[i] = sem_open(name, O_CREAT | O_EXCL, 0644, 1);
        if (chopstick[i] == SEM_FAILED) {
            perror("sem_open (chopstick)");
            exit(EXIT_FAILURE);
        }
    }
}

/* ------------ Helper: Close Named Semaphores ------------ */
/*
 * Closes all semaphore handles, releasing the process's reference.
 * Must be followed by unlink_semaphores() to remove them from the OS.
 */
static void close_semaphores(void) {
    sem_close(room);
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        sem_close(chopstick[i]);
    }
}

/* ------------ Philosopher Thread Function ------------ */
/*
 * Each philosopher runs this function as its own thread.
 * Cycles through MEALS_PER_PHILOSOPHER iterations of:
 *   Think → Hungry → Enter room → Pick up chopsticks →
 *   Eat → Put down chopsticks → Leave room
 *
 * arg: pointer to the philosopher's integer ID (0 to N-1)
 */
void *philosopher(void *arg) {
    int id    = *(int *)arg;
    int left  = id;
    int right = (id + 1) % NUM_PHILOSOPHERS;
    char msg[80];

    for (int meal = 0; meal < MEALS_PER_PHILOSOPHER; meal++) {

        /* ---- THINKING ---- */
        set_state(id, THINKING);
        int think_time = (rand() % THINK_TIME_MAX) + 1;
        snprintf(msg, sizeof(msg), "is THINKING for %ds...", think_time);
        log_msg(id, msg);
        print_state_table();
        sleep(think_time);

        /* ---- HUNGRY: request entry to room ---- */
        /*
         * Deadlock Prevention #1: Room Semaphore (N-1 Rule)
         *
         * sem_wait(room) blocks if N-1 philosophers are already inside.
         * Limiting simultaneous chopstick-seekers to N-1 guarantees that
         * at least one philosopher can always complete eating, so
         * circular wait — a necessary condition for deadlock — is impossible.
         */
        set_state(id, HUNGRY);
        log_msg(id, "is HUNGRY -- waiting to enter the room");
        sem_wait(room);
        log_msg(id, "entered the room");

        /* ---- PICK UP CHOPSTICKS ---- */
        /*
         * Deadlock Prevention #2: Asymmetric Ordering (Resource Hierarchy)
         *
         * Even philosophers: LEFT first, then RIGHT.
         * Odd  philosophers: RIGHT first, then LEFT.
         *
         * This asymmetry ensures not all philosophers request resources
         * in the same circular direction, breaking the circular wait
         * condition that causes deadlock.
         *
         * sem_wait() is used (blocking), NOT sem_trywait().
         * Because the room semaphore limits to N-1 inside, blocking is
         * safe — no philosopher inside will wait forever. Deadlock
         * prevention comes from the room rule, not from non-blocking tries.
         *
         * Logging occurs AFTER sem_wait() returns so each message
         * accurately reflects that the chopstick is actually held.
         */
        if (id % 2 == 0) {
            sem_wait(chopstick[left]);
            snprintf(msg, sizeof(msg), "picked up LEFT chopstick #%d", left);
            log_msg(id, msg);

            sem_wait(chopstick[right]);
            snprintf(msg, sizeof(msg), "picked up RIGHT chopstick #%d", right);
            log_msg(id, msg);
        } else {
            sem_wait(chopstick[right]);
            snprintf(msg, sizeof(msg), "picked up RIGHT chopstick #%d", right);
            log_msg(id, msg);

            sem_wait(chopstick[left]);
            snprintf(msg, sizeof(msg), "picked up LEFT chopstick #%d", left);
            log_msg(id, msg);
        }

        /* ---- EATING ---- */
        set_state(id, EATING);
        int eat_time = (rand() % EAT_TIME_MAX) + 1;
        snprintf(msg, sizeof(msg),
                 "is EATING meal %d/%d for %ds",
                 meal + 1, MEALS_PER_PHILOSOPHER, eat_time);
        log_msg(id, msg);
        print_state_table();
        sleep(eat_time);

        /* ---- PUT DOWN CHOPSTICKS ---- */
        /*
         * Release chopsticks after eating. Order does not affect
         * correctness — the philosopher is done with both.
         * sem_post() signals each chopstick is available again.
         */
        sem_post(chopstick[left]);
        sem_post(chopstick[right]);
        log_msg(id, "put down both chopsticks");

        /* ---- LEAVE ROOM ---- */
        /*
         * Increment the room semaphore so a waiting hungry philosopher
         * may enter and attempt to acquire chopsticks.
         */
        sem_post(room);
        log_msg(id, "left the room");
    }

    /* ---- DONE ---- */
    set_state(id, DONE);
    log_msg(id, "finished all meals and left the table");
    print_state_table();

    return NULL;
}

/* ------------ Main ------------ */
int main(void) {
    /*
     * Seed with current time for non-deterministic think/eat durations.
     * Each run produces a different interleaving, better stress-testing
     * synchronization under varied conditions.
     */
    srand((unsigned)time(NULL));

    /* Remove stale named semaphores from any previous crashed run */
    unlink_semaphores();

    /* Create and initialize all named semaphores */
    open_semaphores();

    /* Initialize all philosopher states */
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        philosopher_state[i] = THINKING;
    }

    /* Print simulation header */
    printf("\033[1;36m");
    printf("===========================================\n");
    printf("  DINING PHILOSOPHERS -- Semaphore Demo\n");
    printf("  CMSC 125 | Process Synchronization\n");
    printf("===========================================\n");
    printf("%s", RESET);
    printf("  Philosophers : %d\n",   NUM_PHILOSOPHERS);
    printf("  Meals each   : %d\n",   MEALS_PER_PHILOSOPHER);
    printf("  Chopsticks   : %d\n",   NUM_PHILOSOPHERS);
    printf("  Room limit   : %d  (N-1 rule)\n", NUM_PHILOSOPHERS - 1);
    printf("  Think range  : 1-%ds\n", THINK_TIME_MAX);
    printf("  Eat range    : 1-%ds\n\n", EAT_TIME_MAX);

    /* ---- Create philosopher threads ---- */
    /*
     * ids[] stores each philosopher's ID so we safely pass a distinct
     * pointer per thread. Passing &i directly would be a data race —
     * all threads would share the same loop variable which may have
     * already incremented before a thread reads it.
     */
    pthread_t threads[NUM_PHILOSOPHERS];
    int ids[NUM_PHILOSOPHERS];

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        ids[i] = i;
        if (pthread_create(&threads[i], NULL, philosopher, &ids[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    /* Wait for all philosopher threads to complete */
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* ---- Cleanup ---- */
    close_semaphores();
    unlink_semaphores();
    pthread_mutex_destroy(&log_mutex);

    /*
     * Reaching here means all threads completed normally.
     * Combined with the two deadlock prevention strategies above,
     * this confirms the simulation ran to completion without deadlock.
     */
    printf("\n\033[1;32m");
    printf("===========================================\n");
    printf("  All philosophers finished.\n");
    printf("  Simulation completed without deadlock.\n");
    printf("===========================================\n");
    printf("%s\n", RESET);

    return 0;
}