#include "signals.h"
#include "Commands.h"
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

#define CTRL_Z_STR ("smash: got ctrl-Z\n")
#define CTRL_C_STR ("smash: got ctrl-C\n")
#define ALARM_STR ("smash: got an alarm\n")
#define SECOND (1.0)

void ctrlZHandler(int sig_num)
{
    write(STDOUT_FILENO, CTRL_Z_STR, sizeof(CTRL_Z_STR) - 1);
    SmallShell& smash = SmallShell::getInstance();
    if (smash.wait_job_pid != -1) {
        kill(smash.wait_job_pid, SIGSTOP);
    }
}

void ctrlCHandler(int sig_num)
{
    write(STDOUT_FILENO, CTRL_C_STR, sizeof(CTRL_C_STR) - 1);
    SmallShell& smash = SmallShell::getInstance();
    if (smash.wait_job_pid != -1) {
        kill(smash.wait_job_pid, SIGKILL);
        smash.foreground_job_killed = true;
    }
}

void alarmHandler(int sig_num)
{
    write(STDOUT_FILENO, ALARM_STR, sizeof(ALARM_STR) - 1);
    SmallShell& smash = SmallShell::getInstance();
    while (smash.timeout_list.size()) {
        time_t time1 = smash.timeout_list.begin()->first;
        int diff = difftime(time1, time(NULL));
        if (diff >= SECOND) {
            alarm((unsigned int)diff);
            break;
        }
        pid_t pid = smash.timeout_list.begin()->second.pid;
        waitpid(pid, NULL, WNOHANG);
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
            string cmd_line = smash.timeout_list.begin()->second.cmd_line;
            write(STDOUT_FILENO, "smash: ", sizeof("smash: ") - 1);
            write(STDOUT_FILENO, cmd_line.c_str(), cmd_line.length());
            write(STDOUT_FILENO, " timed out!\n", sizeof(" timed out!\n") - 1);
        }
        job_id_t job_id = smash.timeout_list.begin()->second.job_id;
        if (smash.jobs_list.jobExist(job_id)) {
            smash.jobs_list.removeJobById(job_id);
        }
        smash.timeout_list.erase(smash.timeout_list.begin());
    }
}
