#include "Commands.h"
#include <algorithm>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sched.h>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY() \
    cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT() \
    cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

string _ltrim(const std::string& s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string& s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string& s)
{
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char* cmd_line, char** args)
{
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char*)malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;
    FUNC_EXIT()
}

void _freeArgs(char** args)
{
    for (size_t i = 0; i < COMMAND_MAX_ARGS && args[i]; i++) {
        free(args[i]);
    }
}

bool _isBackgroundComamnd(const char* cmd_line)
{
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

bool _isRedirectionCommand(const char* cmd_line)
{
    const string str(cmd_line);
    return str.find(">") != string::npos || str.find(">>") != string::npos;
}

bool _isPipeCommand(const char* cmd_line)
{
    const string str(cmd_line);
    return str.find("|") != string::npos || str.find("|&") != string::npos;
}

void _removeBackgroundSign(char* cmd_line)
{
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

void _removeBackgroundSign(string& cmd_line)
{
    cmd_line.erase(std::remove(cmd_line.begin(), cmd_line.end(), '&'), cmd_line.end());
}

char* _cmd_line_copy(const char* cmd_line)
{
    size_t len = strlen(cmd_line) + 1;
    char* copied_cmd_line = (char*)malloc(len);
    if (copied_cmd_line) {
        memcpy(copied_cmd_line, cmd_line, len);
    } else {
        perror("smash error: malloc failed");
    }
    return copied_cmd_line;
}

bool _is_number(const char* arg)
{
    int i = 0;
    if (arg) {
        if (arg[0] == '-') {
            i++;
        }
        for (; arg[i]; ++i) {
            if (arg[i] < '0' || '9' < arg[i])
                return false;
        }
    }
    return true;
}
int _WriteWrapper(int fd, char* buffer, size_t size)
{
    do {
        ssize_t bytes_written = write(fd, buffer, size);
        if (bytes_written == -1) {
            perror("smash error: close failed");
            return -1;
        }
        buffer += bytes_written;
        size -= bytes_written;
    } while (size);
    return 0;
}

void Command::CloseFd(int fd)
{
    if (fd == -1) {
        return;
    }
    if (close(fd) == -1) {
        perror("smash error: close failed");
    }
}

BuiltInCommand::BuiltInCommand(const char* cmd_line)
    : Command()
{
    m_args_num = _parseCommandLine(cmd_line, m_args);
}

BuiltInCommand::~BuiltInCommand()
{
    _freeArgs(m_args);
}

/**
 * BuiltInCommands implementations
 */

void ChangePromptCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();
    if (m_args_num >= 2)
        smash.changePromptName(m_args[1]);
    else
        // default change to "smash"
        smash.changePromptName();
}

void ShowPidCommand::execute()
{
    cout << "smash pid is " << getpid() << endl;
}

void GetCurrDirCommand::execute()
{
    char* cwd = get_current_dir_name();
    if (cwd) {
        cout << cwd << endl;
        free(cwd);
    } else {
        perror("smash error: getcwd failed");
    }
}

void ChangeDirCommand::execute()
{
    if (m_args_num != 2) {
        cerr << "smash error: cd: too many arguments" << endl;
        return;
    }
    SmallShell& smash = SmallShell::getInstance();
    string new_path;
    if (strcmp(m_args[1], "-") == 0) {
        if (!smash.old_pwd) {
            cerr << "smash error: cd: OLDPWD not set" << endl;
            return;
        }
        new_path = smash.old_path;
    } else {
        new_path = m_args[1];
    }
    char* cwd = get_current_dir_name();
    if (cwd == nullptr) {
        perror("smash error: getcwd failed");
    }
    if (chdir(new_path.c_str()) == -1) {
        perror("smash error: chdir failed");
        free(cwd);
        return;
    }
    smash.old_path = cwd;
    smash.old_pwd = true;
    free(cwd);
}

void JobsCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();
    smash.jobs_list.removeFinishedJobs();
    smash.jobs_list.printJobsList();
}

