#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
 
/* Configuration*/
#define NUM_PHILOSOPHERS     5
#define MEALS_PER_PHILOSOPHER 3
#define THINK_TIME_MAX       2   /* Max seconds thinking */
#define EAT_TIME_MAX         2   /* Max seconds eating   */
 
/*Philosopher states*/
typedef enum { THINKING, HUNGRY, EATING, DONE } State;
 
/* Shared synchronization primitives */
sem_t chopstick[NUM_PHILOSOPHERS]; /* One semaphore per chopstick  */
sem_t room;                        /* Room: at most N-1 inside     */
 
/* Logging mutex */
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
 
/*State tracking*/
State philosopher_state[NUM_PHILOSOPHERS];
 
/* ANSI color codes */
static const char *COLORS[] = {
	"\033[1;31m",  /* Red     */
	"\033[1;32m",  /* Green   */
	"\033[1;33m",  /* Yellow  */
	"\033[1;34m",  /* Blue    */
	"\033[1;35m",  /* Magenta */
};
static const char *RESET = "\033[0m";
 
/* Helper: timestamp string*/
void get_timestamp(char *buf, size_t size) {
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	strftime(buf, size, "%H:%M:%S", t);
}
 
/*Helper: log a message*/
void log_msg(int id, const char *msg) {
	char ts[16];
	get_timestamp(ts, sizeof(ts));
	pthread_mutex_lock(&log_mutex);
	printf("[%s] %sPhilosopher %d%s | %s\n",
		   ts, COLORS[id], id, RESET, msg);
	pthread_mutex_unlock(&log_mutex);
}
 
/*Helper: print state table*/
void print_state_table(void) {
	pthread_mutex_lock(&log_mutex);
	printf("\n  State Table: ");
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		switch (philosopher_state[i]) {
			case THINKING: printf("[P%d:THINK]  ", i); break;
			case HUNGRY:   printf("[P%d:HUNGRY] ", i); break;
			case EATING:   printf("[P%d:EAT]    ", i); break;
			case DONE:     printf("[P%d:DONE]   ", i); break;
		}
	}
	printf("\n\n");
	pthread_mutex_unlock(&log_mutex);
}
 
/* Philosopher thread function */
void *philosopher(void *arg) {
	int id    = *(int *)arg;
	int left  = id;
	int right = (id + 1) % NUM_PHILOSOPHERS;
	char msg[64];
 
	for (int meal = 0; meal < MEALS_PER_PHILOSOPHER; meal++) {
 
		/*THINKING */
		philosopher_state[id] = THINKING;
		int think_time = (rand() % THINK_TIME_MAX) + 1;
		snprintf(msg, sizeof(msg), "is THINKING for %ds...", think_time);
		log_msg(id, msg);
		print_state_table();
		sleep(think_time);
 
		/*HUNGRY: enter room*/
		philosopher_state[id] = HUNGRY;
		log_msg(id, "is HUNGRY -- waiting to enter the room");
		sem_wait(&room);   /* Only N-1 may attempt at once */
 
		/*  PICK UP CHOPSTICKS (deadlock prevention) */
		/* Even: left first, then right */
		/* Odd:  right first, then left */
		if (id % 2 == 0) {
			snprintf(msg, sizeof(msg),
					 "picks up LEFT  chopstick #%d", left);
			log_msg(id, msg);
			sem_wait(&chopstick[left]);
 
			snprintf(msg, sizeof(msg),
					 "picks up RIGHT chopstick #%d", right);
			log_msg(id, msg);
			sem_wait(&chopstick[right]);
		} else {
			snprintf(msg, sizeof(msg),
					 "picks up RIGHT chopstick #%d", right);
			log_msg(id, msg);
			sem_wait(&chopstick[right]);
 
			snprintf(msg, sizeof(msg),
					 "picks up LEFT  chopstick #%d", left);
			log_msg(id, msg);
			sem_wait(&chopstick[left]);
		}
 
		/* EATING */
		philosopher_state[id] = EATING;
		int eat_time = (rand() % EAT_TIME_MAX) + 1;
		snprintf(msg, sizeof(msg),
				 "is EATING (meal %d/%d) for %ds",
				 meal + 1, MEALS_PER_PHILOSOPHER, eat_time);
		log_msg(id, msg);
		print_state_table();
		sleep(eat_time);
 
		/*PUT DOWN CHOPSTICKS */
		sem_post(&chopstick[left]);
		sem_post(&chopstick[right]);
		log_msg(id, "puts down both chopsticks");
 
		/* LEAVE ROOM */
		sem_post(&room);
	}
 
	philosopher_state[id] = DONE;
	log_msg(id, "has finished all meals and left the table");
	print_state_table();
 
	return NULL;
}
 
/* Main */
int main(void) {
	srand(42);  /* Fixed seed for reproducibility */
 
	printf("\033[1;36m");
	printf("DINING PHILOSOPHERS -- Semaphore Demo\n");
	printf("CMSC 125 | Process Synchronization\n");
	printf("%s\n", RESET);
	printf("  Philosophers : %d\n", NUM_PHILOSOPHERS);
	printf("  Meals each   : %d\n", MEALS_PER_PHILOSOPHER);
	printf("  Chopsticks   : %d\n", NUM_PHILOSOPHERS);
	printf("  Room limit   : %d (N-1 rule)\n\n", NUM_PHILOSOPHERS - 1);
 
	/* Initialize semaphores */
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		sem_init(&chopstick[i], 0, 1);       /* Each chopstick available */
		philosopher_state[i] = THINKING;
	}
	sem_init(&room, 0, NUM_PHILOSOPHERS - 1); /* Allow N-1 at once */
 
	/* Create philosopher threads */
	pthread_t threads[NUM_PHILOSOPHERS];
	int ids[NUM_PHILOSOPHERS];
 
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		ids[i] = i;
		pthread_create(&threads[i], NULL, philosopher, &ids[i]);
	}
 
	/* Wait for all threads to finish */
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		pthread_join(threads[i], NULL);
	}
 
	/* Cleanup */
	for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
		sem_destroy(&chopstick[i]);
	}
	sem_destroy(&room);
	pthread_mutex_destroy(&log_mutex);
 
	printf("\n\033[1;32m");
	printf("All philosophers finished. No deadlock. \n");
	printf("%s\n", RESET);
 
	return 0;
}

