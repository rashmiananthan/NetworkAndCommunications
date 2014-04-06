#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include "network.h"
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/ioctl.h>

/* Declaring the required global variables */
FILE *cbp;
int sp_count = 0, sp_total, collision = 0, buffer_free = 0, occupied_SPid = -1, maxfd, maxi;
char temp_buffer[MAXLINE];
int *client_fd;
fd_set allset;
typedef void    Sigfunc(int);

/* The structure which stores the details about the stations are declared here  */
typedef struct
{
int count;
int sp_id[10];
char data[10][MAXLINE];
}spframe;

spframe sp;

/* Functions used by CBP  */
void frames_start();
int sending_toSPs(char *, int);
void Inform_SP_Collision();
void collision_present();
Sigfunc * signal(int signo, Sigfunc *func);
Sigfunc * Signal(int signo, Sigfunc *func);

int main(int argc, char **argv)
{
 int i, listenfd, connfd, sockfd;
 int nready, client[FD_SETSIZE];
 ssize_t n;
 fd_set rset;
 char buf[MAXLINE];
 socklen_t clilen;
 struct sockaddr_in cliaddr, servaddr;

 sp.count = 0;
 client_fd = &client[0];

 printf("Enter the total number of SP's required for the setup: ");
 scanf("%d",&sp_total);

 /* Opening the log file for CBP */
 cbp = fopen("cbp_log.txt","w");
 if (!cbp) {fprintf(stderr, "could not open file cbp_log.txt file\n"); exit(-1);}
 setlinebuf(cbp);

 /* Creating the socket */
 listenfd = Socket(AF_INET, SOCK_STREAM, 0);

 bzero(&servaddr, sizeof(servaddr));
 servaddr.sin_family      = AF_INET;
 servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
 servaddr.sin_port        = htons(SERV_PORT);

 Bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr));

 /* Maximum allowed SP's is 10 */
 Listen(listenfd, 10);

 maxfd = listenfd;
 maxi = -1;
 for (i = 0; i < FD_SETSIZE; i++)
        client[i] = -1;
 FD_ZERO(&allset);
 FD_SET(listenfd, &allset);
 
 /* Connecting the clients */
 while(1)
 {
    rset = allset;
    nready = Select(maxfd+1, &rset, NULL, NULL, NULL);

    /* Checking if the listening socket is present */
    if (FD_ISSET(listenfd, &rset))
    {
      /* Connecting the client */
      clilen = sizeof(cliaddr);
      connfd = Accept(listenfd, (struct sockaddr*) &cliaddr, &clilen);
      for (i = 0; i < FD_SETSIZE; i++)
      {
        if (client[i] < 0)
        {
           /* Saving the descriptor */
           client[i] = connfd;
           break;
        }
      }
      if (i == FD_SETSIZE)
          printf("too many clients");

      FD_SET(connfd, &allset);        /* add new descriptor to set */
      if (connfd > maxfd)
          maxfd = connfd;                 /* for select */

      if(i > maxi)
        maxi = i;                        /* max index in client[] array */

      sp_count++;

      /* Checking if all the SP has been connected */
      if(sp_count == sp_total)
      {
         fprintf(cbp, "All SP's are connected\n");
         fprintf(cbp, "Sending signal to all the SP's\n");
         printf("sending signal to sender\n");
         sleep(2);         
         for(i = 0; i <= maxi; i++)
         {
            sprintf(buf, "start %d\n",i+1);
	    write(client[i], buf, strlen(buf));
            fprintf(cbp, "Message sent to SP %d : %s\n",i,buf);
         }
      }
      if (--nready <= 0)
        continue;                               /* no more readable descriptors */
    }

    for (i = 0; i < maxi; i++)
    {       /* check all clients for data */
       if ( (sockfd = client[i]) < 0)
          continue;
       if (FD_ISSET(sockfd, &rset))
       {
         if ( (n = Read(sockfd, buf, MAXLINE)) == 0)
         {  /*connection closed by client */
           Close(sockfd);
           FD_CLR(sockfd, &allset);
           client[i] = -1;
           sp_count--;
         } 
        else
        {
          /* Reading the data from the SP */
	  fprintf(cbp,"\nReading from SP's\n");
	  memcpy(sp.data[sp.count], buf, sizeof(buf));
      	  sp.sp_id[sp.count] = i+1;
          sp.count++;
        }

        if (--nready <= 0)
           break;                          /* no more readable descriptors */
      }
    }
/* Closing the respective SP */
 if(sp_count<=0)
 {
 fclose(cbp);
 Close(listenfd);
 exit(0);
 }
   frames_start();
  }
}

/* frames_start() - handles the incoming frames, collision scenarios, informs stations if collision is present, in case if no collision is present sends the frames */
void frames_start()
{
int i, success = 0, partid, frameid, destid, sourceid;

if(sp.count > 1)
{
  collision = 1;
  fprintf(cbp, "Collision occured as there are more than 2 senders simultaneously\n");
  for(i = 0; i < sp.count; i++)
  {
    sscanf(sp.data[i], "From Station %d sending part %d of Frame %d to SP %d", &sourceid, &partid, &frameid, &destid);
    fprintf(cbp, "Received part %d of frame %d from Station %d to Station %d\n", partid, frameid, sourceid, destid);
  }
  fprintf(cbp, "Informing collision to the SPs\n");
  Inform_SP_Collision();
}

if(collision > 0)
{
  collision = 0;
  sp.count = 0;
  return;
}

for(i = 0; i < sp.count; i++)
{
  if((success = sendingToSPs(sp.data[i],sp.sp_id[i]))<0) 
  {
    fprintf(cbp, "Sending to stations failed\n");
    break;
  }
}

if(collision)
{
  Inform_SP_Collision();
}

collision = 0;
sp.count = 0;
return;
}

