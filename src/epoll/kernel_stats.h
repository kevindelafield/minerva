#include <fstream>
#include <iostream>
#include <string>
#include <ovhttpd/controller.h>
#include <ovhttpd/http_context.h>
#include "kernel.h"

namespace epoll
{

    class kernel_stats : public kernel, ovhttpd::controller
    {
    public:
        kernel_stats() = default;
        virtual ~kernel_stats() = default;

        constexpr static char PATH[] = "stats";

        constexpr static char NAME[] = "stats";

        const char* name() override
        {
            return NAME;
        }

        void initialize() override;

        void handle_request(ovhttpd::http_context & ctx, const std::string & op) override;
    };
}
