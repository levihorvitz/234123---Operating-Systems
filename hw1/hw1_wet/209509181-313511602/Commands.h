#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <map>
#include <signal.h>

#define COMMAND_ARGS_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define DEFAULT_PROMPT_NAME ("smash")
#define TEMP_FILE ("_temp")

class Command {
public:
    virtual void execute() = 0;
    virtual void CloseFd(int fd = -1);
    virtual ~Command() { }
};

class BuiltInCommand : public Command {
protected:
    size_t m_args_num;
    char* m_args[COMMAND_MAX_ARGS];

public:
    BuiltInCommand(const char* cmd_line);
    virtual ~BuiltInCommand();
};

class PipeCommand : public Command {
    std::string m_cmd_line1;
    std::string m_cmd_line2;
    int m_std_type;

public:
    PipeCommand(const char* cmd_line);
    virtual ~PipeCommand() { }
    void execute() override;
};

class RedirectionCommand : public Command {
private:
    std::string m_cmd_line;
    std::string m_file_name;
    bool m_append;

public:
    explicit RedirectionCommand(const char* cmd_line);
    virtual ~RedirectionCommand() { }
    void execute() override;
};

class ChangePromptCommand : public BuiltInCommand {
public:
    ChangePromptCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~ChangePromptCommand() { }
    void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
public:
    ShowPidCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~ShowPidCommand() { }
    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
public:
    GetCurrDirCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~GetCurrDirCommand() { }
    void execute() override;
};

class ChangeDirCommand : public BuiltInCommand {
public:
    ChangeDirCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~ChangeDirCommand() { }
    void execute() override;
};

class JobsCommand : public BuiltInCommand {
public:
    JobsCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~JobsCommand() { }
    void execute() override;
};

class ForegroundCommand : public BuiltInCommand {
public:
    ForegroundCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~ForegroundCommand() { }
    void execute() override;
};

class BackgroundCommand : public BuiltInCommand {
public:
    BackgroundCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~BackgroundCommand() { }
    void execute() override;
};

class QuitCommand : public BuiltInCommand {
public:
    QuitCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~QuitCommand() { }
    void execute() override;
};

class KillCommand : public BuiltInCommand {
public:
    KillCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }

    virtual ~KillCommand() { }
    void execute() override;
};

class FareCommand : public BuiltInCommand {
public:
    FareCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~FareCommand() { }
    void execute() override;
};

class SetcoreCommand : public BuiltInCommand {
public:
    SetcoreCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
    {
    }
    virtual ~SetcoreCommand() { }
    void execute() override;
};

class TimeoutCommand : public BuiltInCommand {
private:
    std::string m_cmd_line;

public:
    explicit TimeoutCommand(const char* cmd_line)
        : BuiltInCommand(cmd_line)
        , m_cmd_line(cmd_line)
    {
    }
    virtual ~TimeoutCommand() { }
    void execute() override;
};

typedef int job_id_t;

class JobEntry {
public:
    std::string cmd_line;
    job_id_t job_id;
    pid_t pid;
    time_t time_epoch;
    bool stopped;
    JobEntry() { }
    JobEntry(std::string cmd_line, pid_t pid);
};

class ExternalCommand : public Command {
private:
    std::string m_cmd_line;

public:
    JobEntry child_job;
    ExternalCommand(const char* cmd_line)
        : Command()
        , m_cmd_line(cmd_line)
        , child_job()
    {
    }
    virtual ~ExternalCommand() { }
    void execute() override;
    void execute(int close_fd);
};

class JobsList {
private:
    std::map<job_id_t, JobEntry> m_job_list;

public:
    JobsList()
        : m_job_list()
    {
    }
    JobsList(JobsList const&) = delete; // disable copy ctor
    void operator=(JobsList const&) = delete; // disable = operator
    void addJob(const JobEntry& job);
    void printJobsList();
    void killAllJobs();
    void removeFinishedJobs();
    int jobsNumber();
    bool stoppedJobs();
    bool jobExist(job_id_t job_id);
    JobEntry& getJobById(job_id_t jobs_id = -1);
    JobEntry& getLastStoppedJob();
    void removeJobById(job_id_t job_id = -1);
};

class SmallShell {
public:
    bool old_pwd;
    std::string old_path;
    bool exit_shell;
    // if set to -1 the shell don't need to wait
    sig_atomic_t wait_job_pid;
    sig_atomic_t foreground_job_killed;
    JobEntry foreground_job;
    JobsList jobs_list;
    std::map<time_t, JobEntry> timeout_list;

private:
    std::string m_prompt_name;
    SmallShell()
        : old_pwd(false)
        , exit_shell(false)
        , wait_job_pid(-1)
        , foreground_job_killed(false)
        , foreground_job()
        , m_prompt_name(DEFAULT_PROMPT_NAME)
    {
    }

public:
    Command* CreateCommand(const char* cmd_line);
    SmallShell(SmallShell const&) = delete; // disable copy ctor
    void operator=(SmallShell const&) = delete; // disable = operator
    static SmallShell& getInstance() // make SmallShell singleton
    {
        static SmallShell instance;
        return instance;
    }
    ~SmallShell() { }
    void executeCommand(const char* cmd_line);
    void waitForJob();
    void changePromptName(const std::string&& prompt_name = DEFAULT_PROMPT_NAME);
    void printPrompt();
};

#endif // SMASH_COMMAND_H_
