#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>
#include <time.h>

// using namespace std;

#define PROCESS_OVER -9
#define TO_SCHEDULER 10
#define FROM_SCHEDULER 20 
#define TO_MMU 10
#define FROM_MMU 20 
#define PAGE_FAULT -1
#define INVALID_PAGE_REFERENCE -2

int len = 0;
int pages[100];

typedef struct MQ3_sendbuf{			//Send msg to MMU via MQ3
	long mtype;         	
	int id;
	int pageno;
}MQ3_sendbuf;

typedef struct MQ3_recvbuf{			//Receive msg from MMU via MQ3
	long mtype;          
	int frameno;
}MQ3_recvbuf;

struct MQ1buf{				//Send/recv msg to scheduler via MQ1
	long mtype;         
	int id;
};

void conv_ref_pages(char *ref)			//Get the page numbers to be accessed from the page reference string
{
	char *token;
	token=strtok(ref,"  ");
	while(token!=NULL)
	{
		// pages.push_back(atoi(token));		//Split by "  "
		pages[len++] = atoi(token); 
		token=strtok(NULL,"  ");
	}
}

int main(int argc, char *argv[])
{
	if (argc<4)
	{
		perror("Invalid Number of Arguments\n");		//Invalid number of args
		exit(1);
	}

	int pid,MQ1id, MQ3id;
	int i;
	pid = atoi(argv[1]);			//get various ids
	MQ1id = atoi(argv[2]);
	MQ3id = atoi(argv[3]);
	conv_ref_pages(argv[4]);
	
	// cout<<"Process id = "<<pid<<endl;		//Process id
	printf("Process id = %d\n",pid);

	struct MQ1buf msg_to_scheduler;
	msg_to_scheduler.mtype=TO_SCHEDULER;
	msg_to_scheduler.id=pid;
	int length=sizeof(struct MQ1buf)-sizeof(long);
	if (msgsnd(MQ1id,&msg_to_scheduler,length,0)==-1)		//Send the id to scheduler via MQ1 i.e ready queue enqueue is done
	{
		perror("Error in sending message to scheduler");
		exit(1);
	}
	
	struct MQ1buf msg_from_scheduler;						//Receive msg from scheduler (just done as a standard to wake up process)
	length=sizeof(struct MQ1buf)-sizeof(long);

	if (msgrcv(MQ1id,&msg_from_scheduler,length,FROM_SCHEDULER+pid,0)==-1)		
	{
		perror("Error in receiving message");
		exit(1);
	}

	MQ3_sendbuf msg_to_mmu;
	MQ3_recvbuf msg_from_mmu;

	// for(i=0;i<pages.size();i++)
	for(i=0;i<len;i++)
	{
		// cout<<"Sending request for Page "<<pages[i]<<".\n";			//Send request for a page to mmu
		printf("Sending request for Page %d.\n",pages[i]);
		msg_to_mmu.mtype=TO_MMU;
		msg_to_mmu.id=pid;
		msg_to_mmu.pageno=pages[i];
		length=sizeof(struct MQ3_sendbuf)-sizeof(long);
		if(msgsnd(MQ3id,&msg_to_mmu,length,0)==-1)
			{
				perror("Error in sending message");
				exit(1);
			}

		length=sizeof(struct MQ3_recvbuf)-sizeof(long);
		if(msgrcv(MQ3id,&msg_from_mmu,length,FROM_MMU+pid,0)==-1)		//Receive the msg from mmu
		{
			perror("Error in receiving message");
			exit(1);
		}

		if(msg_from_mmu.frameno>=0)					//If frame number is found
		{
			printf("MMU responded with frame number for process %d: %d\n",pid,msg_from_mmu.frameno);
			// cout<<"MMU responded with frame number for process "<<pid<<": "<<msg_from_mmu.frameno<<endl;
			i++;
		}
		else if(msg_from_mmu.frameno==PAGE_FAULT) 		//In case of page fault, now wait for scheduler to send a msg to process via MQ1
		{
			printf("Page Fault detected for process %d",pid);
			// cout<<"Page Fault detected for process "<<pid<<endl;
			length=sizeof(struct MQ1buf)-sizeof(long);

			if (msgrcv(MQ1id,&msg_from_scheduler,length,FROM_SCHEDULER+pid,0)==-1)
			{
				perror("Error in receiving message");
				exit(1);
			}
		}
		else if (msg_from_mmu.frameno==INVALID_PAGE_REFERENCE)		//Invalid page reference: terminate
		{
			printf("Invalid Page Reference for Process %d. Terminating the Process...\n",pid);
			// cout<<"Invalid Page Reference for Process "<<pid<<". Terminating the Process...\n";
			exit(1);
		}
	}
	
	printf("Process %d completed succcesfully\n",pid);
	// cout<<"Process "<<pid<<" completed successfully"<<endl;		//Completion of process, send -9 to MMU
	msg_to_mmu.pageno=PROCESS_OVER;
	msg_to_mmu.id=pid;
	msg_to_mmu.mtype=TO_MMU;
	length=sizeof(struct MQ3_sendbuf)-sizeof(long);
	if (msgsnd(MQ3id,&msg_to_mmu,length,0)==-1)	
		{
			perror("Error in sending message");
			exit(1);
		}
	return 0;
}

