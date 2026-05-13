/*
	CMSC 125 - Dining Philosophers Problem
	Final Project for CMSC 125 (Operating Systems)
	Dyoco, Ito, Lopez, Novesteras
	May 14, 2026

	Compile: gcc dppfinal.c -o dppfinal
	Run: ./dpp

	Deadlock Prevention Strategies Used:
	1. Room Semaphore (N-1 Rule):
		At most N-1 philosophers may attempt to eat at once.
		This guarantees at least one philosopher can always acquire
		both chopsticks, making circular wait impossible.

	2. Asymmetric Chopstick Ordering (Resource Hierarchy):
		Even-numbered philosophers pick up LEFT then RIGHT.
		Odd-numbered philosophers pick up RIGHT then LEFT.
		This breaks the symmetry that causes circular wait.
*/

/* ------------ Headers ------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

/* ------------ Configuration ------------ */

/* Number of philosophers (and chopsticks) at the table */
#define NUM_PHILOSOPHERS 5
/* How many meals each philosopher eats before leaving */
#define MEALS_PER_PHILOSOPHER 3
/* Maximum thinking time in seconds (actual = 1 to THINK_TIME_MAX) */
#define THINK_TIME_MAX 4
/* Maximum eating time in seconds (actual = 1 to EAT_TIME_MAX) */
#define EAT_TIME_MAX 3

/* TIME_MAX values must be at least 1 to avoid undefined behavior from rand() % 0 */
#if THINK_TIME_MAX < 1 || EAT_TIME_MAX < 1
#error "THINK_TIME_MAX and EAT_TIME_MAX must each be at least 1."
#endif

/* ------------ State Enum ------------ */
typedef enum {
	THINKING, /* Philosopher is thinking, not competing for chopsticks */
	HUNGRY,   /* Philosopher wants to eat, waiting to enter room */
	EATING,   /* Philosopher holds both chopsticks and is eating */
	DONE      /* Philosopher has finished all meals */
} State;

/* ------------ Shared Synchronization Primitives ------------ */

/* chopstick[i]: Binary semaphore representing the chopstick between philosopher i and (i+1)%N. */
sem_t chopstick[NUM_PHILOSOPHERS];

/* room: Counting semaphore initialized to N-1. */
sem_t room;

/* log_mutex: Protects both console output and philosopher_state[]. */
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* philosopher_state[]: Tracks the current state of each philosopher. */
State philosopher_state[NUM_PHILOSOPHERS];

/* ------------ ANSI Color Codes ------------ */
static const char *COLORS[] = {
	"\033[1;31m", /* Philosopher 0: Red */
	"\033[1;32m", /* Philosopher 1: Green */
	"\033[1;33m", /* Philosopher 2: Yellow */
	"\033[1;34m", /* Philosopher 3: Blue */
	"\033[1;35m", /* Philosopher 4: Magenta */
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

/* ------------ Helper: Print State Table ------------ */
static void print_state_table(void) {
	pthread_mutex_lock(&log_mutex);

	printf("\n  +-- State Table -----------+\n");

	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		const char *label;
		switch (philosopher_state[i]) {
			case THINKING: label = "THINKING"; break;
			case HUNGRY:   label = "HUNGRY";   break;
			case EATING:   label = "EATING";   break;
			case DONE:     label = "DONE";     break;
			default:       label = "UNKNOWN";  break;
		}
		printf("  | %sPhilosopher %d%s : %-8s |\n", COLORS[i], i, RESET, label);
	}
		
	printf("  +--------------------------+\n\n");
	pthread_mutex_unlock(&log_mutex);
}

/* ------------ Philosopher Thread Function ------------ */
/*
	Each philosopher runs this function as its own thread.
	The philosopher cycles through MEALS_PER_PHILOSOPHER iterations of:
	Think -> Get hungry -> Enter room -> Pick up chopsticks -> Eat -> Put down chopsticks -> Leave room
	arg: pointer to the philosopher's integer ID (0 to N-1)
 */
