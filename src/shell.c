#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/command.h"
#include "../include/builtin.h"

// ======================= requirement 2.3 =======================
/**
 * @brief 
 * Redirect command's stdin and stdout to the specified file descriptor
 * If you want to implement ( < , > ), use "in_file" and "out_file" included the cmd_node structure
 * If you want to implement ( | ), use "in" and "out" included the cmd_node structure.
 *
 * @param p cmd_node structure
 * 
 */
void redirection(struct cmd_node *p){
	// pipe
	if (p->in != STDIN_FILENO) 
	{
		if(dup2(p->in, STDIN_FILENO) == -1)
			perror("dup2 in");
	}
	if (p->out != STDOUT_FILENO) 
	{
		if(dup2(p->out, STDOUT_FILENO) == -1)
			perror("dup2 out");
	}
	
	// <
	if(p->in_file){
		int fd_in = open(p->in_file, O_RDONLY);
		if(fd_in < 0){
			perror("open in_file");
		}
		if(dup2(fd_in, STDIN_FILENO) == -1)
			perror("dup2 in_file");
		close(fd_in);
	}
	// >
	if(p->out_file){
		int fd_out = open(p->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if(fd_out < 0){
			perror("open out_file");
		}
		if(dup2(fd_out, STDOUT_FILENO) == -1)
			perror("dup2 out_file");
		close(fd_out);
	}
}
// ===============================================================

// ======================= requirement 2.2 =======================
/**
 * @brief 
 * Execute external command
 * The external command is mainly divided into the following two steps:
 * 1. Call "fork()" to create child process
 * 2. Call "execvp()" to execute the corresponding executable file
 * @param p cmd_node structure
 * @return int 
 * Return execution status
 */
int spawn_proc(struct cmd_node *p)
{
	pid_t pid = fork();
	if (pid < 0){
		perror("fork");
		return -1;
	}
	else if(pid == 0){
		redirection(p);
		if (execvp(p->args[0], p->args) == -1) {
			perror("execvp");
			_exit(1);  // Exit child process on failure
		}
	}
	else{
		waitpid(pid, NULL, 0);
	}

  	return 1;
}

// ===============================================================


// ======================= requirement 2.4 =======================
/**
 * @brief 
 * Use "pipe()" to create a communication bridge between processes
 * Call "spawn_proc()" in order according to the number of cmd_node
 * @param cmd Command structure  
 * @return int
 * Return execution status 
 */
int fork_cmd_node(struct cmd *cmd)
{
    struct cmd_node *node = cmd->head;
    int prev_read = STDIN_FILENO;   // previous read end
    int pipefd[2];
    int status = 0;
    int child_cnt = 0;              // actual number of forked children

    for (; node != NULL; node = node->next) {

        // 1. decide this node's in / out
        if (node->next != NULL) {
            // not the last one → connect to the next command, so create a new pipe
            if (pipe(pipefd) == -1) {
                perror("pipe");
                return -1;
            }
            node->in  = prev_read;   // read from previous (first is STDIN)
            node->out = pipefd[1];   // write to this pipe's write end
        } else {
            // last one → read from previous, output to stdout
            node->in  = prev_read;
            node->out = STDOUT_FILENO;
        }

        // 2. fork child
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            // ===== child =====
            redirection(node);  

            int idx = searchBuiltInCommand(node);
            if (idx != -1) {
                execBuiltInCommand(idx, node);
                _exit(0);
            } else {
                if(execvp(node->args[0], node->args) == -1) {
                	perror("execvp");
					_exit(1);
                }
				_exit(0);
            }
        }

        // ===== parent =====
        child_cnt++;

        // close write end of current command
        if (node->out != STDOUT_FILENO) {
            close(node->out);
        }

        // close read end of previous command
        if (prev_read != STDIN_FILENO) {
            close(prev_read);
        }

        // update prev_read: leave it for the next command
        if (node->next != NULL) {
            prev_read = pipefd[0];  // next node is reading from here
        }
    }

    // 3. wait all children
    for (int i = 0; i < child_cnt; ++i) {
        if (wait(&status) == -1) {
            perror("wait");
            return -1;
        }
    }

    if (WIFEXITED(status))
        return 1;
    return -1;
}


// ===============================================================


void shell()
{
	while (1) {
		printf(">>> $ ");
		char *buffer = read_line();
		if (buffer == NULL)
			continue;

		struct cmd *cmd = split_line(buffer);
		
		int status = -1;
		// only a single command
		struct cmd_node *temp = cmd->head;
		
		if(temp->next == NULL){
			status = searchBuiltInCommand(temp);
			if (status != -1){
				int in = dup(STDIN_FILENO), out = dup(STDOUT_FILENO);
				if( in == -1 || out == -1)
					perror("dup");
				redirection(temp);
				status = execBuiltInCommand(status,temp);

				// recover shell stdin and stdout
				if (temp->in_file)  dup2(in, 0);
				if (temp->out_file) dup2(out, 1);
				close(in);
				close(out);
			}
			else{
				//external command
				status = spawn_proc(cmd->head);
			}
		}
		// There are multiple commands ( | )
		else{
			status = fork_cmd_node(cmd);
		}
		// free space
		while (cmd->head) {
			
			struct cmd_node *temp = cmd->head;
      		cmd->head = cmd->head->next;
			free(temp->args);
   	    	free(temp);
   		}
		free(cmd);
		free(buffer);
		
		if (status == 0)
			break;
	}
}
