#include <poll.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <cstring>
#include <cassert>
#include "log.h"
#include "exec_utils.h"
#include "string_utils.h"

namespace util
{
    pid_t get_pid_by_name(const std::string & name)
    {
        std::vector<std::string> args;
        args.push_back("-s");
        args.push_back(name);
        execl proc("/bin/pidof", args);

        int ppid;

        if (!proc.start(ppid))
        {
            return -1;
        }

        proc.close();

        int status = proc.wait();
        if (status)
        {
            return -1;
        }

        std::string first;

        proc.stdout() >> first;

        pid_t pid;

        if (!try_parse_int(first, pid))
        {
            LOG_ERROR("unexpected output from pidof: " << first);
            return -1;
        }

        return pid;
    }

    execl::execl(const std::string & process,
                 const std::vector<std::string> & args) :
        m_process(process), m_args(args)
    {
    }

    execl::~execl()
    {
        if (m_in)
        {
            fclose(m_in);
        }
        if (m_tout)
        {
            LOG_DEBUG("joining tout");
            m_tout->join();
            delete m_tout;
            LOG_DEBUG("joined tout");
        }
        if (m_terr)
        {
            LOG_DEBUG("joining terr");
            m_terr->join();
            delete m_terr;
            LOG_DEBUG("joined terr");
        }
        if (m_out)
        {
            fclose(m_out);
        }
        if (m_err)
        {
            fclose(m_err);
        }
        if (m_pid > -1 && !m_bg)
        {
            kill(m_pid, SIGTERM);
        }
    }

    void execl::close()
    {
        if (m_in)
        {
            if (m_debug)
            {
                LOG_INFO("flusing in");
            }
            fflush(m_in);
            if (m_debug)
            {
                LOG_INFO("closing in");
            }
            if (fclose(m_in))
            {
                LOG_ERROR("failed to close stdin of subprocess");
            }
            m_in = nullptr;
        }
        else
        {
            LOG_INFO("in is null");
        }
    }

    bool execl::write(const char * data, size_t size)
    {
        assert(m_pid > -1);
        return fwrite(data, 1, size, m_in) == size;
    }

    bool execl::write(std::istream & str)
    {
        if (!str)
        {
            LOG_WARN("stream is closed");
            return false;
        }
        auto sz = str.rdbuf()->pubseekoff(0, str.end);
        str.rdbuf()->pubseekoff(0, str.beg);
        while (sz > 0 && str)
        {
            char buf[2048];

            auto to_read =
                std::min(static_cast<long unsigned int>(sz), sizeof(buf));

            int read = str.rdbuf()->sgetn(buf, to_read);
            if (read == 0)
            {
                LOG_WARN("failed to read from stream");
                return false;
            }
            if (!write(buf, read))
            {
                LOG_WARN("failed to write to stream");
                return false;
            }
            sz -= read;
        }
        if (!str)
        {
            return false;
        }
        return true;
    }

    int execl::wait()
    {
        assert(m_pid > -1);
        if (m_debug)
        {
            LOG_INFO("joining out thread");
        }
        m_tout->join();
        delete m_tout;
        m_tout = nullptr;
        if (m_debug)
        {
            LOG_INFO("joining err thread");
        }
        m_terr->join();
        delete m_terr;
        m_terr = nullptr;

        if (m_debug)
        {
            LOG_INFO("waiting for pid to exit");
        }
        
        siginfo_t info;
        if (waitid(P_PID, m_pid, &info, WEXITED))
        {
            LOG_ERROR_ERRNO("failed to wait on pid", errno);
            return 1;
        }
        if (m_debug)
        {
            LOG_INFO("child process has exited");
        }
        return WEXITSTATUS(info.si_status);
    }

