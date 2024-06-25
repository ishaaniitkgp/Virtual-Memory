#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <limits.h>
#include <sys/shm.h>
#include <stdbool.h>
#include <fcntl.h>
#include <string.h>

#define FROM_PROCESS 10				//To send a msg to process
#define TO_PROCESS 20				//To recv a msg from process
#define INVALID_PAGE_REFERENCE -2	//Page reference not valid
#define PAGE_FAULT -1				//Page fault
#define PROCESS_OVER -9				//Process is over
#define PAGE_FAULT_HANDLED 1		//Type 1 msg
#define TERMINATED 2				//Type 2 msg

int timestamp=0;					//Global timestamp
int fault_freq[10000];			//Frequency of page faults
int fault_freq_index=0;			//Index for the frequency of page faults
int outfile;

typedef struct{						//Page Table Entry has the Frame number, valid/invalid and time of use (timestamp)
	int frame;
	bool valid;
	int time;
}PTentry;

typedef struct{						//To enter necessary information for a process: The id, number of pages, allocated number of frames and used number of frames
	int pid;
	int m;
	int allocount;
	int usecount;
}process;

typedef struct{						//Free Frame List: Stores the size of the list and the free frames available
	int size;
	int ffl[];
}FFL;

//Message Queue Structures

typedef struct {					//To receive id and pageno from the process via MQ3	
	long mtype;
	int id;
	int pageno;
}MQ3_recvbuf;

typedef struct {					//To send frameno to process via MQ3
	long mtype;
	int frameno;
}MQ3_sendbuf;

typedef struct {						//To send msg to scheduler via MQ2
	long mtype;
	char mbuf[1];
}MQ2buf;

int shmid1, shmid2;					//ids for various queues and shared memories
int MQ2id, MQ3id;
int PCBid;

process *PCB;						//Structures
PTentry *PT;
FFL *freeFL;

int m,k;

void sendFrameNo(int id, int frame)			//send frame number to process specified by id
{
	MQ3_sendbuf msg_to_process;
	int length;

	msg_to_process.mtype=TO_PROCESS+id;		//msg to process
	msg_to_process.frameno=frame;
	length=sizeof(MQ3_sendbuf)-sizeof(long);

	if(msgsnd(MQ3id,&msg_to_process,length,0)==-1)	//send frame number
	{
		perror("Error in sending message");
		exit(1);
	}
}

void sendMsgToScheduler(int type)			//send type1/type2 msg to scheduler
{
	MQ2buf msg_to_scheduler;
	int length;

	msg_to_scheduler.mtype=type;
	length=sizeof(MQ2buf)-sizeof(long);

	if(msgsnd(MQ2id,&msg_to_scheduler,length,0)==-1)	//send the msg
	{
		perror("Error in sending message");
		exit(1);
	}
}


int handlePageFault(int id,int pageno)			//handle the page faults
{
	int i,frameno;
	if(freeFL->size==0||PCB[id].usecount>PCB[id].allocount)			//if there is no free frame or if the page has all its allocated number of frames used
	{
		int min=INT_MAX,mini=-1;			//find the frame with the minimum timestamp, specifying the LRU policy
		for(i=0;i<PCB[id].m;i++)
		{
			if(PT[id*m+i].valid==true)
			{
				if(PT[id*m+i].time<min)
				{
					min=PT[id*m+i].time;	//minimum timestamp is found
					mini = i;
				}
			}
		}
		PT[id*m+mini].valid=false;			//that page table entry is made invalid
		frameno=PT[id*m+mini].frame;		//corresponding frame is returned 
	}

	else
	{
		frameno=freeFL->ffl[freeFL->size-1];		//otherwise get a free frame and allot it to the corresponding process
		freeFL->size-=1;
		PCB[id].usecount++;
	}
	return frameno;
}

void freeFrames(int id)					//When a process is over/terminated, free all the frames allotted to it
{
	int i= 0;
	for(i= 0;i<PCB[i].m;i++)
	{
		if(PT[id*m+i].valid==true)
		{
			freeFL->ffl[freeFL->size]=PT[id*m+i].frame;		//add the frame to FFL
			freeFL->size += 1;								//increase the size 
		}
	}
}

