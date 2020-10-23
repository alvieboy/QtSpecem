#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>

#include "../h/env.h"

#define SHMSZ     (128*1024) + (32*1024)
#define SHMVARS sizeof(struct Z80vars)+(sizeof(union Z80Regs)*2)+sizeof(struct CPU_flags) \
		+sizeof(union Z80IX)+sizeof(union Z80IY)

unsigned char * alloc_speccy_shared_ram(void)
{
    int shmid;
    key_t key;
    char *shm;

    /*
     * We'll name our shared memory segment
     * "5679".
     */
    key = 5679; /* 0x162f */

    /*
     * Create the segment.
     */
    if ((shmid = shmget(key, SHMSZ, IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    /*
     * Now we attach the segment to our data space.
     */
    if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        perror("shmat");
        exit(1);
    }

    return (unsigned char *)shm;
}

unsigned char * alloc_speccy_shared_vars(void)
{
    int shmid;
    key_t key;
    char *shm;

    /*
     * We'll name our shared memory segment
     * "5678".
     */
    key = 1234;

    /*
     * Create the segment.
     */
    if ((shmid = shmget(key, SHMVARS, IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    /*
     * Now we attach the segment to our data space.
     */
    if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        perror("shmat");
        exit(1);
    }

    return (unsigned char *)shm;
}

void release_speccy_shared_ram(unsigned char *ptr)
{
    shmdt(ptr);
}