void ForegroundCommand::execute()
{
    if (m_args_num > 2 || (m_args_num == 2 && !_is_number(m_args[1]))) {
        cerr << "smash error: fg: invalid arguments" << endl;
        return;
    }

    SmallShell& smash = SmallShell::getInstance();
    smash.jobs_list.removeFinishedJobs();
    if (m_args_num == 1 && !smash.jobs_list.jobsNumber()) {
        cerr << "smash error: fg: jobs list is empty" << endl;
        return;
    }
    if (m_args_num == 2 && !smash.jobs_list.jobExist(atoi(m_args[1]))) {
        cerr << "smash error: fg: job-id " << m_args[1] << " does not exist" << endl;
        return;
    }
    int job_id = (m_args_num == 2) ? atoi(m_args[1]) : -1;
    smash.foreground_job = smash.jobs_list.getJobById(job_id);
    smash.jobs_list.removeJobById(job_id);
    smash.wait_job_pid = smash.foreground_job.pid;
    kill(smash.foreground_job.pid, SIGCONT);
    cout << smash.foreground_job.cmd_line << " : " << smash.foreground_job.pid << endl;
}

void BackgroundCommand::execute()
{
    if (m_args_num > 2 || (m_args_num == 2 && !_is_number(m_args[1]))) {
        cerr << "smash error: bg: invalid arguments" << endl;
        return;
    }
    SmallShell& smash = SmallShell::getInstance();
    smash.jobs_list.removeFinishedJobs();
    if (m_args_num == 1 && !smash.jobs_list.stoppedJobs()) {
        cerr << "smash error: bg: there is no stopped jobs to resume" << endl;
        return;
    }
    if (m_args_num == 2 && !smash.jobs_list.jobExist(atoi(m_args[1]))) {
        cerr << "smash error: bg: job-id " << m_args[1] << " does not exist" << endl;
        return;
    }
    if (m_args_num == 2) {
        JobEntry& job = smash.jobs_list.getJobById(atoi(m_args[1]));
        if (!job.stopped) {
            cerr << "smash error: bg: job-id " << job.job_id << " is already running in the background" << endl;
            return;
        }
        kill(job.pid, SIGCONT);
        job.stopped = false;
        cout << job.cmd_line << " : " << job.pid << endl;
    } else {
        JobEntry& job = smash.jobs_list.getLastStoppedJob();
        kill(job.pid, SIGCONT);
        job.stopped = false;
        cout << job.cmd_line << " : " << job.pid << endl;
    }
}

void QuitCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();
    smash.jobs_list.removeFinishedJobs();
    if (m_args_num == 2 && strcmp(m_args[1], "kill") == 0) {
        cout << "smash: sending SIGKILL signal to " << smash.jobs_list.jobsNumber() << " jobs:" << endl;
        smash.jobs_list.killAllJobs();
    }
    smash.exit_shell = true;
}

void KillCommand::execute()
{
    if (m_args_num != 3 || m_args[1][0] != '-' || !_is_number(&m_args[1][1]) || !_is_number(m_args[2])) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }
    int signal_type = atoi(&m_args[1][1]);
    if (signal_type < 1 || 31 < signal_type) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }
    SmallShell& smash = SmallShell::getInstance();
    smash.jobs_list.removeFinishedJobs();
    if (!smash.jobs_list.jobExist(atoi(m_args[2]))) {
        cerr << "smash error: kill: job-id " << m_args[2] << " does not exist" << endl;
        return;
    }
    JobEntry& job = smash.jobs_list.getJobById(atoi(m_args[2]));
    kill(job.pid, signal_type);
    cout << "signal number " << signal_type << " was sent to pid " << job.pid << endl;
    switch (signal_type) {
    case SIGSTOP:
        job.stopped = true;
        break;
    case SIGCONT:
        job.stopped = false;
        break;
    case SIGKILL:
        waitpid(job.pid, NULL, WNOHANG);
        smash.jobs_list.removeJobById(job.job_id);
        break;
    }
}

