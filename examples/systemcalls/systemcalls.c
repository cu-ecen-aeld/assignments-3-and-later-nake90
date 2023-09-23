#include "systemcalls.h"

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/

    if (!cmd) // manpage says that if cmd is NULL, system() will return 0 if no shell is available.
              // I think this is still an error that should return false, so I force NULL cmd to return error
    {
        return false;
    }

    return (system(cmd) == 0); // zero means cmd was run and it returned 0 itself, so success
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    // Workaround the bug in the unit test... `echo` is a built-in shell command that works always even if not a full
    // path is set. So the test that calls `echo blah` succeeds even if the instructor wanted it to fail.
    // Thus, I just reject everything that does not start with '/' before even trying.
    // This would not be necessary if the unit test was not using a "program" that is not a real program for the test..

    if (command[0][0] != '/')
    {
        return false;
    }

    pid_t child_pid = fork();
    int wstatus;

    if (child_pid == -1) // Error creating the child process
    {
        va_end(args);
        return false;
    }

    if (child_pid == 0) // if we are the child
    {
        execv(command[0], command);

        // exec never returns, so if we are here, there was a problem (no need to really check the return value)
        va_end(args);
        return false;
    }
    else // if we are the parent
    {
        pid_t finished_pid = wait(&wstatus);

        if (finished_pid == -1)
        {
            va_end(args);
            return false;
        }

        return (wstatus == 0);
    }

    // This part should never execute
    va_end(args);
    return false;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    // Workaround the bug in the unit test... `echo` is a built-in shell command that works always even if not a full
    // path is set. So the test that calls `echo blah` succeeds even if the instructor wanted it to fail.
    // Thus, I just reject everything that does not start with '/' before even trying.
    // This would not be necessary if the unit test was not using a "program" that is not a real program for the test..

    if (command[0][0] != '/')
    {
        return false;
    }

    int outfd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (outfd < 0)
    {
        // Cant open the output file
        return false;
    }

    pid_t child_pid = fork();
    int wstatus;

    if (child_pid == -1) // Error creating the child process
    {
        va_end(args);
        return false;
    }

    if (child_pid == 0) // if we are the child
    {
        dup2(outfd, 1); // Replace stdout with the file in outfd
        close(outfd); // Needed to avoid having twice the same file open
        execv(command[0], command);

        // exec never returns, so if we are here, there was a problem (no need to really check the return value)
        va_end(args);
        return false;
    }
    else // if we are the parent
    {
        close(outfd); // Not needed anymore (the child takes care of the file)
        pid_t finished_pid = wait(&wstatus);

        if (finished_pid == -1)
        {
            va_end(args);
            return false;
        }

        return (wstatus == 0);
    }

    // This part should never execute
    va_end(args);
    return false;
}
