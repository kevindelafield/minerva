#include <fstream>
#include <iostream>
#include <string>
#include <ovhttpd/controller.h>
#include <ovhttpd/http_context.h>
#include "kernel.h"

namespace epoll
{

    class kernel_test : public ovhttpd::component, ovhttpd::controller
    {
    public:
        kernel_test() = default;
        virtual ~kernel_test() = default;

        constexpr static char PATH[] = "test";

        constexpr static char NAME[] = "test";

        const char* name() override
        {
            return NAME;
        }

        void initialize() override;

    private:
        void parse(ovhttpd::http_context & ctx);
        void upload(ovhttpd::http_context & ctx);
        void sendfile(ovhttpd::http_context & ctx);

    };
}
