#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <owl/controller.h>
#include <owl/http_context.h>
#include <owl/component.h>

namespace www
{

    class kernel_stats : public owl::component, owl::controller
    {
    public:
        kernel_stats() = default;
        virtual ~kernel_stats() = default;

        constexpr static char PATH[] = "stats";

        constexpr static char NAME[] = "stats";

        std::string name() override
        {
            return NAME;
        }

        void initialize() override;

        bool auth_callback(const std::string & user,
                           const std::string & op) override;

        void handle_request(owl::http_context & ctx, const std::string & op) override;
    };
}
