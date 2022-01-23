//Server file for project 1b

#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
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
#include <ulimit.h>
#include <zlib.h>

const int size = 256;
char * shellName;


z_stream todecompressed;
z_stream tocompressed;

int portNu;
int socketfd;

pid_t processid;

struct termios saved;


int to_shell[2];
int from_shell[2];

int compressfl;


void init_compress_stream(z_stream * stream){
  int ret = 0;
	  //stream->zalloc, stream->zfree: function pointers to malloc/free, Z_NULL to use the
    //default one. stream->opaque: data passed to customized heap allocator;
    stream->zalloc = Z_NULL, stream-> zfree = Z_NULL, stream-> opaque = Z_NULL;
    //Second arg: compression degress: 0-9, 9: most aggressive compression, slow.
	  //Z_DEFAULT_COMPRESSION: default compression degree.
	  ret = deflateInit(stream, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK){
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
    if (ret != Z_OK){
       fprintf(stderr, "compression inflateInit string couldn't initialize \n");
	  }
}


int socketSetup(){
  struct sockaddr_in serv_addr, cli_addr;
  unsigned int cli_len = sizeof(struct sockaddr_in);
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  int retfd = 0;

  memset(&serv_addr, 0, sizeof(struct sockaddr_in));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portNu); //Portnu should be setup

  if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
      fprintf(stderr, "ERROR on binding");

  listen(listenfd,5); //let the socket listen
  retfd = accept(listenfd, (struct sockaddr *) &cli_addr, &cli_len);  //wait for client's connection, cli_addr stores client address
  if(retfd < 0){
    fprintf(stderr, "Error with accepting.\n");
		exit(1);
  }
  return retfd;
}


void createPipe(int p[2]){
  if(pipe(p) < 0){
    fprintf(stderr, "error createPipe");
    exit(1);
  }
}

void noshell(){
  char buffer[size];
  int bytes = read(0, buffer, sizeof(char)*size);
  if(bytes < 0){
    fprintf(stderr, "Error reading keyboard input");
    exit(1);
  }
  while (bytes){
    int i = 0;
		for(; i < bytes; i++){
			if (buffer[i] == '\4'){ //ctrl + D
				write(1, "^D", sizeof(char));
        exit(0);
        //restored terminal at end
      }
			else if (buffer[i] == '\r' || buffer[i] == '\n'){
				write(1, "\r\n", sizeof(char)*2);
      }
			else{
				write(1, &buffer[i], sizeof(char));
        //write(1, &buffer[i], sizeof(char));
      }
		}
    memset(buffer, 0, bytes);
    bytes = read(0, buffer, sizeof(buffer));
	}
}

void sigpipehandle(){
  int temp;
  close(to_shell[1]);
  close(from_shell[0]);
  kill(processid, SIGINT);
  waitpid(processid, &temp, 0);
  fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", WIFSIGNALED(temp), WEXITSTATUS(temp));
	exit(0);
}

void decompressdata(char * buf, int cnt){
  char newbuf[1024];
  todecompressed.next_in = (unsigned char *) buf;
  todecompressed.avail_in = cnt;
  todecompressed.next_out = (unsigned char *) newbuf;
  todecompressed.avail_out = 1024;

  while (todecompressed.avail_in > 0)
      inflate(&todecompressed, Z_SYNC_FLUSH);

  int amt = 1024 - todecompressed.avail_out;
  int i = 0;
  for(; i < amt; i++){
    if (newbuf[i] == '\r' || newbuf[i] == '\n'){
      write(to_shell[1], "\n", sizeof(char));
    }
    else if(newbuf[i] == '\4'){
      close(to_shell[1]);
      //Upon receiving an EOF (^D, or 0x04) from the terminal, close the pipe to the shell, but continue processing input from the shell.
    }
    else if(newbuf[i] == '\3'){
      write(1, "^C", 2);   //TO indicate we got a ^C
      kill(processid, SIGINT);
      break;
    }
    else{
      write(to_shell[1], &newbuf[i], sizeof(char));
    }
  }

}

void toservercompression(char * buf, int cnt){
  char newbuf[size];
  tocompressed.next_in = (unsigned char *) buf;
  tocompressed.avail_in = cnt;
  tocompressed.next_out = (unsigned char *) newbuf;
  tocompressed.avail_out = size;

  while (tocompressed.avail_in > 0)
      deflate(&tocompressed, Z_SYNC_FLUSH);

  write(socketfd, newbuf, size - tocompressed.avail_out);

  // char newbuf2[2] = "\r\n";
  // char newbuf3[size];
  // tocompressed.next_in = (unsigned char *) newbuf2;
  // tocompressed.avail_in = 2;
  // tocompressed.next_out = (unsigned char *) newbuf3;
  // tocompressed.avail_out = size;
  //
  // while (tocompressed.avail_in > 0)
  //     deflate(&tocompressed, Z_SYNC_FLUSH);
  //
  // write(socketfd, newbuf3, size - tocompressed.avail_out);

}