    bool execl::start(int & bgid, bool nohup)
    {
        bgid = -1;

        // open pipe
        int filedes[2];
        int filedes_err[2];
        int filedes_in[2];
        
        // fork
        {
            if (pipe2(filedes, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                return false;
            }
            if (pipe2(filedes_err, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                ::close(filedes[1]);
                ::close(filedes[0]);
                return false;
            }
            if (pipe2(filedes_in, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                ::close(filedes[1]);
                ::close(filedes[0]);
                ::close(filedes_err[1]);
                ::close(filedes_err[0]);
                return false;
            }

            pid_t pid = fork();
            if (pid == -1)
            {
                LOG_ERROR_ERRNO("failed to fork child process", errno);
                ::close(filedes[1]);
                ::close(filedes[0]);
                ::close(filedes_err[1]);
                ::close(filedes_err[0]);
                ::close(filedes_in[1]);
                ::close(filedes_in[0]);
                return false;
            }
            if (pid == 0)
            {
                // dup pipe
                while ((dup2(filedes_in[0], STDIN_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                while ((dup2(filedes[1], STDOUT_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                while ((dup2(filedes_err[1], STDERR_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                ::close(filedes[1]);
                ::close(filedes[0]);
                ::close(filedes_err[1]);
                ::close(filedes_err[0]);
                ::close(filedes_in[1]);
                ::close(filedes_in[0]);
                
                std::vector<char*> args;
                
                args.push_back(const_cast<char*>(m_process.c_str()));
                for (auto & i : m_args)
                {
                    args.push_back(const_cast<char*>(i.c_str()));
                }
                args.push_back(0);
                
                if (nohup)
                {
                    signal(SIGHUP, SIG_IGN);
                }
                
                // exec
                execv(m_process.c_str(), &args[0]);
                
                // exec failed - exit
                LOG_ERROR_ERRNO("failed to execute execv", errno);
                exit(1);
            }
            
            bgid = pid;
            m_pid = pid;
            
            // close half of pipe
            if (::close(filedes[1]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
            if (::close(filedes_err[1]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
            if (::close(filedes_in[0]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
        }

        // open stdout from child process
        m_out = fdopen(filedes[0], "r");
        if (!m_out)
        {
            LOG_ERROR("failed to open output from child process");
            return false;
        }
        
        // open stdout from child process
        m_err = fdopen(filedes_err[0], "r");
        if (!m_err)
        {
            LOG_ERROR("failed to open error output from child process");
            return false;
        }
        
        // open stdout from child process
        m_in = fdopen(filedes_in[1], "w");
        if (!m_in)
        {
            LOG_ERROR("failed to open input to child process");
            return false;
        }
        
        m_tout = new std::thread(&execl::collect, this, m_out, true);
        m_terr = new std::thread(&execl::collect, this, m_err, false);

        return true;
    }

    bool execl::start_nout(int & bgid, bool nohup)
    {
        bgid = -1;
        m_bg = true;

        // open pipe
        int filedes_in[2];
        
        // fork
        {
            if (pipe2(filedes_in, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                return false;
            }
            
            pid_t pid = fork();
            if (pid == -1)
            {
                LOG_ERROR_ERRNO("failed to fork child process", errno);
                ::close(filedes_in[1]);
                ::close(filedes_in[0]);
                return false;
            }
            if (pid == 0)
            {
                // dup pipe
                while ((dup2(filedes_in[0], STDIN_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                ::close(filedes_in[1]);
                ::close(filedes_in[0]);
                
                std::vector<char*> args;
                
                args.push_back(const_cast<char*>(m_process.c_str()));
                for (auto & i : m_args)
                {
                    args.push_back(const_cast<char*>(i.c_str()));
                }
                args.push_back(0);
                
                if (nohup)
                {
                    signal(SIGHUP, SIG_IGN);
                }
                
                // exec
                execv(m_process.c_str(), &args[0]);
                
                // exec failed - exit
                LOG_ERROR_ERRNO("failed to exec init script", errno);
                exit(1);
            }
        
            bgid = pid;
            m_pid = pid;
            
            if (::close(filedes_in[0]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
        }

        // open stdout from child process
        m_in = fdopen(filedes_in[1], "w");
        if (!m_in)
        {
            LOG_ERROR("failed to open input to child process");
            return false;
        }
        
        return true;
    }

    void execl::collect(FILE * fd, bool out)
    {
        assert(fd);

        char * ptr = nullptr;
        size_t len = 0;
        int status = getline(&ptr, &len, fd);
        while (status > -1)
        {
            if (out)
            {
                m_out_stream << ptr;
            }
            else
            {
                m_err_stream << ptr;
            }
            free(ptr);
            // do it again
            ptr = nullptr;
            len = 0;
            status = getline(&ptr, &len, fd);
        }
        free(ptr);

        if (m_debug)
        {
            LOG_INFO("collect thread exiting: " << out);
        }
    }

    execl_stream::execl_stream(const std::string & process,
                               const std::vector<std::string> & args) :
        m_process(process), m_args(args)
    {
    }

    execl_stream::~execl_stream()
    {
        if (m_in)
        {
            fclose(m_in);
        }
        if (m_out)
        {
            fclose(m_out);
        }
        if (m_err)
        {
            fclose(m_err);
        }
        if (m_pid > -1 && !m_bg)
        {
            kill(m_pid, SIGTERM);
        }
    }

    void execl_stream::close()
    {
        if (m_in)
        {
            fflush(m_in);
            if (fclose(m_in))
            {
                LOG_ERROR("failed to close stdin of subprocess");
            }
            m_in = nullptr;
        }
    }

    bool execl_stream::write(const char * data, size_t size)
    {
        assert(m_pid > -1);
        return fwrite(data, 1, size, m_in) == size;
    }

    bool execl_stream::write(std::istream & str)
    {
        if (!str)
        {
            LOG_WARN("stream is closed");
            return false;
        }
        auto sz = str.rdbuf()->pubseekoff(0, str.end);
        str.rdbuf()->pubseekoff(0, str.beg);
        while (sz > 0 && str)
        {
            char buf[2048];

            auto to_read =
                std::min(static_cast<long unsigned int>(sz), sizeof(buf));

            int read = str.rdbuf()->sgetn(buf, to_read);
            if (read == 0)
            {
                LOG_WARN("failed to read from stream");
                return false;
            }
            if (!write(buf, read))
            {
                LOG_WARN("failed to write to stream");
                return false;
            }
            sz -= read;
        }
        if (!str)
        {
            return false;
        }
        return true;
    }

    int execl_stream::wait()
    {
        assert(m_pid > -1);

        siginfo_t info;
        if (waitid(P_PID, m_pid, &info, WEXITED))
        {
            LOG_ERROR_ERRNO("failed to wait on pid", errno);
            return 1;
        }
        return WEXITSTATUS(info.si_status);
    }

    bool execl_stream::start(int & bgid, bool nohup)
    {
        bgid = -1;

        // open pipe
        int filedes[2];
        int filedes_err[2];
        int filedes_in[2];
        
        // fork
        {
            if (pipe2(filedes, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                return false;
            }
            if (pipe2(filedes_err, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                ::close(filedes[1]);
                ::close(filedes[0]);
                return false;
            }
            if (pipe2(filedes_in, O_CLOEXEC))
            {
                LOG_ERROR_ERRNO("failed to create pipe", errno);
                ::close(filedes[1]);
                ::close(filedes[0]);
                ::close(filedes_err[1]);
                ::close(filedes_err[0]);
                return false;
            }
            
            pid_t pid = fork();
            if (pid == -1)
            {
                LOG_ERROR_ERRNO("failed to fork child process", errno);
                ::close(filedes[1]);
                ::close(filedes[0]);
                ::close(filedes_err[1]);
                ::close(filedes_err[0]);
                ::close(filedes_in[1]);
                ::close(filedes_in[0]);
                return false;
            }
            if (pid == 0)
            {
                // dup pipe
                while ((dup2(filedes_in[0], STDIN_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                while ((dup2(filedes[1], STDOUT_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                while ((dup2(filedes_err[1], STDERR_FILENO) == -1))
                {
                    if (errno != EINTR)
                    {
                        LOG_ERROR_ERRNO("failed to dup fd",  errno);
                        exit(1);
                    }
                }
                ::close(filedes[1]);
                ::close(filedes[0]);
                ::close(filedes_err[1]);
                ::close(filedes_err[0]);
                ::close(filedes_in[1]);
                ::close(filedes_in[0]);
                
                std::vector<char*> args;
                
                args.push_back(const_cast<char*>(m_process.c_str()));
                for (auto & i : m_args)
                {
                    args.push_back(const_cast<char*>(i.c_str()));
                }
                args.push_back(0);
                
                if (nohup)
                {
                    signal(SIGHUP, SIG_IGN);
                }
                
                // exec
                execv(m_process.c_str(), &args[0]);
                
                // exec failed - exit
                LOG_ERROR_ERRNO("failed to exec init script", errno);
                exit(1);
            }
            
            bgid = pid;
            m_pid = pid;
            
            // close half of pipe
            if (::close(filedes[1]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
            if (::close(filedes_err[1]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
            if (::close(filedes_in[0]))
            {
                LOG_ERROR_ERRNO("failed to close file descriptor", errno);
            }
        }

        // open stdout from child process
        m_out = fdopen(filedes[0], "r");
        if (!m_out)
        {
            LOG_ERROR("failed to open output from child process");
            return false;
        }
        
        // open stdout from child process
        m_err = fdopen(filedes_err[0], "r");
        if (!m_err)
        {
            LOG_ERROR("failed to open error output from child process");
            return false;
        }
        
        // open stdout from child process
        m_in = fdopen(filedes_in[1], "w");
        if (!m_in)
        {
            LOG_ERROR("failed to open input to child process");
            return false;
        }
        
        return true;
    }

    bool execl_stream::poll(bool & stdout, bool & stderr, int ms)
    {
        stdout = false;
        stderr = false;

        struct pollfd p[2];

        memset(p, 0, sizeof(p));

        p[0].fd = fileno(m_out);
        p[1].fd = fileno(m_err);
        p[0].events = POLLIN | POLLERR || POLLRDHUP;
        p[1].events = POLLIN | POLLERR || POLLRDHUP;

        if (::poll(p, 2, ms) < 0)
        {
            LOG_ERROR_ERRNO("poll failed", errno);
            return false;
        }

        if (p[0].revents)
        {
            stdout = true;
        }
        if (p[1].revents)
        {
            stderr = true;
        }

        return true;
    }

    std::string execl_stream::read_line_stdout()
    {
        std::string ret;

        char * ptr = nullptr;
        size_t len = 0;
        int status = getline(&ptr, &len, m_out);
        if (status > -1)
        {
            std::stringstream ss(ptr);
            std::getline(ss, ret);
            free(ptr);
        }
        return ret;
    }

    std::string execl_stream::read_line_stderr()
    {
        std::string ret;

        char * ptr = nullptr;
        size_t len = 0;
        int status = getline(&ptr, &len, m_err);
        if (status > -1)
        {
            std::stringstream ss(ptr);
            std::getline(ss, ret);
            free(ptr);
        }
        return ret;
    }

    size_t execl_stream::read_stdout(size_t size, char * buf)
    {
        return ::read(fileno(m_out), buf, size);
    }

    size_t execl_stream::read_stderr(size_t size, char * buf)
    {
        return ::read(fileno(m_err), buf, size);
    }

    int execsh(const std::string & cmd, bool nohup)
    {
        std::string so;
        std::string se;
        return execsh(cmd, so, se, nohup);
    }

    int exec(const std::string & cmd, 
             std::string & stdout, 
             std::string & stderr, 
             bool nohup)
    {
        execl proc(cmd, std::vector<std::string>());

        int pid;

        if (!proc.start(pid, nohup))
        {
            LOG_WARN("failed to exec " << cmd);
            return -1;
        }

        proc.close();

        int status = proc.wait();

        std::stringstream ss1;
        ss1 << proc.stdout().rdbuf();
        std::stringstream ss2;
        ss2 << proc.stderr().rdbuf();

        stdout = ss1.str();
        stderr = ss2.str();

        return status;
    }

    int execbg(const std::string & cmd, int & pid, bool nohup)
    {
        std::vector<std::string> args;
        args.push_back("-c");
        args.push_back(cmd);
        execl proc("/bin/sh", args);

        if (!proc.start_nout(pid, nohup))
        {
            LOG_WARN("failed to exec " << cmd);
            return -1;
        }

        proc.close();

        return 0;
    }

    int execsh(const std::string & cmd, 
               std::string & stdout, 
               std::string & stderr, 
               bool nohup)
    {
        std::vector<std::string> args;
        args.push_back("-c");
        args.push_back(cmd);
        execl proc("/bin/sh", args);

        int pid;

        if (!proc.start(pid, nohup))
        {
            LOG_WARN("failed to exec /bin/sh");
            return -1;
        }

        proc.close();

        int status = proc.wait();
        
        std::stringstream ss1;
        ss1 << proc.stdout().rdbuf();
        std::stringstream ss2;
        ss2 << proc.stderr().rdbuf();

        stdout = ss1.str();
        stderr = ss2.str();

        return status;
    }

    bool send_hup(const std::string & proc)
    {
        pid_t pid = get_pid_by_name(proc);
        if (pid < 0)
        {
            LOG_WARN("failed to stat process: " << proc);
            return false;
        }
        int status = kill(pid, SIGHUP);
        if (status)
        {
            LOG_ERROR_ERRNO("failed to send SIGHUP to proc: " << proc, errno);
            return false;
        }
        return true;
    }
}
