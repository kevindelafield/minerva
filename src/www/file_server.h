#include <fstream>
#include <iostream>
#include <string>
#include <util/json_utils.h>
#include <owl/component.h>
#include <httpd/http_context.h>
#include <httpd/controller.h>

namespace minerva
{

    class file_server : public component, public controller
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

        void handle_request(http_context & ctx, const std::string & op) override;

    private:
        std::string m_root_dir;
        std::string m_default_file;
        Json::Value m_config;
    };
}