void FareCommand::execute()
{
    ssize_t bytes_read;
    int src_file, dest_file;

    if (m_args_num != 4) {
        cerr << "smash error: fare: invalid arguments" << endl;
        return;
    }
    string tmp_file = string(m_args[1]) + string(TEMP_FILE);
    if ((dest_file = open(tmp_file.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0655)) == -1) {
        perror("smash error: open failed");
        return;
    }
    if ((src_file = open(m_args[1], O_RDONLY)) == -1) {
        close(dest_file);
        perror("smash error: open failed");
        return;
    }
    size_t buf_size = strlen(m_args[2]);
    char* buffer = (char*)malloc(buf_size);
    if (buffer == nullptr) {
        close(src_file);
        close(dest_file);
        perror("smash error: malloc failed");
        return;
    }

    size_t instances = 0;
    size_t str_replace_len = strlen(m_args[3]);
    do {
        bytes_read = read(src_file, buffer, buf_size);
        if ((size_t)bytes_read == buf_size && memcmp(buffer, m_args[2], buf_size) == 0) {
            if (_WriteWrapper(dest_file, m_args[3], str_replace_len) == -1) {
                close(src_file);
                close(dest_file);
                return;
            }
            instances++;
        } else if (bytes_read > 0) {
            if (_WriteWrapper(dest_file, &buffer[0], 1) == -1) {
                close(src_file);
                close(dest_file);
                return;
            }
            lseek(src_file, 1 - bytes_read, SEEK_CUR);
        } else if (bytes_read == -1) {
            perror("smash error: read failed");
            close(src_file);
            close(dest_file);
            return;
        }
    } while (bytes_read != 0);

    if (close(src_file) == -1) {
        perror("smash error: close failed");
        return;
    }
    if (close(dest_file) == -1) {
        perror("smash error: close failed");
        return;
    }
    free(buffer);
    remove(m_args[1]);
    rename(tmp_file.c_str(), m_args[1]);
    cout << "replaced " << instances << " instances of the string \"" << m_args[2] << "\"" << endl;
}

void SetcoreCommand::execute()
{
    if (m_args_num != 3 || (m_args_num == 3 && (!_is_number(m_args[1]) || !_is_number(m_args[2])))) {
        cerr << "smash error: setcore: invalid arguments" << endl;
        return;
    }
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(atoi(m_args[2]), &mask);
    SmallShell& smash = SmallShell::getInstance();
    smash.jobs_list.removeFinishedJobs();
    if (!smash.jobs_list.jobExist(atoi(m_args[1]))) {
        cerr << "smash error: setcore: job-id " << m_args[1] << " does not exist" << endl;
        return;
    }
    JobEntry& job = smash.jobs_list.getJobById(atoi(m_args[1]));

    if (sched_setaffinity(job.pid, sizeof(cpu_set_t), &mask) != 0) {
        if (errno == EINVAL)
            cerr << "smash error: setcore: invalid core number" << endl;
        else if (errno == ESRCH)
            cerr << "smash error: setcore: job-id " << m_args[1] << " does not exist" << endl;
        else
            perror("smash error: sched_setaffinity failed");
    }
}

void TimeoutCommand::execute()
{
    static time_t min_completion_time = 0;
    if (m_args_num < 3 || !_is_number(m_args[1]) || atoi(m_args[1]) < 0) {
        cerr << "smash error: timeout: invalid arguments" << endl;
        return;
    }
    time_t job_time = atoi(m_args[1]);
    SmallShell& smash = SmallShell::getInstance();
    string outer_cmd_line = _trim(m_cmd_line);
    string inner_cmd_line = outer_cmd_line.substr(outer_cmd_line.find(' ', sizeof("timeout ")) + 1, string::npos);
    Command* command = smash.CreateCommand(inner_cmd_line.c_str());
    if (command) {
        command->execute();
    }
    if (dynamic_cast<ExternalCommand*>(command) != nullptr) {
        // not a built-in so we can add it and send kill signal to it when time comes
        JobEntry& job = dynamic_cast<ExternalCommand*>(command)->child_job;
        job.cmd_line = m_cmd_line;
        time_t completion_time = time(NULL) + job_time;
        if (min_completion_time > completion_time || smash.timeout_list.size() == 0) {
            alarm(job_time);
            min_completion_time = completion_time;
        }
        smash.timeout_list.insert({ completion_time, job });
        if (smash.jobs_list.jobExist(job.job_id)) {
            smash.jobs_list.getJobById(job.job_id).cmd_line = m_cmd_line;
        }
    }
    delete command;
}

