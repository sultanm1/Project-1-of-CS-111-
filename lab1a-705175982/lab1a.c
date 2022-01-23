//Proj 1a Summer 2020 Sultan Madkhali
//UID: 705175982
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/wait.h>

const int size = 256;
char * shellName;

pid_t processid;

struct termios saved;


int to_shell[2];
int from_shell[2];


void createPipe(int p[2]){
  if(pipe(p) < 0){
    fprintf(stderr, "error createPipe");
    exit(1);
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

void parentProcess(){
  struct pollfd poll_list[2];

  char buffer[size];
  int ret;

  poll_list[0].events = POLLIN | POLLERR | POLLHUP;
  poll_list[1].events = POLLIN | POLLERR | POLLHUP;
  poll_list[0].fd = 0;
  poll_list[1].fd = from_shell[0];

  close(to_shell[0]);
  close(from_shell[1]);

  while(1){
    ret = poll(poll_list, 2, 0);
    if(ret < 0){
      fprintf(stderr, "error using poll()");
      exit(1);
    }

    if (poll_list[0].revents & POLLIN) {   //This is directly from keyboard ---> giving straight to echo and show on screen + to shell
			int count = read(0, buffer, sizeof(buffer));
			// forward to stdout, to_shell [1]
      int i = 0;
      for(; i < count; i++){
        if (buffer[i] == '\r' || buffer[i] == '\n'){
  				write(1, "\r\n", sizeof(char)*2);
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
          write(1, &buffer[i], sizeof(char));
          write(to_shell[1], &buffer[i], sizeof(char));
        }
      }

		}
    if (poll_list[1].revents & POLLIN) {  //Getting output from shell directly to stdout
      int count = read(from_shell[0], buffer, sizeof(buffer));
      int i = 0;
      for(; i < count; i++){

        if (buffer[i] == '\n'){
  				write(1, "\r\n", sizeof(char)*2);
        }
        else{
          write(1, &buffer[i], sizeof(char));
        }
      }
    }
    if ((POLLERR | POLLHUP) & (poll_list[1].revents)) {
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

int main(int argc, char * argv[])
{
    int shell = 0;
    terminalSetup();

    static struct option long_options[] = {
      {"shell", required_argument,  0, 's'},
    };
    int cExample = getopt_long(argc, argv, "", long_options, 0);
    if(cExample > 0){
      switch(cExample){
        case 's':
          shellName = optarg;
          shell = 1;
          break;
        default:
          fprintf(stderr, "Usage: [--shell=PROGRAM]\n");
          exit(1);
      }
    }

    if(shell != 1){
      noshell();
    }else{
      createPipe(to_shell);
      createPipe(from_shell);
      shellD();
    }
    // \r = <cr> --> carriage return
    // \n = <lf> --> linefeed
    return 0;
}
