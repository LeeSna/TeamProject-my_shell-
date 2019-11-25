#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <setjmp.h>

int desc[2];
static sigjmp_buf go_prompt;
static volatile sig_atomic_t jumpable = 0;

static void jhandling(int signalnum) {
        if(!jumpable) return;
        jumpable = 0;
        siglongjmp(go_prompt, 1);
}

int makeargv(const char *s, const char *delimiters, char ***argvp) {
        int error;
        int i;
        int num_argv;
        const char *snew;
        char *t;
        
        if((s==NULL) || (delimiters==NULL) || (argvp==NULL)) {   //s, delimiters, argvp 중 하나라도 NULL 이라면 
                errno = EINVAL;
                return -1;
        }
        *argvp = NULL;
        snew = s+strspn(s, delimiters);      //필요한 메모리 공간을 계산하기 위해 필요한 문자열 길이를 계산 
        if((t = (char *)malloc(strlen(snew) + 1)) == NULL)   //동적 메모리 공간 할당, 할당 실패시 
                return -1;
        strcpy(t, snew);   //문자열 복사 
        num_argv = 0;

        if(strtok(t, delimiters) != NULL)   //문자열을 delimiters 기준으로 자름 
                for(num_argv = 1; strtok(NULL, delimiters) != NULL; num_argv++);
        if((*argvp = malloc((num_argv +1)*sizeof(char *))) == NULL) {   //메모리 공간 동적 할당, 할당 실패시 
                error = errno;
                free(t);
                errno = error;
                return -1;
        }
        if(num_argv == 0)   //문자열 t의 공간 할당이 성공적이었으면 num_argv = 0이므로 
                free(t);   //메모리 반환 
        else {
                strcpy(t, snew);
                **argvp = strtok(t, delimiters);   //문자열을 delimiters 기준으로 자름
                for(i=1; i<num_argv; i++)
                        *((*argvp) + i) = strtok(NULL, delimiters);
        }
        *((*argvp) + num_argv) = NULL;

        return num_argv;
}

int make_redirect_in(char *cmd) {   //표준입력 형태로 전달 
        int error;
        int infd;
        char *infile;
        if((infile = strchr(cmd, '<')) == NULL)      //'<' 문자가 포함되어 있지 않다면 
                return 0;
        *infile = 0;   //포함 되어 있다면 0으로 초기화 
        infile = strtok(infile+1, " \t");   //탭까지 문자열을 자름 
        
      if(infile == NULL)   //자른 문자열이 NULL일 경우 
                return 0;
                
        if((infd = open(infile, O_RDONLY)) == -1)   //읽기 전용으로 infile을 열기 및 열기 실패시 -1을 리턴 
                return -1;
                
        if(dup2(infd, STDIN_FILENO) == -1) {   //파일 디스크립터 복사 및 복사 실패시 
                error = errno;
                close(infd);   //파일을 닫고 
                errno = error;
                return -1;   //-1을 리턴 
        }
        return close(infd);
}

int make_redirect_out(char *cmd) {   //저장 
        int error;
        int outfd;
        char *outfile;
        if((outfile = strchr(cmd, '>')) == NULL)   //'>' 문자가 포함되어 있지 않다면 
                return 0;
        *outfile = 0;   //포함 되어 있다면 0으로 초기화 
        outfile = strtok(outfile+1, " \t");      //탭까지 문자열을 자름 

        if(outfile == NULL)      //자른 문자열이 NULL일 경우 
                return 0;

        if((outfd = open(outfile, O_WRONLY)) == -1)   //쓰기전용으로 outfile을 열기 및 열기 실패시 -1 리턴 
                return -1;

        if(dup2(outfd, STDOUT_FILENO) == -1) {   //파일 디스크립터 복사 및 복사 실패시 
                error = errno;
                close(outfd);   //파일을 닫고 
                errno = error;
                return -1;   //-1을 리턴 
        }
        return close(outfd);
}

