#include <sys/types.h>                                                    
#include <sys/stat.h>                                                     
#include <fcntl.h>                                                        
#include <termios.h>                                                      
#include <stdio.h>                                                        
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
                                                                          
#define BAUDRATE B921600 
#define MODEMDEVICE "/dev/ttyACM0"
#define _POSIX_SOURCE 1 /* POSIX compliant source */                      
#define FALSE 0                                                           
#define TRUE 1                                                            
                                                                          
volatile int STOP=FALSE;                                                  
                                                                          
int main()                                                                    
{                                                                         
  int fd,c, res;                                                          
  struct termios oldtio,newtio;                                           
  char buf[255];                                                          
                                                                          
  fd = open(MODEMDEVICE, O_RDWR | O_NOCTTY );                             
  if (fd <0) {perror(MODEMDEVICE); exit(-1); }                            
                                                                          
  tcgetattr(fd,&oldtio); /* save current port settings */                 
                                                                          
  bzero(&newtio, sizeof(newtio));                                         
  newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;             
  newtio.c_iflag = IGNPAR;                                                
  newtio.c_oflag = 0;                                                     
                                                                          
  /* set input mode (non-canonical, no echo,...) */                       
  newtio.c_lflag = 0;                                                     
                                                                          
  newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */         
  newtio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */ 
                                                                          
  tcflush(fd, TCIFLUSH);                                                  
  tcsetattr(fd,TCSANOW,&newtio);                                          
                                                                          
  for(;;) {
    res = read(fd, buf, 255);
    if(res < 0)
      break;
    for(int n=0; n < res; ++n)
      printf("%c", buf[n]);
  }                                                                       
  tcsetattr(fd,TCSANOW,&oldtio);                                          
}                                                                         