/**
 * ExternalCommand implementation
 */

void ExternalCommand::execute()
{
    execute(-1);
}

void ExternalCommand::execute(int close_fd)
{
    // TODO: add support for special chars
    SmallShell& smash = SmallShell::getInstance();
    char* cmd_line = _cmd_line_copy(m_cmd_line.c_str());
    _removeBackgroundSign(cmd_line);
    char* args[COMMAND_MAX_ARGS];
    if (m_cmd_line.find('*') == string::npos && m_cmd_line.find('?') == string::npos) {
        _parseCommandLine(cmd_line, args);
        free(cmd_line);
    } else {
        args[0] = _cmd_line_copy("/bin/bash");
        args[1] = _cmd_line_copy("-c");
        args[2] = cmd_line;
        args[3] = NULL;
    }
    size_t pid = fork();
    if (pid == 0) {
        // child
        setpgrp();
        CloseFd(close_fd);
        execvp(args[0], args);
        perror("smash error: execve failed");
        exit(errno);
    } else if (pid > 0) {
        // parent
        JobEntry job = JobEntry(m_cmd_line, pid);
        if (_isBackgroundComamnd(m_cmd_line.c_str())) {
            smash.jobs_list.addJob(job);
            child_job = smash.jobs_list.getJobById();
        } else {
            child_job = job;
            smash.foreground_job = job;
            smash.wait_job_pid = pid;
        }
        _freeArgs(args);
    } else {
        perror("smash error: fork failed");
    }
}

/**
 * Creates and returns a pointer to Command class which matches the given command line (cmd_line)
 */
Command* SmallShell::CreateCommand(const char* cmd_line)
{
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

    if (_isRedirectionCommand(cmd_line)) {
        return new RedirectionCommand(cmd_line);
    } else if (_isPipeCommand(cmd_line)) {
        return new PipeCommand(cmd_line);
    } else if (firstWord.compare("chprompt") == 0) {
        return new ChangePromptCommand(cmd_line);
    } else if (firstWord.compare("showpid") == 0) {
        return new ShowPidCommand(cmd_line);
    } else if (firstWord.compare("pwd") == 0) {
        return new GetCurrDirCommand(cmd_line);
    } else if (firstWord.compare("cd") == 0) {
        return new ChangeDirCommand(cmd_line);
    } else if (firstWord.compare("jobs") == 0) {
        return new JobsCommand(cmd_line);
    } else if (firstWord.compare("fg") == 0) {
        return new ForegroundCommand(cmd_line);
    } else if (firstWord.compare("bg") == 0) {
        return new BackgroundCommand(cmd_line);
    } else if (firstWord.compare("quit") == 0) {
        return new QuitCommand(cmd_line);
    } else if (firstWord.compare("kill") == 0) {
        return new KillCommand(cmd_line);
    } else if (firstWord.compare("fare") == 0) {
        return new FareCommand(cmd_line);
    } else if (firstWord.compare("setcore") == 0) {
        return new SetcoreCommand(cmd_line);
    } else if (firstWord.compare("timeout") == 0) {
        return new TimeoutCommand(cmd_line);
    } else if (firstWord.length()) {
        return new ExternalCommand(cmd_line);
    }
    return nullptr;
}

/**
 *  RedirectionCommand Implementation
 */
