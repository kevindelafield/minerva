#pragma once

#include <string>
#include <vector>
#include <istream>
#include <thread>
#include <sstream>

namespace owl
{
    pid_t get_pid_by_name(const std::string & name);

    class execl
    {
    public:
        execl(const std::string & process,
              const std::vector<std::string> & args);

        ~execl();

        bool start(int & pid, bool nohup = false);

        bool start_nout(int & pid, bool nohup = false);

        bool write(const char *data, size_t len);

        bool write(std::istream & str);

        void close();

        int wait();

        void debug(bool enable)
        {
            m_debug = true;
        }

        std::istream & stdout()
        {
            return m_out_stream;
        }
        
        std::istream & stderr()
        {
            return m_err_stream;
        }

    private:
        std::string m_process;
        std::vector<std::string> m_args;
        FILE * m_in = nullptr;
        FILE * m_out = nullptr;
        FILE * m_err = nullptr;
        std::thread * m_tout = nullptr;
        std::thread * m_terr = nullptr;
        bool m_bg = false;
        bool m_debug = false;

        void collect(FILE * fd, bool out);
        
        pid_t m_pid = -1;

        std::stringstream m_out_stream;
        std::stringstream m_err_stream;
    };

    class execl_stream
    {
    public:
        execl_stream(const std::string & process,
                     const std::vector<std::string> & args);

        ~execl_stream();

        bool start(int & pid, bool nohup = false);

        bool start_nout(int & pid, bool nohup = false);

        bool write(const char *data, size_t len);

        bool write(std::istream & str);

        void close();

        int wait();

        std::string read_line_stdout();
        
        std::string read_line_stderr();
        
        size_t read_stdout(size_t size, char * buf);
        
        size_t read_stderr(size_t size, char * buf);
        
        bool poll(bool & stdout, bool & stderr, int ms);

    private:

        std::string m_process;
        std::vector<std::string> m_args;
        FILE * m_in = nullptr;
        FILE * m_out = nullptr;
        FILE * m_err = nullptr;
        bool m_bg = false;

        pid_t m_pid = -1;
    };

    bool send_hup(const std::string & proc);

    int exec(const std::string & cmd, 
             std::string & stdout, 
             std::string & stderr, 
             bool nohup = false);

    int execsh(const std::string & cmd, 
               std::string & stdout, 
               std::string & stderr,
               bool nohup = false);

    int execsh(const std::string & cmd, bool nohup = false);
    
    int execbg(const std::string & cmd, int & pid, bool nohup = false);

}
