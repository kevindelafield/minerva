#include <string>
#include <iostream>
#include <owl/log.h>
#include <authdb/auth_db.h>

static void print_usage(char * bin)
{
    std::cout << "set user password usage: " << bin <<
        " -s <realm> <user> <password> <db_file>" <<
        std::endl;
    std::cout << "delete user usage: " << bin << " -d <realm> <user> <db_file>" <<
        std::endl;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd(argv[1]);

    if (cmd == "-s")
    {
        if (argc != 6)
        {
            print_usage(argv[0]);
            return 1;
        }
        
        std::string realm(argv[2]);
        std::string user(argv[3]);
        std::string password(argv[4]);
        std::string file(argv[5]);

        authdb::auth_db db(realm, file);
        if (!db.initialize())
        {
            LOG_ERROR("failed to load auth db");
            return 1;
        }

        if (!db.set_user(user, realm, password))
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
            print_usage(argv[0]);
            return 1;
        }

        std::string realm(argv[2]);
        std::string user(argv[3]);
        std::string file(argv[4]);

        authdb::auth_db db(realm, file);
        if (!db.initialize())
        {
            LOG_ERROR("failed to load auth db");
            return 1;
        }

        if (!db.delete_user(user))
        {
            LOG_ERROR("failed to set user and password in auth db");
            return 1;
        }

        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
