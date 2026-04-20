#include <string>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <openssl/crypto.h>
#include <util/log.h>
#include <authdb/auth_db.h>

namespace
{
    void secure_zero_string(std::string & s)
    {
        if (!s.empty())
        {
            OPENSSL_cleanse(&s[0], s.size());
        }
        s.clear();
    }

    // Best-effort scrub of an argv slot so the password no longer appears
    // in /proc/<pid>/cmdline.  The kernel snapshots cmdline at exec time
    // on some systems, so this is defense in depth, not a guarantee.
    void scrub_argv(char * arg)
    {
        if (arg != nullptr)
        {
            size_t n = std::strlen(arg);
            if (n > 0)
            {
                OPENSSL_cleanse(arg, n);
            }
        }
    }

    // Read a password from the controlling terminal with echo disabled.
    // Returns false on any I/O or tty error.  Keeps the result in `out`.
    bool read_password_from_tty(const std::string & prompt, std::string & out)
    {
        out.clear();

        FILE * tty = std::fopen("/dev/tty", "r+");
        if (tty == nullptr)
        {
            LOG_ERROR("no controlling tty available to prompt for password");
            return false;
        }

        int fd = fileno(tty);
        struct termios old_tio;
        struct termios new_tio;
        bool restored = false;

        if (tcgetattr(fd, &old_tio) != 0)
        {
            std::fclose(tty);
            LOG_ERROR("tcgetattr failed");
            return false;
        }
        new_tio = old_tio;
        new_tio.c_lflag &= ~(tcflag_t)(ECHO | ECHONL);
        if (tcsetattr(fd, TCSAFLUSH, &new_tio) != 0)
        {
            std::fclose(tty);
            LOG_ERROR("tcsetattr failed");
            return false;
        }

        std::fputs(prompt.c_str(), tty);
        std::fflush(tty);

        bool ok = true;
        int c;
        while ((c = std::fgetc(tty)) != EOF && c != '\n' && c != '\r')
        {
            out.push_back(static_cast<char>(c));
            // Hard cap to avoid pathological input.
            if (out.size() > 4096)
            {
                ok = false;
                break;
            }
        }
        if (std::ferror(tty))
        {
            ok = false;
        }

        // Restore tty state and emit the newline the user didn't see.
        if (tcsetattr(fd, TCSAFLUSH, &old_tio) == 0)
        {
            restored = true;
        }
        std::fputc('\n', tty);
        std::fclose(tty);

        if (!restored)
        {
            LOG_WARN("failed to restore terminal echo state");
        }
        if (!ok)
        {
            secure_zero_string(out);
            LOG_ERROR("failed to read password from tty");
            return false;
        }
        if (out.empty())
        {
            LOG_ERROR("empty password");
            return false;
        }
        return true;
    }
}

static void print_usage(const char * bin)
{
    std::cout <<
        "set user password (prompted on tty):\n  "
        << bin << " -s <realm> <user> <db_file>\n"
        "set user password (insecure, password on cmdline):\n  "
        << bin << " -S <realm> <user> <password> <db_file>\n"
        "delete user:\n  "
        << bin << " -d <realm> <user> <db_file>\n"
        "Note: <realm> must match the server's configured digest realm or\n"
        "the user will not be able to authenticate via digest auth.\n";
}

int main(int argc, char ** argv)
{
    const char * bin = (argc > 0 && argv[0] != nullptr) ? argv[0] : "shield";

    if (argc < 2)
    {
        print_usage(bin);
        return 1;
    }

    std::string cmd(argv[1]);

    if (cmd == "-s" || cmd == "-S")
    {
        const bool password_on_cmdline = (cmd == "-S");
        const int expected_argc = password_on_cmdline ? 6 : 5;
        if (argc != expected_argc)
        {
            print_usage(bin);
            return 1;
        }

        std::string realm(argv[2]);
        std::string user(argv[3]);
        std::string password;
        std::string file;

        if (password_on_cmdline)
        {
            password.assign(argv[4]);
            file.assign(argv[5]);
            // Best-effort: scrub the password slot in argv so it stops
            // showing up in /proc/<pid>/cmdline as soon as possible.
            scrub_argv(argv[4]);
        }
        else
        {
            file.assign(argv[4]);
            if (!read_password_from_tty("Password: ", password))
            {
                return 1;
            }
            std::string confirm;
            if (!read_password_from_tty("Confirm:  ", confirm))
            {
                secure_zero_string(password);
                return 1;
            }
            if (password != confirm)
            {
                secure_zero_string(password);
                secure_zero_string(confirm);
                LOG_ERROR("passwords do not match");
                return 1;
            }
            secure_zero_string(confirm);
        }

        if (realm.empty() || user.empty() || file.empty())
        {
            secure_zero_string(password);
            LOG_ERROR("realm, user, and db_file must be non-empty");
            print_usage(bin);
            return 1;
        }

        minerva::auth_db db(realm, file);
        if (!db.initialize())
        {
            secure_zero_string(password);
            LOG_ERROR("failed to load auth db");
            return 1;
        }

        bool ok = db.set_user(user, realm, password);
        secure_zero_string(password);
        if (!ok)
        {
            LOG_ERROR("failed to set user and password in auth db");
            return 1;
        }

        return 0;
    }
    else if (cmd == "-d")
    {
        if (argc != 5)
        {
            print_usage(bin);
            return 1;
        }

        std::string realm(argv[2]);
        std::string user(argv[3]);
        std::string file(argv[4]);

        if (realm.empty() || user.empty() || file.empty())
        {
            LOG_ERROR("realm, user, and db_file must be non-empty");
            print_usage(bin);
            return 1;
        }

        minerva::auth_db db(realm, file);
        if (!db.initialize())
        {
            LOG_ERROR("failed to load auth db");
            return 1;
        }

        if (!db.delete_user(user))
        {
            LOG_ERROR("failed to delete user from auth db");
            return 1;
        }

        return 0;
    }

    print_usage(bin);
    return 1;
}