/* sendingToSPs takes care of sending the frames to the respective SP
   input - input from the station, sending station's ID
   output - 0 is successful, -1 if fails*/
int sendingToSPs(char *input, int SPid)
{
int sourceid=0, partid=0, frameid=0, destid=0;
char sendframe[MAXLINE];

sscanf(input, "From Station %d sending part %d of Frame %d to SP %d", &sourceid, &partid, &frameid, &destid);
fprintf(cbp, "Received part %d of frame %d from Station %d to Station %d\n", partid, frameid, sourceid, destid); 

if(destid > sp_total)
{
  fprintf(cbp, "Skipping information from Sender %d to Receiver %d as Receiver SP not present\n", sourceid, destid);
  return -1;
}

sprintf(sendframe, "Part %d of frame %d from Station %d\n", partid, frameid, destid);

if(partid == 1)
{
  if(!buffer_free)
  {
    strcpy(temp_buffer, sendframe);
    fprintf(cbp, "Storing Part 1 of frame %d from Station %d to Station %d in temporary buffer\n", frameid, sourceid, destid);
    buffer_free = 1;
    occupied_SPid = sourceid;
  }
  else
  {
    collision = 1;
    fprintf(cbp, "Collision occured as temporary buffer is occupied when 1st part of frame %d is to be sent from Station %d to Station %d\n", frameid, sourceid, destid);
  }
}
else if(partid == 2)
{
  if(buffer_free && (occupied_SPid == sourceid))
  {
    fprintf(cbp, "Sending part 1 of frame %d from station %d to station %d\n", frameid, sourceid, destid);

    collision_present();

    if(collision)
    {
      fprintf(cbp, "Collision occured while sending part 1 of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);
      return -1;  
    }
    else
    {
      fprintf(cbp, "Transferring part 1 of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);
      
      if(client_fd[destid-1] >= 0) write(client_fd[destid-1],temp_buffer, strlen(temp_buffer));
      else 
      {
        fprintf(cbp, "Destination Sp %d unavailable\n", destid);
        return -1;
      }
    }
    
    fprintf(cbp, "Successful transfer of part 1 of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);
    fprintf(cbp, "Now transfer part 2 of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);

    collision_present();

    if(collision)
    {
      fprintf(cbp, "Collision occured while transferring part 2 of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);
      return -1;
    }
    else
    {
      fprintf(cbp, "Transferring part 2 of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);

      if(client_fd[destid-1] >= 0) write(client_fd[destid-1], sendframe, strlen(sendframe));
      else
      {
        fprintf(cbp, "Destination Sp %d unavailable\n", destid);
        return -1;
      }
    }
   fprintf(cbp, "Successful transfer of frame %d from Station %d to Station %d\n", frameid, sourceid, destid);
   buffer_free = 0;
   occupied_SPid = -1;
  }
 }
 return 1;
}
/* Inform_SP_Collision will inform the stations about the collision if present */
void Inform_SP_Collision()
{
int i = 0;

for(i = 0; i < sp.count; i++)
{
   Writen(client_fd[sp.sp_id[i]-1], "collision", 11);
   fprintf(cbp, "Informing SP %d about a collision \n", sp.sp_id[i]);

   if(occupied_SPid == sp.sp_id[i])
   {
     buffer_free = 0;
     occupied_SPid = -1;
   }
}

if(occupied_SPid != -1)
{
  Writen(client_fd[occupied_SPid - 1], "collision", 11);
  fprintf(cbp, "Informing SP %d about a collision \n", occupied_SPid);
  buffer_free = 0;
  occupied_SPid = -1;
}
return;
}
/* Checks for the prsence of collision using select() */
void collision_present()
{
fd_set read_fd = allset;
int sockfd, i, select_num, sending_sp_count =0, n;
char buffer[MAXLINE];
struct timeval	time;
	
time.tv_sec = 0;
time.tv_usec = 0;

select_num = select(maxfd + 1, &read_fd, NULL, NULL, &time);
	
if(select_num)
{
  for (i = 0; i <= maxi; i++) 
  {	
    sockfd = client_fd[i];
    if (sockfd < 0) 
	continue;
    if (FD_ISSET(sockfd, &read_fd)) 
    {
      n = Read(sockfd, buffer, sizeof(buffer));
      if (n == 0) 
      {
        Close(sockfd);
	FD_CLR(sockfd, &allset);
	client_fd[i] = -1;
	sp_count--;
      }
      else
      {
        memcpy(sp.data[sp.count], buffer, strlen(buffer));
	sp.sp_id[sp.count++] = i + 1;
	sending_sp_count++;
      }
      select_num--; 
      if (select_num <= 0)
	break;	
    }
  }
  if(sending_sp_count > 0)
  {
     fprintf(cbp, "Collision, more than one SPs are sending messages.\n");
     collision = 1;
  }
}
return;
}
