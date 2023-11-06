#include "Commands.h"
#include "signals.h"
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (signal(SIGTSTP, ctrlZHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-Z handler");
    }
    if (signal(SIGINT, ctrlCHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-C handler");
    }
    struct sigaction action = { 0 };
    action.sa_handler = alarmHandler;
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &action, NULL) == -1) {
        perror("smash error: failed to set alarm handler");
    }

    SmallShell& smash = SmallShell::getInstance();
    while (!smash.exit_shell) {
        smash.printPrompt();
        std::string cmd_line;
        std::getline(std::cin, cmd_line);
        smash.executeCommand(cmd_line.c_str());
        smash.waitForJob();
    }
    return 0;
}