RedirectionCommand::RedirectionCommand(const char* cmd_line)
    : Command()
    , m_cmd_line(cmd_line)
{
    std::string redirection_symbol;
    if (m_cmd_line.find(">>") != string::npos) {
        redirection_symbol = ">>";
        m_append = true;
    } else {
        redirection_symbol = ">";
        m_append = false;
    }
    m_file_name = m_cmd_line.substr(m_cmd_line.find(redirection_symbol) + redirection_symbol.length());
    m_cmd_line = m_cmd_line.substr(0, m_cmd_line.find(redirection_symbol));
    m_file_name = _trim(m_file_name);
}

void RedirectionCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();
    int flags = O_WRONLY | O_CREAT;
    flags |= (m_append) ? O_APPEND : O_TRUNC;

    int file_fd = open(m_file_name.c_str(), flags, 0655);
    if (file_fd == -1) {
        perror("smash error: open failed");
        return;
    }
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout == -1) {
        perror("smash error: dup failed");
        close(file_fd);
        return;
    }
    if (dup2(file_fd, STDOUT_FILENO) == -1) {
        perror("smash error: dup2 failed");
        close(saved_stdout);
        close(file_fd);
        return;
    }
    smash.executeCommand(m_cmd_line.c_str());

    if (close(file_fd) == -1)
        perror("smash error: close failed");
    if (dup2(saved_stdout, STDOUT_FILENO) == -1)
        perror("smash error: dup2 failed");
    if (close(saved_stdout) == -1)
        perror("smash error: close failed");
}

/**
 * PipeCommand Implementation
 */
PipeCommand::PipeCommand(const char* cmd_line)
    : Command()
    , m_cmd_line1(cmd_line)
{
    std::string redirection_symbol;
    if (m_cmd_line1.find("|&") != string::npos) {
        redirection_symbol = "|&";
        m_std_type = STDERR_FILENO;
    } else {
        redirection_symbol = "|";
        m_std_type = STDOUT_FILENO;
    }
    m_cmd_line2 = m_cmd_line1.substr(m_cmd_line1.find(redirection_symbol) + redirection_symbol.length());
    m_cmd_line1 = m_cmd_line1.substr(0, m_cmd_line1.find(redirection_symbol));
    _removeBackgroundSign(m_cmd_line1);
    _removeBackgroundSign(m_cmd_line2);
}

void PipeCommand::execute()
{
    int pipe_fd[2];
    int saved_stdin, saved_stdout_stderr;
    enum {
        READ,
        WRITE
    };
    if ((saved_stdin = dup(STDIN_FILENO)) == -1) {
        perror("smash error: dup failed");
        return;
    }
    if ((saved_stdout_stderr = dup(m_std_type)) == -1) {
        perror("smash error: dup failed");
        close(saved_stdin);
        return;
    }
    if (pipe(pipe_fd) == -1) {
        perror("smash error: pipe failed");
        close(saved_stdin);
        close(saved_stdout_stderr);
        return;
    }
    SmallShell& smash = SmallShell::getInstance();
    Command* command1 = smash.CreateCommand(m_cmd_line1.c_str());
    Command* command2 = smash.CreateCommand(m_cmd_line2.c_str());

    if (dup2(pipe_fd[WRITE], m_std_type) == -1) {
        perror("smash error: dup2 failed");
    } else if (close(pipe_fd[WRITE]) != -1) {
        if (command1 != nullptr) {
            if (dynamic_cast<ExternalCommand*>(command1) != nullptr)
                dynamic_cast<ExternalCommand*>(command1)->execute(pipe_fd[READ]);
            else
                command1->execute();
        }
    } else {
        perror("smash error: close failed");
    }

    if (dup2(saved_stdout_stderr, m_std_type) == -1)
        perror("smash error: dup2 failed");
    if (close(saved_stdout_stderr) == -1)
        perror("smash error: close failed");

    if (dup2(pipe_fd[READ], STDIN_FILENO) == -1) {
        perror("smash error: dup2 failed");
    } else if (close(pipe_fd[READ]) != -1) {
        if (command2 != nullptr)
            command2->execute();
    } else {
        perror("smash error: close failed");
    }

    if (dup2(saved_stdin, STDIN_FILENO) == -1)
        perror("smash error: dup2 failed");
    if (close(saved_stdin) == -1)
        perror("smash error: close failed");

    delete command1;
    delete command2;
}