void parentProcess(){
  struct pollfd poll_list[2];

  char buffer[size];
  int ret;

  poll_list[0].events = POLLIN | POLLERR | POLLHUP;
  poll_list[1].events = POLLIN | POLLERR | POLLHUP;
  poll_list[0].fd = socketfd;
  poll_list[1].fd = from_shell[0];

  close(to_shell[0]);
  close(from_shell[1]);

  while(1){
    ret = poll(poll_list, 2, -1);
    if(ret < 0){
      fprintf(stderr, "error using poll()");
      exit(1);
    }

    if (poll_list[0].revents & POLLIN){   //This is directly from keyboard ---> giving straight to echo and show on screen + to shell
			int count = read(socketfd, buffer, sizeof(buffer));
			// forward to stdout, to_shell [1]

      if(compressfl == 1){
        decompressdata(buffer, count);
      }
      else{
        int i = 0;
        for(; i < count; i++){
          if (buffer[i] == '\r' || buffer[i] == '\n'){
            write(to_shell[1], "\n", sizeof(char));
          }
          else if(buffer[i] == '\4'){
            close(to_shell[1]);
            //Upon receiving an EOF (^D, or 0x04) from the terminal, close the pipe to the shell, but continue processing input from the shell.
          }
          else if(buffer[i] == '\3'){
            write(1, "^C", 2);   //TO indicate we got a ^C
            kill(processid, SIGINT);
            break;
          }
          else{
            write(to_shell[1], &buffer[i], sizeof(char));
          }
        }
      }
		}
    if (poll_list[1].revents & POLLIN){   //Getting output from shell directly to stdout
      int count = read(from_shell[0], buffer, sizeof(buffer));
      // char buf2[1024];
      // int i = 0;
      int j = 0;
      char new_buf[1024]="";
      int new_buf_i = 0;
      for (; j < count; j++) {
        if(buffer[j] == '\4'){
          int temp;
          close(to_shell[1]);
          waitpid(processid, &temp, 0);
          close(socketfd);
          fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", WIFSIGNALED(temp), WEXITSTATUS(temp));
          exit(0);
          //Upon receiving an EOF (^D, or 0x04) from the terminal, close the pipe to the shell, but continue processing input from the shell.
        }
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

      if(compressfl == 1){
        toservercompression(new_buf, new_buf_i);
      }else{
        write(socketfd, buffer, count);
        // // int i = 0;
        // // for(; i < count; i++){
        // //   if (buffer[i] == '\n'){
    		// // 		write(socketfd, "\r\n", sizeof(char)*2);
        // //   }
        // //   else{
        // //     write(socketfd, &buffer[i], sizeof(char));
        // //   }
        // // }
      }
    }
    if ((POLLERR | POLLHUP) & (poll_list[1].revents)){
      int temp;
      waitpid(processid, &temp, 0);
      fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", WIFSIGNALED(temp), WEXITSTATUS(temp));
      exit(1);
    }
  }
}

void childProcess(){
  close(to_shell[1]);
  close(0);
  dup(to_shell[0]);
  close(to_shell[0]);

  close(from_shell[0]);
  dup2(from_shell[1], 1);
  dup2(from_shell[1], 2);
  close(from_shell[1]);


  char * inputFileName = shellName;
  char * args[2] = {inputFileName, NULL};
  if(execvp(inputFileName, args) < 0){
    exit(1);
  }
}

void shellD(){
  signal(SIGPIPE, sigpipehandle);
  processid = fork();
  if(processid == 0){
    childProcess();
  }else if(processid > 0){
    parentProcess();
  }else{
    fprintf(stderr, "error on creating fork");
    exit(1);
  }
}

int main (int argc, char **argv){
  shellName = "/bin/bash";
  static struct option long_options[] = {
    {"shell", required_argument,  0, 's'},
    {"port", required_argument,  0, 'p'},
    {"compress", no_argument,  0, 'c'}
  };
  int cExample = getopt_long(argc, argv, "", long_options, 0);
  while(cExample > 0){
    switch(cExample){
      case 's':
        shellName = optarg;
        break;
      case 'p':
        portNu = atoi(optarg);
        break;
      case 'c':
        compressfl = 1;
        init_compress_stream(&tocompressed);
        init_decompress_stream(&todecompressed);
        break;
      default:
        fprintf(stderr, "Usage: [--port=PortNumber] [--compress]\n");
        exit(1);
    }
    cExample = getopt_long(argc, argv, "", long_options, 0);
  }

  socketfd = socketSetup();
  createPipe(to_shell);
  createPipe(from_shell);
  shellD();

  close(socketfd);
  if(compressfl){
    deflateEnd(&tocompressed);
    inflateEnd(&todecompressed);
  }

  return 0;
}
