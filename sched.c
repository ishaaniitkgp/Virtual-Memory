#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#define FROM_PROCESS 10				
#define TO_PROCESS 20  
#define PAGE_FAULT_HANDLED 1
#define TERMINATED 2

struct MQ2buf{
	long mtype;
	char mbuf[1];
};

struct MQ1buf{
	long mtype;
	int id;
};

int k; 

int main(int argc , char * argv[])
{
	if (argc<5)
	{
		printf("Invalid Number of Arguments\n");
		printf("Usage: %s <MQ1id> <MQ2id> <k> <master_pid>\n",argv[0]);
		exit(1);
	}

	int MQ1id,MQ2id,master_pid,length,curr_pid;
	MQ1id=atoi(argv[1]);	//get the id of the message queues MQ1 and MQ2
	MQ2id=atoi(argv[2]);
	k=atoi(argv[3]);
	master_pid=atoi(argv[4]);

	struct MQ1buf msg_to_process,msg_from_process;
	struct MQ2buf msg_from_mmu;

	int n_terminated=0; 

	while (1)
	{
		length=sizeof(struct MQ1buf)-sizeof(long);
		if(msgrcv(MQ1id,&msg_from_process,length,FROM_PROCESS,0)==-1)	//select the 1st process from the ready queue
		{
			perror("Error in receiving message");
			exit(1);
		}

		curr_pid=msg_from_process.id;

		msg_to_process.mtype=TO_PROCESS+curr_pid;
		msg_to_process.id=curr_pid;

		length=sizeof(struct MQ1buf)-sizeof(long);
		if (msgsnd(MQ1id,&msg_to_process,length,0)==-1)	//send message to the selected process to start execution
		{
			perror("Error in sending message");
			exit(1);
		}

		length=sizeof(struct MQ2buf)-sizeof(long);
		if(msgrcv(MQ2id,&msg_from_mmu,length,0,0)==-1)	//receive message from MMU
		{
			perror("Error in receiving message");
			exit(1);
		}
		if (msg_from_mmu.mtype==PAGE_FAULT_HANDLED)
		{
			//if message type if PAGE_FAULT_HANDLED, then add the current process to the end of ready queue
			msg_from_process.mtype=FROM_PROCESS;
			msg_from_process.id=curr_pid;
			length=sizeof(struct MQ1buf)-sizeof(long);
			if (msgsnd(MQ1id,&msg_from_process,length,0)==-1)
			{
				perror("Error in sending message");
				exit(1);
			}
		}
		else if(msg_from_mmu.mtype==TERMINATED)
		{	
			n_terminated++;	//increment the number of processes terminated
		}
		else
		{
			printf("Incorrect Message Received\n");
			exit(1);
		}
		if (n_terminated==k) break; //if all processes have terminated, then break
	}

	kill(master_pid,SIGUSR1);	//send signal to Master to terminate all the modules
	pause();					//wait for signal from Master
	printf("Terminating Scheduler\n");
	exit(1);
}