/**
 *  JobEntry Implementation
 */
JobEntry::JobEntry(string cmd_line, pid_t pid)
    : cmd_line(cmd_line)
    , job_id(-1) // TODO: change the job id later after inserting to the list
    , pid(pid)
    , stopped(false)
{
    time(&time_epoch);
}

/**
 *  JobsList Implementation
 */
void JobsList::addJob(const JobEntry& job)
{
    // gets the job id of the last member in the map and append by 1
    int job_id = job.job_id;
    if (job_id == -1) {
        job_id = (m_job_list.size() == 0) ? 1 : m_job_list.rbegin()->first + 1;
        m_job_list[job_id] = job;
        m_job_list[job_id].job_id = job_id;
    } else {
        m_job_list[job_id] = job;
    }
}

void JobsList::printJobsList()
{
    time_t present;
    time(&present);
    for (auto& it : m_job_list) {
        const JobEntry& job = it.second;
        int passed_time = static_cast<int>(difftime(present, job.time_epoch));
        cout << "[" << job.job_id << "] " << job.cmd_line << " : " << job.pid << " " << passed_time << " secs";
        cout << ((job.stopped) ? " (stopped)" : "") << endl;
    }
}

void JobsList::killAllJobs()
{
    for (auto& it : m_job_list) {
        if (waitpid(it.second.pid, NULL, WNOHANG) != it.second.pid) {
            cout << it.second.pid << ": " << it.second.cmd_line << endl;
            kill(it.second.pid, SIGKILL);
            waitpid(it.second.pid, NULL, 0);
        }
    }
}

void JobsList::removeFinishedJobs()
{
    for (auto it = m_job_list.begin(); it != m_job_list.end();) {
        if (waitpid(it->second.pid, NULL, WNOHANG) == it->second.pid) {
            it = m_job_list.erase(it);
        } else {
            ++it;
        }
    }
}

bool JobsList::jobExist(int jobs_id)
{
    return m_job_list.find(jobs_id) != m_job_list.end();
}

int JobsList::jobsNumber()
{
    return m_job_list.size();
}

bool JobsList::stoppedJobs()
{
    for (auto& it : m_job_list) {
        if (it.second.stopped)
            return true;
    }
    return false;
}

JobEntry& JobsList::getJobById(int jobs_id)
{
    if (jobs_id == -1) {
        return m_job_list.rbegin()->second;
    }
    return m_job_list[jobs_id];
}

JobEntry& JobsList::getLastStoppedJob()
{
    auto it = m_job_list.rbegin();
    for (; it != m_job_list.rend();) {
        if (it->second.stopped) {
            return it->second;
        } else {
            ++it;
        }
    }
    return it->second;
}

void JobsList::removeJobById(int job_id)
{
    if (job_id == -1) {
        auto last = m_job_list.end();
        last--;
        m_job_list.erase(last);
    } else {
        m_job_list.erase(m_job_list.find(job_id));
    }
}

void SmallShell::executeCommand(const char* cmd_line)
{
    Command* cmd = CreateCommand(cmd_line);
    if (cmd) {
        cmd->execute();
        delete cmd;
    }
}

void SmallShell::waitForJob()
{
    int status;
    if (wait_job_pid == -1) {
        return;
    }
    waitpid(wait_job_pid, &status, WUNTRACED);
    if (WIFSTOPPED(status)) {
        cout << "smash: process " << wait_job_pid << " was stopped" << endl;
        foreground_job.stopped = true;
        jobs_list.addJob(foreground_job);
    } else if (foreground_job_killed) {
        cout << "smash: process " << wait_job_pid << " was killed" << endl;
        foreground_job_killed = false;
    }
    wait_job_pid = -1;
}

void SmallShell::changePromptName(const std::string&& prompt_name)
{
    m_prompt_name = prompt_name;
}

void SmallShell::printPrompt()
{
    std::cout << m_prompt_name << "> ";
}