void executecmd(char *cmds) {
        int child;
        int count;
        int fds[2];
        int i;
        char **pipelist;
        
        count = makeargv(cmds, "|", &pipelist);
        if(count <=0 ) {
                fprintf(stderr, "Failed\n");
                exit(1);
        }
        for(i=0; i<count-1; i++) {
                if(pipe(fds) == -1){   //파이프 생성 및 생성 실패시 
                        perror("Failed to create pipes");
                        exit(1);
                }
                else if((child = fork()) == -1){   //자식 프로세스 생성 및 생성 실패시 
                        perror("Failed to create process to run command");
                        exit(1);
                }
                else if(child) {   //자식 프로세스 생성 성공시 
                        if(dup2(fds[1], STDOUT_FILENO) == -1){   //디스크립터 복사 및 복사 실패시 
                                perror("Failed to connect pipeline");
                                exit(1);
                        }
                        if(close(fds[0]) || close(fds[1])){   //파일 닫기 및 닫기 실패시 
                                perror("Failed to close needed files");
                        exit(1);
                  }
                        executeredirect(pipelist[i], i==0, 0);   //재지향 실행 
                        exit(1);   //프로세스 종료 
                }
                if(dup2(fds[0], STDIN_FILENO) == -1){   //디스크립터 복사, 복사 실패시 
                        perror("Failed to connect last component");
                        exit(1);
                }
                if(close(fds[0]) || close(fds[1])){      //파일 닫기, 닫기 실패시 
                        perror("Failed to do final close");
                        exit(1);
                }
        }
        executeredirect(pipelist[i], i==0, 1); // 파이프가 있다면 excuteredirect 함수를 이용해 리다이렉션을 실행
        exit(1);
}

void executeredirect(char *s, int in, int out) {
        char **chargv;
        char *pin;
        char *pout;
        int i, j;
        if(in && ((pin = strchr(s, '<')) != NULL) && out && ((pout=strchr(s,'>')) !=NULL) && (pin>pout)) { // >,< 가 Null이 아니고 in =1, out =1 , pin>pout 이라면
                if(make_redirect_in(s) == -1) {      //재지향-읽기 실행 및 과정 중 오류가 있다면 
                        perror("Failed to redirect input");
                        return;
                }
        }
        if(out && make_redirect_out(s) == -1)   //재지향-저장 실행 및 과정 중 오류나 out이 1이라면 
                perror("Failed to redirect output");
        else if(in && make_redirect_in(s) == -1)   //재지향-읽기 실행 및 과정 중 오류나 in이 1이라면 
                perror("failed to redirect input");
        else if(makeargv(s, " \t", &chargv) <= 0)   //커맨드라인 해석 실패시 
                fprintf(stderr, "failed to parse command line\n");

        else {
                for(i=0; chargv[i] != 0; i++) {      //재지향 실행부 
                        for(j=0; chargv[i][j] != 0; j++)  {
                                write(desc[1], &chargv[i][j], sizeof(char));
                        }
                        write(desc[1], " ", sizeof(char));
                }
                execvp(chargv[0], chargv);
                perror("failed to execute command");
                write(desc[1], "/5999", sizeof("/5999"));
        }
        exit(1);
}

int signalsetup(struct sigaction *def, sigset_t *mask, void (*handler)(int)) { //시그널 설정 함수 
        struct sigaction catch;
        catch.sa_handler = handler;
        def->sa_handler = SIG_DFL;
        catch.sa_flags = 0;
        def->sa_flags = 0;
        
      //시그널 집합내용 모두 삭제, 시그널 추가, 시그널 처리 및 이 중 하나라도 실패시 -1 리턴 
        if((sigemptyset(&(def->sa_mask)) == -1) || (sigemptyset(&(catch.sa_mask)) == -1) || (sigaddset(&(catch.sa_mask), SIGINT) == -1) || (sigaddset(&(catch.sa_mask), SIGQUIT) == -1) || (sigaction(SIGINT, &catch, NULL) == -1) || (sigaction(SIGQUIT, &catch, NULL) == -1) || (sigemptyset(mask) == -1) || (sigaddset(mask, SIGINT) == -1) || (sigaddset(mask, SIGQUIT) == -1))
                return -1;
        return 0;   //모두 성공시 
}

