#include <fstream>
#include <iostream>
#include <string>
#include <owl/controller.h>
#include <owl/json_utils.h>
#include <owl/http_context.h>
#include <owl/component.h>

namespace www
{

    class file_server : public owl::component, public owl::controller
    {
    public:
        file_server() = default;
        virtual ~file_server() = default;

        constexpr static char NAME[] = "www-default";

        std::string name() override
        {
            return NAME;
        }

        void config(const Json::Value & config)
        {
            m_config = config;
        }

        Json::Value config() const
        {
            return m_config;
        }

        void initialize() override;

        bool auth_callback(const std::string & user,
                           const std::string & op) override;

        void handle_request(owl::http_context & ctx, const std::string & op) override;

    private:
        std::string m_root_dir;
        std::string m_default_file;
        Json::Value m_config;
    };
}
