//Client file for project 1b
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ulimit.h>
#include <zlib.h>


const int size = 256;

z_stream compressedData;
z_stream decompressedData;

int compressfl = 0;
int portNu = 0;

int logflag;
char * logflagname;
int logfd;

struct termios saved;

void init_compress_stream(z_stream * stream){
  int ret = 0;
	  //stream->zalloc, stream->zfree: function pointers to malloc/free, Z_NULL to use the
    //default one. stream->opaque: data passed to customized heap allocator;
    stream->zalloc = Z_NULL, stream-> zfree = Z_NULL, stream-> opaque = Z_NULL;
    //Second arg: compression degress: 0-9, 9: most aggressive compression, slow.
	  //Z_DEFAULT_COMPRESSION: default compression degree.
	  ret = deflateInit(stream, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
       fprintf(stderr, "compression deflateInit string couldn't initialize \n");
	  }
}

void init_decompress_stream(z_stream * stream){
  int ret = 0;
	  //stream->zalloc, stream->zfree: function pointers to malloc/free, Z_NULL to use the
    //default one. stream->opaque: data passed to customized heap allocator;
    stream->zalloc = Z_NULL, stream-> zfree = Z_NULL, stream-> opaque = Z_NULL;
    //Second arg: compression degress: 0-9, 9: most aggressive compression, slow.
	  //Z_DEFAULT_COMPRESSION: default compression degree.
	  ret = inflateInit(stream);
    if (ret != Z_OK) {
       fprintf(stderr, "compression inflateInit string couldn't initialize \n");
	  }
}

void resetAtEnd(){
  if(tcsetattr(0, TCSANOW, &saved) < 0){
    fprintf(stderr, "error resetting termios modes");
    exit(1);
  }
}

void terminalSetup(){
  struct termios temp;
  tcgetattr(0, &temp);

  saved = temp;

  temp.c_iflag = ISTRIP;
  temp.c_oflag = 0;
  temp.c_lflag  = 0;

  tcsetattr(0, TCSANOW, &temp);
  atexit(resetAtEnd);
}

//Discussion 1A template function for socket setup
int socketSetup(){


   struct sockaddr_in serv_addr;
   struct hostent* server;
   int sockfd;

   sockfd = socket(AF_INET, SOCK_STREAM, 0);
  //AF_INET --> IPv4

  server = gethostbyname("localhost");
  if (server == NULL) {
      fprintf(stderr,"ERROR, no such host\n");
      exit(1);
  }

  //converting host name to ip address
  memset(&serv_addr, 0, sizeof(struct sockaddr_in));
  serv_addr.sin_family = AF_INET;
  memcpy((char * )&serv_addr.sin_addr.s_addr, (char * ) server->h_addr, server->h_length);

  //copying host name from server to serv_addr
  serv_addr.sin_port = htons(portNu);
  if(connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0){
    fprintf(stderr, "Error with connecting on client.\n");
		exit(1);
  }
  return sockfd;

}

void logflagsent(char * buf, int cnt){  //I don't why this sends bytes that are only byte by byte but whatever
  char number[3];
  sprintf(number, "%d", cnt);
  if (write(logfd, "SENT ", 5) < 0)
			fprintf(stderr, "Error writing to log file");
	write(logfd, number, strlen(number));
	if (write(logfd, " bytes: ", 8) < 0)
			fprintf(stderr, "Error writing to log file");
	if (write(logfd, buf, cnt) < 0)
			fprintf(stderr, "Error writing to log file");
	if (write(logfd, "\n", 1) < 0)
			fprintf(stderr, "Error writing to log file");
}

void logflagrecieved(char * buf, int cnt){
  char number[3];
  sprintf(number, "%d", cnt);
  if (write(logfd, "RECEIVED ", 9) < 0)
      fprintf(stderr, "Error writing to log file");
  write(logfd, number, strlen(number));
  if (write(logfd, " bytes: ", 8) < 0)
      fprintf(stderr, "Error writing to log file");
  if (write(logfd, buf, cnt) < 0)
      fprintf(stderr, "Error writing to log file");
  if (write(logfd, "\n", 1) < 0)
      fprintf(stderr, "Error writing to log file");
}

void toservercompression(char * buf, int cnt, int fd){
  char newbuf[size];
  compressedData.next_in = (unsigned char *) buf;
  compressedData.avail_in = cnt;
  compressedData.next_out = (unsigned char *) newbuf;
  compressedData.avail_out = size;

  while (compressedData.avail_in > 0)
      deflate(&compressedData, Z_SYNC_FLUSH);

  write(fd, newbuf, size - compressedData.avail_out);
  if(logflag == 1)
    logflagsent(newbuf, size - compressedData.avail_out);

}

