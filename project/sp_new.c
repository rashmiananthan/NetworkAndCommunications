/* Imporitng the required header */
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>	
#include <sys/time.h>
#include <time.h>		
#include <netinet/in.h>
#include <signal.h>	
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>	
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "stdio.h"
#include "time.h"
#include "network.h"

/* Required variables declaration */
int sockfd=0, start=0, collision=0, collisioncount=0, done=0, busy=0, sp_id=0, frame_num=0, station_dest=0;
FILE *spfile;
char file_input[MAXLINE];
typedef void    Sigfunc(int);

/* required function declarations */
void ip_from_cbp(int);
void str_frames_start();
void SleepOneTimeSlot();
void Bebo_algm();
int CheckForInput(int, int);
Sigfunc * signal(int signo, Sigfunc *func);
Sigfunc * Signal(int signo, Sigfunc *func);

/* main function is called by the stations during the start of the program
   input : executable, ip_address of the CBP, input file name */
int main(int argc, char **argv)	
{
  int i;
  struct sockaddr_in servaddr; 
  const int const_ioctl = 1; 

  if (argc != 3) {fprintf(stderr, "usage: %s ip_address input_file_name", argv[0]); exit(-1);}
	
  sprintf(file_input, "%s", argv[2]);

  sockfd = Socket(AF_INET, SOCK_STREAM, 0);	

  /* register the signal handlers*/
  Signal(SIGIO, ip_from_cbp);
  fcntl(sockfd, F_SETOWN, getpid());
  ioctl(sockfd, FIOASYNC, &const_ioctl);
  ioctl(sockfd, FIONBIO, &const_ioctl);

  bzero(&servaddr, sizeof(servaddr));	
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(SERV_PORT);	
  inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
	
  connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
  	
  str_frames_start();
  return;
}

/* str_frames_start - starts the process of processing the frames by reading from the input file specified and sending it to the CBP */
void str_frames_start()
{
  char ip_from_file[MAXLINE], logfile[MAXLINE], sendline[MAXLINE];
  int frame_number;
  FILE *fp;

  fp = Fopen(file_input,"r");
  if(!fp) { printf("Error occured while opening file: %s \n", file_input);exit(0);}
  setlinebuf(fp);

  collision = 0;
  collisioncount = 0;
  start = 0;
  done = 0;

  while(1)
  {
     if(start == 1)
	break;
  }

  sprintf(logfile, "SP%d.txt", sp_id);
  spfile = Fopen(logfile,"w");
  setlinebuf(spfile);

  while(1)
  {
    collision = 0;
    collisioncount = 0;
		
    if((Fgets(ip_from_file, MAXLINE, fp)) == NULL)
    {
	if(feof(fp))
	{
	   fprintf(spfile, "End of file\n");
done = 1;
           break;				
	}
	else 
	{
	   fprintf(spfile, "Error Reading input file.\n");
done = 1;
           break;
	}
    }
		
    fprintf(spfile, "Reading from file : %s\n", ip_from_file);
    sscanf(ip_from_file, "Frame %d, To Station %d", &frame_num, &station_dest);
    printf("Frame = %d, From SP %d To SP %d \n", frame_num, sp_id, station_dest);
	
    if(sp_id == station_dest)
    {
       fprintf(spfile, "Skipping this frame as sending SP and receiving SP are same\n");
    }

    frame_number = 0;
    
    while(!frame_number)
    {
	if(collisioncount > 16)	break;
			
        sprintf(sendline, "From Station %d sending part 1 of Frame %d to SP %d \n", sp_id, frame_num, station_dest);			
			
        busy = 1;
	Write(sockfd, sendline, strlen(sendline));
	fprintf(spfile, "Sending 1st part of Frame %d from SP %d to SP %d \n", frame_num, sp_id, station_dest);
			
	sleep(1);		
	
	if(collision)
	{
	   busy = 0;
           fprintf(spfile,"Collision has occured\n");
	   collisioncount++;
           Bebo_algm();
	   collision = 0;
	   continue;
	}
	
        sprintf(sendline, "From Station %d sending part 2 of Frame %d to SP %d \n", sp_id, frame_num, station_dest);			
	Write(sockfd, sendline, strlen(sendline));
	fprintf(spfile, "Sending 2nd part of Frame %d from SP %d to SP %d \n", frame_num, sp_id, station_dest);
	
        if(!collision)
	{
	   SleepOneTimeSlot();
	}
			
	if(collision)
	{
	   busy = 0;
           fprintf(spfile,"Collision has occured\n");
	   collisioncount++;
	   Bebo_algm();
	   collision = 0;
	   continue;
	}
        
        frame_number = 1;
	fprintf(spfile, "Station %d has successfully sent Frame %d to Station %d \n", sp_id, frame_num, station_dest);
    }
		
    if(collisioncount > 16)
    {
	fprintf(spfile, "Number of collisions > 16 => Dropping Frame %d to Station %d \n", frame_num, station_dest);
    }
   }
  
   printf("All frames were sent successfully.\n");
   fprintf(spfile, "SP is Done processing all the inputs\n");
	
   while(!done);
   fprintf(spfile, "SP will shut down.\n");
	
   fclose(spfile);
   fclose(fp);
   Close(sockfd);
   exit(0);
}