void serviceMessageRequest()			//Service message requests
{
	int id,pageno,length,frameno,i,found;
	int mintime,mini;
	MQ3_recvbuf msg_from_process;
	MQ3_sendbuf msg_to_process;
	length=sizeof(MQ3_recvbuf)-sizeof(long);
	if(msgrcv(MQ3id,&msg_from_process,length,FROM_PROCESS,0)==-1)		//Receive a msg from the process
	{
		perror("Error in receiving message");
		exit(1);
	}
	id=msg_from_process.id;
	pageno=msg_from_process.pageno;				//Retrieve the process id and page number requested

	if (pageno==PROCESS_OVER)					//if -9 is received, free frames and send type 2 msg to scheduler
	{
		freeFrames(id);
		sendMsgToScheduler(TERMINATED);
		return;
	}

	timestamp++;								//Increase the timestamp

	printf("Page reference: (%d, %d, %d)\n",timestamp,id,pageno);
	char temp[100];
	memset(temp,0,sizeof(temp));
	sprintf(temp,"Page reference: (%d, %d, %d)\n",timestamp,id,pageno);
	write(outfile,temp,strlen(temp));

	if (pageno>PCB[id].m||pageno<0)				//If we refer to an invalid page number
	{
		printf("Invalid Page Reference: (%d, %d)\n",id,pageno);
		char buffer[100];
		memset(buffer,0,sizeof(buffer));
		sprintf(buffer,"Invalid Page Reference: (%d, %d)\n",id,pageno);
		write(outfile,buffer,strlen(buffer));
		
		sendFrameNo(id,INVALID_PAGE_REFERENCE);	//Send invalid reference to process

		freeFrames(id);							//Free frames and terminate the process
		sendMsgToScheduler(TERMINATED);
	}

	else 							//if a valid page numeber is used
	{
		if(PT[id*m+pageno].valid==true)			//if found in page table but not in TLB
		{
			frameno=PT[id*m+pageno].frame;

			// updateTLB(id,pageno,frameno);		//update TLB and return frame number
			sendFrameNo(id,frameno);
			PT[id*m+pageno].time=timestamp;
		}
		else
		{
			printf("Page Fault: (%d, %d)\n",id,pageno);
			char buffer[100];
			memset(buffer,0,sizeof(buffer));
			sprintf(buffer,"Page Fault: (%d, %d)\n",id,pageno);
			write(outfile,buffer,strlen(buffer));
			fault_freq[id]+=1;
			sendFrameNo(id,PAGE_FAULT);			//otherwise we get a page fault, we handle the page fault, update TLB and PT
			frameno=handlePageFault(id,pageno);
			PT[id*m+pageno].valid=true;
			PT[id*m+pageno].time=timestamp;
			PT[id*m+pageno].frame=frameno;
			sendMsgToScheduler(PAGE_FAULT_HANDLED);		//tell scheduler that page fault is handled
		}
	}	
}

void complete(int signo)			//Signal Handler for SIGUSR1
{
	int i;
	if(signo==SIGUSR2) 
	{

		printf("Frequency of Page Faults for Each Process:\n");	
		char buf[100];
		memset(buf,0,sizeof(buf));
		sprintf(buf,"Frequency of Page Faults for Each Process:\n");
		write(outfile,buf,strlen(buf));
		printf("PID\tFrequency\n");
		memset(buf,0,sizeof(buf));
		sprintf(buf,"PID\tFrequency\n");
		write(outfile,buf,strlen(buf));
		for(i=0;i<k;i++)
		{
			printf("%d\t%d\n",i,fault_freq[i]);
			memset(buf,0,sizeof(buf));
			sprintf(buf,"%d\t%d\n",i,fault_freq[i]);
			write(outfile,buf,strlen(buf));
		}

		shmdt(PCB);					//Detach various shared memory segments
		shmdt(PT);
		shmdt(freeFL);
		close(outfile);			//close the file
		exit(0); 
	}
}

int main(int argc, char const *argv[])			//Main Function
{
	signal(SIGUSR2,complete);					//Install Signal Handler to get the signals
	sleep(1);									//Just to show the context switch for better visualisation, otherwise the page access gets completed within 250 ms
	if (argc<8)
	{
		perror("Invalid Number of Arguments\n");
		exit(1);
	}

	MQ2id=atoi(argv[1]);						//Get various ids and other parameters
	MQ3id=atoi(argv[2]);
	shmid1=atoi(argv[3]);
	shmid2=atoi(argv[4]);
	PCBid=atoi(argv[5]);
	m=atoi(argv[6]);
	k=atoi(argv[7]);

	int i;

	for(i=0;i<k;i++) fault_freq[i]=0;	//Page faults for all processes initially 0
	
	PCB=(process *)(shmat(PCBid,NULL,0));		//Attach the various data structures to the shared memory via the id
	PT=(PTentry *)(shmat(shmid1,NULL,0));
	freeFL=(FFL *)(shmat(shmid2,NULL,0));

	outfile = open("result.txt",O_CREAT|O_WRONLY|O_TRUNC,0666);		//Open the output file
	while(1)
	{
		serviceMessageRequest();				//Service the various requests received
	}
	return 0;
}