#include <regex>
#include <string>
#include <iostream>
#include "hybrid_lock.h"

int main(int argc, char** argv)
{
    hybrid_lock lock;

    lock.lock();
    lock.unlock();

    std::string header("CONNECT login.microsoftonline.com:443 HTTP/1.0\r\n");
    std::regex connect_regex("^(.*)\\s+(.*):?(\\d*)\\sHTTP/(1.0|1.1)\\r\\n");

    std::smatch m;

    std::regex_search(header, m, connect_regex);

    std::cout << m.size() << std::endl;
    std::cout << m[1] << std::endl;
    std::cout << m[2] << std::endl;

    
    for (auto x : m)
    {
        std::cout << x << std::endl;
    }

    return 0;
}