/* CheckForInput - checks for the presence of input from the CBP using select function 
   input - socket descriptor, time */
int CheckForInput(int fd, int usec)
{
  fd_set setfd;
  struct timeval time;
  int result;

  FD_ZERO(&setfd);
  FD_SET(fd, &setfd);

  time.tv_sec = 0;
  time.tv_usec = usec;

  result = select(fd + 1, &setfd, NULL, NULL, &time);
  if(result > 0 && FD_ISSET(fd, &setfd))
	return result;
  else
	return 0;
}

/* ip_from_cbp - signal function catch the message from the CBP */
void ip_from_cbp(int signal)
{
  char readBuff[MAXLINE];

  if(CheckForInput(sockfd, 10000))
  {
 	read(sockfd, readBuff, MAXLINE);
	printf("%s", readBuff);
		
        if(!strncmp(readBuff, "start", 5))
	{
	    sscanf(readBuff, "start %d", &sp_id);
	    start = 1;
	    printf("start:SP %d \n", sp_id);
	}
	else
	{
	    fprintf(spfile, "In coming message from CBP: %s", readBuff);
	    printf("In coming message from CBP: %s", readBuff);

	    if(!strncmp(readBuff, "collision", 9)) collision = 1;
	    else if(busy) collision = 1;
	}
   }
  return;
}

/* SleepOneTimeSlot - sleeps for the specified time */
void SleepOneTimeSlot()
{
	struct timeval	waittime;

	waittime.tv_sec = 0;
	waittime.tv_usec = 10 * 100;
	select(0,NULL,NULL,NULL,&waittime);
	return; 
}

/* Bebo_algm() - simulates the bebo algorithm by calculating the random time slots and waiting for the specified time */
void Bebo_algm()
{
  int timeslots = 2;
  int randomtimeslot = 0;
  int index = 0;
	
  if(collisioncount > 10)
	collisioncount = 10;
	
  for(index = 0; index < collisioncount; index++) timeslots = timeslots * 2;		
fprintf(spfile,"Collision count:%d\n", collisioncount);	
  timeslots--;
	
  srand(time(NULL));
  randomtimeslot = rand() % timeslots;
	
  fprintf(spfile, "Random Wait time slot is %d \n", randomtimeslot);
	
  for(index = 0; index < randomtimeslot; index++) SleepOneTimeSlot();
}

//Function to register signals and callbacks
Sigfunc * signal(int signo, Sigfunc *func)
{
	struct sigaction	act, oact;

	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (signo == SIGALRM) {
#ifdef	SA_INTERRUPT
		act.sa_flags |= SA_INTERRUPT;	/* SunOS 4.x */
#endif
	} else {
#ifdef	SA_RESTART
		act.sa_flags |= SA_RESTART;		/* SVR4, 44BSD */
#endif
	}
	if (sigaction(signo, &act, &oact) < 0)
		return(SIG_ERR);
	return(oact.sa_handler);
}


Sigfunc * Signal(int signo, Sigfunc *func)
{
	Sigfunc	*sigfunc;

	if ( (sigfunc = signal(signo, func)) == SIG_ERR)
		printf("signal error");
	return(sigfunc);
}

