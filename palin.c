/*
$Id: palin.c,v 1.10 2017/09/25 06:35:46 o1-hester Exp $
$Date: 2017/09/25 06:35:46 $ 
$Revision: 1.10 $
$Log: palin.c,v $
Revision 1.10  2017/09/25 06:35:46  o1-hester
*** empty log message ***

Revision 1.9  2017/09/24 23:32:22  o1-hester
cleanup, modularization

Revision 1.8  2017/09/24 06:37:53  o1-hester
signals better.

Revision 1.7  2017/09/23 04:43:42  o1-hester
trying to set up signal handlers

Revision 1.6  2017/09/22 22:56:10  o1-hester
palindrome support

Revision 1.5  2017/09/20 06:50:22  o1-hester
child limit

Revision 1.4  2017/09/20 01:59:32  o1-hester
semaphonres set up

Revision 1.3  2017/09/17 22:50:15  o1-hester
shm to msgqueue

Revision 1.2  2017/09/17 21:24:28  o1-hester
shared memory work

Revision 1.1  2017/09/17 00:31:23  o1-hester
Initial revision

$Author: o1-hester $
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "ipchelper.h"
#include "sighandler.h"
#include "filehelper.h"
#define PALIN "./palin.out"
#define NOPALIN "./nopalin.out"

//long masterpid;
int semid;
struct sembuf mutex[2];

int palindrome(const char* string);
char* trimstring(const char* string);
int initsighandler();

int main (int argc, char** argv) {
	// check for # of args
	if (argc < 3) {
		fprintf(stderr, "Wrong # of args. ");
		return 1;
	}
	// if not number, then id, index = 0, respectively
	int id = atoi(argv[1]);
	int index = atoi(argv[2]);
	// random r1 r2 for sleep 
	int r1, r2;
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC, &tm);
	srand((unsigned)(tm.tv_sec ^ tm.tv_nsec ^ (tm.tv_nsec >> 31)));
	r1 = rand() % 3;
	r2 = rand() % 3;
	
	// get key from file
	key_t mkey, skey;
	if (((mkey = ftok(KEYPATH, PROJ_ID)) == -1) ||
		((skey = ftok(KEYPATH, SEM_ID)) == -1)) {
		perror("Failed to retreive keys.");
		return 1;
	}
	/*************** Set up signal handler ********/
	if (initsighandler() == -1) {
		perror("Failed to set up signal handlers");
		return 1;
	}
	/***************** Set up semaphore ************/
	//int semid;
	if ((semid = semget(skey, 3, PERM)) == -1) {
		if (errno == EIDRM) {
			perror("I cannot go on like this :(\n");
			return 1;
		}
		perror("Failed to set up semaphore.");
		return 1;
	}
	//struct sembuf mutex[2];
	struct sembuf signalDad[2];
	setsembuf(mutex, 0, -1, 0);
	setsembuf(mutex+1, 0, 1, 0);
	setsembuf(signalDad, 1, -1, 0);
	setsembuf(signalDad+1, 2, 1, 0);
	/**************** Set up message queue *********/
	int msgid;
	size_t size;
	mymsg_t mymsg;	
	if ((msgid = msgget(mkey, PERM)) == -1) {
		if (errno == EIDRM) {
			perror("I cannot go on like this :(\n");
			return 1;
		}
		perror("Failed to create message queue.");
		return 1;
	}
	// receive message into mymsg.mText
	if ((size = msgrcv(msgid, &mymsg, LINESIZE, index+1, 0)) == -1) {
		perror("Failed to recieve message.");
		return 1;
	}
	/************ Entry section ***************/	
	// wait until your turn
	if (semop(semid, mutex, 1) == -1){
		if (errno == EIDRM) {
			fprintf(stderr, "(ch:=%d) interrupted.\n", index);
			return 1;
		}
		perror("Failed to lock semid.");
		return 1;	
	}
	// Block all signals during critical section
	sigset_t newmask, oldmask; 	
	if ((sigfillset(&newmask) == -1) ||
		(sigprocmask(SIG_BLOCK, &newmask, &oldmask) == -1)) {
		perror("Failed setting signal mask.");
		return 1;
	} 
	/************ Critical section ***********/
	long pid = (long)getpid();	
	const time_t tma = time(NULL);
	char* tme = ctime(&tma);
	fprintf(stderr, "(ch:=%d) in crit sec: %s", index, tme); 
	sleep(r1);
	int p = palindrome(mymsg.mText);
	char* filename;
	if (p < 0) { filename = NOPALIN; }
	else { filename = PALIN; }
	// begin writing to file
	if (writeToFile(filename, pid, index, mymsg.mText) == -1) {
		// failed to open file
		perror("Failed to open file.");
		// unlock file
		if (semop(semid, mutex+1, 1) == -1)
			perror("Failed to unlock semid.");
		// decrement line counter
		if (semop(semid, signalDad, 2) == -1)
			perror("Failed to signal parent.");
		return 1;
	}	
	sleep(r2);
	/*********** Exit section **************/
	// Unblock signals after critical sections
	if ((sigprocmask(SIG_BLOCK, &newmask, &oldmask) == -1)) {
		perror("Failed setting signal mask.");
		return 1;
	} 
	// unlock file
	if (semop(semid, mutex+1, 1) == -1) { 		
		if (errno == EINVAL) {
			char* msg = "finished critical section after signal";
			fprintf(stderr, "(ch:=%d) %s\n", index, msg);
			return 1;
		}
		perror("Failed to unlock semid.");
		return 1;
	}
	// decrement line counter
	if (semop(semid, signalDad, 2) == -1) {
		perror("Failed to signal parent.");
		return 1;
	}
 	if (errno != 0) {
		perror("palin uncaught error:");
		return 1;
	}
	return 0;
}

// returns 1 if palindrome, -1 if not
int palindrome(const char* string) {
	int i = 0;
	char* trimmed = trimstring(string);
	int len = strlen(trimmed);
	int j = len - 1;
	for (i = 0; i < len/2; i++) {
		if (trimmed[i] != trimmed[j]) 
			return -1;
		j--;
	}
	return 1;
}

// returns the string with only alphanumeric lowercase characters
char* trimstring(const char* string) {
	char* trimmed = (char*)malloc(LINESIZE*sizeof(char));
	int i, j;
	char t;
	j = 0;
	for (i = 0; i < LINESIZE; i++) {
		t = string[i];	
		if (isalpha(t) || isdigit(t) || (t == '\0')) {
			if (isupper(t)) {
				t = tolower(t);	
			}
			trimmed[j] = t;
			if (trimmed[j] == '\0')
				break;
			j++;
		}
	} 	
	return trimmed;
}

// initialize signal handler, return -1 on error
int initsighandler() {
	struct sigaction act = {0};
	act.sa_handler = catchchildintr;
	act.sa_flags = 0;
	if ((sigemptyset(&act.sa_mask) == -1) ||
	    (sigaction(SIGINT, &act, NULL) == -1) ||
	    (sigaction(SIGALRM, &act, NULL) == -1)) {
		return -1;
	}	
	return 0;
}