void decompressdata(char * buf, int cnt){
  char newbuf[1024];
  decompressedData.next_in = (unsigned char *) buf;
  decompressedData.avail_in = cnt;
  decompressedData.next_out = (unsigned char *) newbuf;
  decompressedData.avail_out = 1024;

  while (decompressedData.avail_in > 0)
      inflate(&decompressedData, Z_SYNC_FLUSH);

  //write(to_shell[1], newbuf, 1024 - compressedData.avail_out);
  write(1, newbuf, 1024 - decompressedData.avail_out);
}

void writing(){
  int sockfd = socketSetup();
  //Sockfd is now the file descriptor to use for reading and writing with the server
  char buffer[size];

  struct pollfd poll_list[2];

  poll_list[0].events = POLLIN | POLLERR | POLLHUP;
  poll_list[1].events = POLLIN | POLLERR | POLLHUP;
  poll_list[0].fd = 0;
  poll_list[1].fd = sockfd;

  int ret;
  //Poll through a while loop
  while(1){
    ret = poll(poll_list, 2, -1);
    if(ret < 0){
      fprintf(stderr, "error using poll() on client");
      exit(1);
    }

    if (poll_list[0].revents & POLLIN) {   //This is directly from keyboard ---> giving straight to echo and show on screen + to shell
			int count = read(0, buffer, sizeof(buffer));
      // forward to stdout
      int i = 0;
      for(; i < count; i++){
        if (buffer[i] == '\r' || buffer[i] == '\n'){
          write(1, "\r\n", sizeof(char)*2);
        }
        else{
          write(1, &buffer[i], sizeof(char));
        }
      }
      if(compressfl == 1){
        toservercompression(buffer, count, sockfd);
      }else{
        write(sockfd, buffer, count);
        if(logflag == 1)
          logflagsent(buffer, count);  //Something wrong with this, idk what tho?
      }
		}
    if (poll_list[1].revents & POLLIN) {  //Getting output from shell directly to stdout
      int count = read(sockfd, buffer, sizeof(buffer));
      if(count == 0){
        exit(0);
      }
      if(logflag == 1 && compressfl == 1){
        logflagrecieved(buffer, count);
      }
      else if(logflag == 1){
        logflagrecieved(buffer, count);
      }
      if(compressfl == 1){
        decompressdata(buffer, count);
      }
      else{
        // for(; i < count; i++){
        //     write(1, &buffer[i], sizeof(char));
        // }
        int j = 0;
        char new_buf[1024]="";
        int new_buf_i = 0;
        for (; j < count; j++) {
          if (buffer[j] == '\n') {
            new_buf[new_buf_i] = '\r';
            new_buf[new_buf_i+1] = '\n';
            new_buf_i = new_buf_i + 2;
          }
          else {
            new_buf[new_buf_i] = buffer[j];
            new_buf_i++;
          }
        }
        new_buf[new_buf_i] = -1;
        write(1, new_buf, new_buf_i);

     }
    }
    if((poll_list[1].revents & (POLLHUP | POLLERR)) | (poll_list[0].revents & (POLLHUP | POLLERR))){
      //fprintf(stderr, "breaking out of client poll loop");
      exit(0);
    }
  }
}

int main (int argc, char **argv){
  logfd = 0; logflag = 0;
  portNu = 0;

  static struct option long_options[] = {
    {"port", required_argument,  0, 'p'},
    {"compress", no_argument,  0, 'c'},
    {"log", required_argument,  0, 'l'}
  };
  int cExample = getopt_long(argc, argv, "", long_options, 0);
  while(cExample > 0){
    switch(cExample){
      case 'p':
        portNu = atoi(optarg);
        break;
      case 'c':
        compressfl = 1;
        init_compress_stream(&compressedData);
        init_decompress_stream(&decompressedData);
        break;
      case 'l':
        logflag = 1;
        logflagname = optarg;
        break;
      default:
        fprintf(stderr, "Usage: [--port=PortNumber] [--compress] [--log=LogName]\n");
        exit(1);
    }
    cExample = getopt_long(argc, argv, "", long_options, 0);
  }

  if(logflag == 1){
    logfd = creat(logflagname, 0666);
    if(logfd < 0){
      fprintf(stderr, "The selected output file could not be created: %s\n", strerror(errno));
      exit(1);
    }
  }
  if(portNu == 0){
    fprintf(stderr, "Port is a required argument \n");
    exit(1);
  }
  terminalSetup();
  writing();

  if(compressfl){
    deflateEnd(&compressedData);
    inflateEnd(&decompressedData);
  }

  return 0;
}
