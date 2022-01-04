#pragma once

#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include <vector>
#include <set>
#include <mutex>
#include <curl/curl.h>

namespace util
{
    class curl_easy
    {
    public:
        enum protocol
        {
            http,
            https
        };

        class request_statistics
        {
        public:
           request_statistics() :
                mTotal(0),
                mGetHostAddrTotal(0),
                mConnectTotal(0),
                mSSLConnectTotal(0),
                mSendTotal(0),
                mResponseTotal(0)
            {
            }

          request_statistics(const request_statistics & other) = default;

          request_statistics & operator=(const request_statistics & other)
          {
              if (this != &other)
              {
                  mTotal = other.mTotal;
                  mGetHostAddrTotal = other.mGetHostAddrTotal;
                  mConnectTotal = other.mConnectTotal;
                  mSSLConnectTotal = other.mSSLConnectTotal;
                  mSendTotal = other.mSendTotal;
                  mResponseTotal = other.mResponseTotal;
              }
             return *this;
           }

           long long mTotal;
           long long mGetHostAddrTotal;
           long long mConnectTotal;
           long long mSSLConnectTotal;
           long long mSendTotal;
           long long mResponseTotal;
        };

        class slist
        {
        public:
            slist()
                : pList(nullptr)
            {
            }

            explicit slist(const char* str)
                : pList(nullptr)
            {
                pList = curl_slist_append(pList, str);
            }

            explicit slist(const std::string & str)
                : pList(nullptr)
            {
                pList = curl_slist_append(pList, str.c_str());
            }

            slist(const slist & other) = default;

            slist(slist&& other) noexcept
                : pList(other.pList)
            {
            }

            ~slist()
            {
                if (pList != nullptr)
                {
                    curl_slist_free_all(pList);
                }
            }

            slist & operator=(const slist & other)
            {
                if (this == &other)
                {
                    return *this;
                }
                pList = other.pList;
                return *this;
            }

            slist & operator=(slist && other) noexcept
            {
                if (this == &other)
                {
                    return *this;
                }
                pList = other.pList;
                return *this;
            }

            const struct curl_slist* get_slist() const { return pList; }

            void append(const char* str)
            {
                this->pList = curl_slist_append(this->pList, str);
            }

            void append(const std::string& str)
            {
                this->append(str.c_str());
            }

        private:
            struct curl_slist* pList;
        };

        class http_response
        {
        public:
            http_response()
                : statusCode(0)
            {                
            }

            http_response(const short int statusCode, 
                         std::string headers, 
                         std::string body)
                : statusCode(statusCode), 
                headers(std::move(headers)), 
                body(std::move(body))
            {
            }

            void set_response(const short int statusCode, 
                              const std::string & headers, 
                              const std::string & body)
            {
                this->statusCode = statusCode;
                this->headers = headers;
                this->body = body;
            }

            short int get_status_code() const { return this->statusCode; }
            const std::string & get_headers() const { return this->headers; }
            const std::string & get_body() const { return this->body; }

        private:
            short int statusCode;
            std::string headers;
            std::string body;
        };

    public:
        /**
         * Initializes the libcurl library.
         * If the curl library cannot be initialized, this function will
         * terminate the process by calling exit(EXIT_FAILURE).
         * @param flags Flags to pass to curl_global_init(long).
         */
        static void initialize_curl(long flags = CURL_GLOBAL_ALL);

    public:
        curl_easy();
        curl_easy(const curl_easy & other);
        curl_easy(curl_easy && other) noexcept;

        curl_easy & operator=(const curl_easy & other);
        curl_easy & operator=(curl_easy && other) noexcept;

        ~curl_easy() noexcept;

        CURLcode get_statistics(request_statistics & status);

        void set_user_agent(const std::string & userAgent) 
        { 
            this->userAgent = userAgent; 
        }

        const std::string & get_user_agent() const 
        { 
            return this->userAgent; 
        }

        CURLcode set_url(const protocol protocol, 
                         const std::string& host, 
                         const unsigned short int port, 
                         const std::string& path,
                         const bool validateCertificate = true) const;

        CURLcode set_url(const protocol protocol, 
                        const unsigned short int port, 
                         const std::string& path,
                         const bool validateCertificate = true) const
        {
            return this->set_url(protocol, "127.0.0.1", port, path);
        }

        

        CURLcode set_authentication(const std::string & username, 
                                    const std::string & password) const;

        CURLcode set_cert_location(const std::string & path);

        CURLcode set_timeout(int timeoutSecs);

        CURLcode post(const slist & headers, 
                      const std::string & body, 
                      http_response & response);

        CURLcode post(const slist & headers, 
                      const std::vector<unsigned char> & body, 
                      http_response & response);

        CURLcode get(const slist& headers, http_response & response);

        std::string get_last_error() const 
        { 
            return std::string(this->curlErrorBuffer); 
        }

        void set_verbose(bool isVerbose) const;

    private:
        static bool isGlobalInitialized;
        static const char notInitializedFatalErrorMessage[];

        struct ReadBuffer
        {
            size_t length;
            size_t pos;
            const char *buffer;
        };

        CURL *curl;
        char curlErrorBuffer[CURL_ERROR_SIZE];
        std::string userAgent;
        std::ostringstream responseHeaderStream;
        std::ostringstream responseBodyStream;

        static size_t read_stream_function(char* buffer, 
                                           size_t size, 
                                           size_t nItems, 
                                           void* userData);

        static int seek_stream_function(void *userp, 
                                        curl_off_t offset, 
                                        int origin);
        static size_t write_function(char* buffer, 
                                     size_t size, 
                                     size_t nItems, 
                                     void* userData);
    };
}
