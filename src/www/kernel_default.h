#include <fstream>
#include <iostream>
#include <string>
#include <owl/controller.h>
#include <owl/http_context.h>
#include <owl/component.h>

namespace www
{

    class kernel_default : public owl::component, public owl::controller
    {
    public:
        kernel_default() = default;
        virtual ~kernel_default() = default;

        constexpr static char NAME[] = "www-default";

        std::string name() override
        {
            return NAME;
        }

        void initialize() override;

        bool auth_callback(const std::string & user,
                           const std::string & op) override;

        void handle_request(owl::http_context & ctx, const std::string & op) override;

    private:
        std::string m_root_dir;
        std::string m_default_file;
    };
}