void *philosopher(void *arg) {
	int id = *(int *)arg;
	int left = id; /* Chopstick to this philosopher's left  */
	int right = (id + 1) % NUM_PHILOSOPHERS; /* Chopstick to this philosopher's right */
	char msg[80];

	for (int meal = 0; meal < MEALS_PER_PHILOSOPHER; meal++) {

		/* THINKING */
		philosopher_state[id] = THINKING;
		int think_time = (rand() % THINK_TIME_MAX) + 1;
		snprintf(msg, sizeof(msg), "is THINKING for %ds...", think_time);
		log_msg(id, msg);
		print_state_table();
		sleep(think_time);

		/* HUNGRY: request entry to room */
		philosopher_state[id] = HUNGRY;
		log_msg(id, "is HUNGRY -- waiting to enter room");
		sem_wait(&room);
		log_msg(id, "entered the room");

		/* PICK UP CHOPSTICKS */
		/* ------------------------------------------------------------------------
			Deadlock Prevention #2: Asymmetric Ordering (Resource Hierarchy)
			Even philosophers: acquire LEFT chopstick first, then RIGHT.
			Odd  philosophers: acquire RIGHT chopstick first, then LEFT.
		------------------------------------------------------------------------ */
		if (id % 2 == 0) {
			/* Even philosopher: LEFT then RIGHT */
			sem_wait(&chopstick[left]);
			snprintf(msg, sizeof(msg), "picked up LEFT  chopstick #%d", left);
			log_msg(id, msg);

			sem_wait(&chopstick[right]);
			snprintf(msg, sizeof(msg), "picked up RIGHT chopstick #%d", right);
			log_msg(id, msg);
		} else {
			/* Odd philosopher: RIGHT then LEFT */
			sem_wait(&chopstick[right]);
			snprintf(msg, sizeof(msg), "picked up RIGHT chopstick #%d", right);
			log_msg(id, msg);

			sem_wait(&chopstick[left]);
			snprintf(msg, sizeof(msg), "picked up LEFT  chopstick #%d", left);
			log_msg(id, msg);
		}

		/* EATING */
		philosopher_state[id] = EATING;
		int eat_time = (rand() % EAT_TIME_MAX) + 1;
		snprintf(msg, sizeof(msg), "is EATING meal %d/%d for %ds", meal + 1, MEALS_PER_PHILOSOPHER, eat_time);
		log_msg(id, msg);
		print_state_table();
		sleep(eat_time);

		/* PUT DOWN CHOPSTICKS */
		sem_post(&chopstick[left]);
		sem_post(&chopstick[right]);
		log_msg(id, "put down both chopsticks");

		/* LEAVE ROOM */
		sem_post(&room);
		log_msg(id, "left the room");
	}

	/* DONE */
	philosopher_state[id] = DONE;
	log_msg(id, "finished all meals and left the table");
	print_state_table();

	return NULL;
}

/* ------------ Main ------------ */
int main(void) {
	srand((unsigned)time(NULL));

	/* Initialize all philosopher states */
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		philosopher_state[i] = THINKING;
	}

	printf("\033[1;36m");
	printf("===========================================\n");
	printf("  DINING PHILOSOPHERS PROBLEM\n");
	printf("  CMSC 125 | Process Synchronization\n");
	printf("===========================================\n");
	printf("%s", RESET);
	printf("  Philosophers : %d\n", NUM_PHILOSOPHERS);
	printf("  Meals each   : %d\n", MEALS_PER_PHILOSOPHER);
	printf("  Chopsticks   : %d\n", NUM_PHILOSOPHERS);
	printf("  Room limit   : %d  (N-1 rule)\n", NUM_PHILOSOPHERS - 1);
	printf("  Think range  : 1-%ds\n", THINK_TIME_MAX);
	printf("  Eat range    : 1-%ds\n\n", EAT_TIME_MAX);

	/* ------------------------------------------------------------------------
		Deadlock Prevention #1: Room Semaphore (N-1 Rule)
		Only N-1 philosophers can attempt to eat at the same time.
		This guarantees at least one philosopher can always acquire both chopsticks,
		preventing circular wait and thus deadlock.
	------------------------------------------------------------------------ */
	sem_init(&room, 0, NUM_PHILOSOPHERS - 1);
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		sem_init(&chopstick[i], 0, 1);
	}

	/* Create philosopher threads */
	pthread_t threads[NUM_PHILOSOPHERS];
	int ids[NUM_PHILOSOPHERS];

	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		ids[i] = i;
		if (pthread_create(&threads[i], NULL, philosopher, &ids[i]) != 0) {
			perror("pthread_create");
			exit(EXIT_FAILURE);
		}
	}

	/* Wait for all philosophers to finish */
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		pthread_join(threads[i], NULL);
	}

	/* Cleanup */
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		sem_destroy(&chopstick[i]);
	}
	sem_destroy(&room);
	pthread_mutex_destroy(&log_mutex);

	/* Final message */
	printf("\n\033[1;32m");
	printf("===========================================\n");
	printf("  All philosophers finished.\n");
	printf("  Simulation completed without deadlock.\n");
	printf("===========================================\n");
	printf("%s\n", RESET);

	return 0;
}