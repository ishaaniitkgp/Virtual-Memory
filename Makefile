all: process.c mmu.c master.c sched.c
	gcc master.c -o master
	gcc sched.c -o scheduler
	gcc mmu.c -o mmu 
	gcc process.c -o process 

clean:
	rm process master scheduler mmu