int main(void) {
        pid_t childpid;
        char inbuf[MAX_CANON];
        int len;
        sigset_t blockmask;
        struct sigaction defhandler;
        char *backp;
        int inbackground;
        char **str;
        int tcnt=0;
        char pipebuf[101];
        int j, k;
        int str_len;
        pipe(desc);
        if(signalsetup(&defhandler, &blockmask, jhandling) == -1) {   //시그널 설정 및 실패시 오류출력 
                perror("Failed to set up shell signal handling");
                return 1;
        }
        if(sigprocmask(SIG_BLOCK, &blockmask, NULL) == -1 ) {   //시그널 마스크 변경 및 실패시 오류출력 
                perror("Failed to block signals");
                return 1;
        }
        while(1) {
                if((sigsetjmp(go_prompt, 1)) && (fputs("\n", stdout) ==EOF)) //출력이 eof이거나, 시그널이 점프라면 
                        continue;

               jumpable = 1;
                
                if(fputs("SHELL >> ", stdout) == EOF) // 아무런 출력이 없으면. 
                        continue;

                if(fgets(inbuf, 256, stdin) == NULL) // 아무런 입력이 없다면. 
                        continue;

                len = strlen(inbuf);  // 명령어의 길이 체크
                if(inbuf[len-1] == '\n')
                        inbuf[len-1] = 0;

                if(strcmp(inbuf, "exit") == 0)   //exit를 입력했다면 
                        break;

                if((backp = strchr(inbuf, '&')) == NULL)   //&포함시 백그라운드 
                        inbackground = 0;

                else {      //&비포함시 
                        inbackground = 1;
                        *backp = 0;
                }

                if(sigprocmask(SIG_BLOCK, &blockmask, NULL) == -1) // 시그널 대기가 실패했다면.
                        perror("Failed to block signals");

                makeargv(inbuf, " \t", &str);
           if(str[0]=='\0')   //아무 입력없고 Enter 입력 시 
                  continue;   
                
                if(strcmp(str[0],"cd") == 0) { 
                        chdir(str[1]);
                        system("pwd");   //시스템 함수 호출 - 경로표시 
                        continue;
                }
                

               for(j=0; j<100; j++)   //파이프버퍼 초기화 
                      pipebuf[j] = '\0';
      
                      write(desc[1], " ", sizeof(char));
                      str_len = read(desc[0], pipebuf, 100);
                      pipebuf[str_len]=0;
                                          
                      if((childpid = fork()) == -1) { // 자식프로세스 생성에 실패했다면. 
                              perror("Failed to fork child to execute command");
      
                      }
                        else if(childpid == 0) { // 자식프로세스가 생성되었다면. 
                              if(inbackground && (setpgid(0,0) == -1)) // 백그라운드이고, 자식 프로세스의 그룹 id 를 0으로 변경하지 못했다면.
                                      return 1; // 프로그램 종료 
                              if((sigaction(SIGINT, &defhandler, NULL) == -1) || (sigaction(SIGQUIT, &defhandler, NULL) == -1) || (sigprocmask(SIG_UNBLOCK, &blockmask, NULL) == -1)) {
                                 // ctrl-C , ctrl-\, 이 발생 시그널이 발생되도 지연가능 
                                      perror("Failed to set signal handling for command ");
                                      // 에러 
                              return 1;
                              }
                          executecmd(inbuf);
                          return 1;
                      }
                      if(sigprocmask(SIG_UNBLOCK, &blockmask, NULL) == -1)
                              perror("Failed to unblock signals");
                      if(!inbackground) // 백그라운드가 아니라면
                              waitpid(childpid, NULL, 0); // 자식프로세스의 종료를 기다린다.
                      while(waitpid(-1, NULL, WNOHANG) > 0); //어떤 자식프로세스라도 종료된다면 wait 통과.
        }
        return 0;
}