#include <cstring>
#include <sstream>
#include <algorithm>
#include "curl_easy.h"
#include "log.h"

namespace minerva
{
    bool curl_easy::isGlobalInitialized = false;

    const char curl_easy::notInitializedFatalErrorMessage[] = "curl_easy member function called before curl_easy::InitializeCurl(long).";

#define FAIL_IF_NOT_INITIALIZED() {                             \
        if (!curl_easy::isGlobalInitialized)                    \
        {                                                       \
            FATAL(curl_easy::notInitializedFatalErrorMessage);  \
        }                                                       \
    }
    
#define SET_CURLOPT_OR_FAIL(option, param) {                            \
        auto res = curl_easy_setopt(this->curl, option, param);         \
        if (res != CURLE_OK)                                            \
        {                                                               \
            LOG_WARN("curl_easy_setopt failed for " << #option << ": " << res); \
            return res;                                                 \
        }                                                               \
    }
    
#define GET_CURLINFO_OR_FAIL(option, param) {                           \
        auto res = curl_easy_getinfo(this->curl, option, &param);       \
        if (res != CURLE_OK)                                            \
        {                                                               \
            LOG_WARN("curl_easy_setopt failed for " << #option << ": " << res); \
            return res;                                                 \
        }                                                               \
    }
    
    CURLcode curl_easy::get_statistics(request_statistics & stats)
    {
        curl_off_t total_offset;
        GET_CURLINFO_OR_FAIL(CURLINFO_TOTAL_TIME_T, total_offset);
        curl_off_t dns_offset;
        GET_CURLINFO_OR_FAIL(CURLINFO_NAMELOOKUP_TIME_T, dns_offset);
        curl_off_t connect_offset;
        GET_CURLINFO_OR_FAIL(CURLINFO_CONNECT_TIME_T, connect_offset);
        curl_off_t ssl_offset;
        GET_CURLINFO_OR_FAIL(CURLINFO_APPCONNECT_TIME_T, ssl_offset);
        curl_off_t send_offset;
        GET_CURLINFO_OR_FAIL(CURLINFO_STARTTRANSFER_TIME_T, send_offset);

        stats.mTotal = static_cast<long long>(total_offset);
        stats.mGetHostAddrTotal = 
            static_cast<long long>(dns_offset);
        stats.mConnectTotal = 
            static_cast<long long>(connect_offset - dns_offset);
        stats.mSSLConnectTotal = 
            static_cast<long long>(ssl_offset - connect_offset);
        stats.mSendTotal = 
            static_cast<long long>(send_offset - ssl_offset);
        stats.mResponseTotal = 
            static_cast<long long>(total_offset - send_offset);

        return CURLE_OK;
    }

    size_t curl_easy::read_stream_function(char* buffer, size_t size, size_t nItems, void* userData)
    {
        auto pReadBuffer = static_cast<ReadBuffer*>(userData);
        const auto available = pReadBuffer->length - pReadBuffer->pos;

        if (available <= 0)
        {
            return 0;
        }

		const size_t nCopied = std::min((size * nItems), static_cast<size_t>(available));
        memcpy(buffer, &(pReadBuffer->buffer[pReadBuffer->pos]), nCopied);
        pReadBuffer->pos += nCopied;
        return nCopied;
    }

    int curl_easy::seek_stream_function(void *userData, curl_off_t offset, int origin)
    {
        auto pReadBuffer = static_cast<ReadBuffer*>(userData);
        if (origin == SEEK_SET)
        {
            pReadBuffer->pos = offset;
        }
        else if (origin == SEEK_CUR)
        {
            pReadBuffer->pos += offset;
        }
        else
        {
            return CURL_SEEKFUNC_FAIL; // Makes no sense. Fail.
        }
        return CURL_SEEKFUNC_OK;
    }

    size_t curl_easy::write_function(char* buffer, size_t size, size_t nItems, void* userData)
    {
        auto oss = static_cast<std::ostringstream*>(userData);
        const auto bytesWritten = size * nItems;
        oss->write(buffer, bytesWritten);
        return bytesWritten;
    }

    void curl_easy::initialize_curl(const long flags)
    {
        if (0 != curl_global_init(flags))
        {
            FATAL("Failed to initialize the CURL library. curl_global_init(" << 
                  flags << " returned non-zero.");
        }
        isGlobalInitialized = true;
    }


    CURLcode curl_easy::set_cert_location(const std::string & path)
    {
            // set ca certs location
        auto res = curl_easy_setopt(this->curl, CURLOPT_CAINFO, 
                                    path.c_str());
        if (res != CURLE_OK)
        {
            LOG_WARN("Failed to set CURL option CURLOPT_CAINFO: " << res);
        }
        return res;
    }

    CURLcode curl_easy::set_timeout(int timeoutSecs)
    {
        FAIL_IF_NOT_INITIALIZED();

        // set timeout
        SET_CURLOPT_OR_FAIL(CURLOPT_TIMEOUT, static_cast<long>(timeoutSecs));
        return CURLE_OK;
    }

    curl_easy::curl_easy()
        : curl(nullptr), curlErrorBuffer()
    {
        if (curl_easy::isGlobalInitialized)
        {
            this->curl = curl_easy_init();
            if (this->curl ==  nullptr)
            {
                throw std::runtime_error("Failed to initialize CURL via curl_easy_init().");
            }

            memset(this->curlErrorBuffer, 0, sizeof(this->curlErrorBuffer));
            auto res = curl_easy_setopt(this->curl, CURLOPT_ERRORBUFFER, this->curlErrorBuffer);
            if (res != CURLE_OK)
            {
				std::ostringstream msg;
                msg << "Failed to set CURL option CURLOPT_ERRORBUFFER: " << res;
                throw std::runtime_error(msg.str());
            }

            // set a defaulttimeout
            res = curl_easy_setopt(this->curl, CURLOPT_TIMEOUT, 10L);
            if (res != CURLE_OK)
            {
				std::ostringstream msg;
                msg << "Failed to set CURL option CURLOPT_TIMEOUT: " << res;
                throw std::runtime_error(msg.str());
            }

            res = curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, &(this->responseBodyStream));
            if (res != CURLE_OK)
            {
				std::ostringstream msg;
                msg << "Failed to set CURL option CURLOPT_WRITEDATA: " << res;
                throw std::runtime_error(msg.str());
            }

            res = curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, curl_easy::write_function);
            if (res != CURLE_OK)
            {
				std::ostringstream msg;
                msg << "Failed to set CURL option CURLOPT_WRITEFUNCTION: " << res;
                throw std::runtime_error(msg.str());
            }

            res = curl_easy_setopt(this->curl, CURLOPT_HEADERDATA, &(this->responseHeaderStream));
            if (res != CURLE_OK)
            {
				std::ostringstream msg;
                msg << "Failed to set CURL option CURLOPT_WRITEDATA: " << res;
                throw std::runtime_error(msg.str());
            }

            res = curl_easy_setopt(this->curl, CURLOPT_HEADERFUNCTION, curl_easy::write_function);
            if (res != CURLE_OK)
            {
				std::ostringstream msg;
                msg << "Failed to set CURL option CURLOPT_HEADERFUNCTION: " << res;
                throw std::runtime_error(msg.str());
            }
        }
    }
           
    curl_easy::curl_easy(const curl_easy& other)
        : curl(nullptr), curlErrorBuffer()
    {
        if (other.curl != nullptr)
        {
            this->curl = curl_easy_duphandle(other.curl);
            if (this->curl ==  nullptr)
            {
                throw std::runtime_error("Failed to initialize CURL via curl_easy_duphandle().");
            }
        }
    }

    curl_easy::curl_easy(curl_easy&& other) noexcept
        : curl(nullptr), curlErrorBuffer()
    {
        if (other.curl != nullptr)
        {
            this->curl = other.curl;
            other.curl = nullptr;
        }
    }

    curl_easy& curl_easy::operator=(const curl_easy& other)
    {
        if (this == &other)
        {
            return *this;
        }
        if (curl_easy::isGlobalInitialized)
        {
            curl_easy_cleanup(this->curl);
            this->curl = curl_easy_duphandle(other.curl);
        }
        return *this;
    }

    curl_easy& curl_easy::operator=(curl_easy&& other) noexcept
    {
        if (this == &other)
            return *this;
        curl = other.curl;
        return *this;
    }

    curl_easy::~curl_easy() noexcept
    {
        if (this->curl != nullptr)
        {
            // deallocate curl handle
            curl_easy_cleanup(this->curl);
        }
    }

    CURLcode curl_easy::set_url(const protocol protocol, 
                                const std::string& host, 
                                const unsigned short int port, 
                                const std::string& path, 
                                const bool validateCertificate) const
    {
        FAIL_IF_NOT_INITIALIZED();

		std::ostringstream ss;
        ss << (protocol == protocol::http ? "http://" : "https://") << host << ":" << port;
        if (path.at(0) != '/')
        {
            ss << "/";
        }
        ss << path;

        // 
        // optionally do not validate cert
        //
        if (!validateCertificate && protocol == protocol::https)
        {
            SET_CURLOPT_OR_FAIL(CURLOPT_SSLENGINE_DEFAULT, 1L);
            SET_CURLOPT_OR_FAIL(CURLOPT_SSL_VERIFYPEER, 0L);
            SET_CURLOPT_OR_FAIL(CURLOPT_SSL_VERIFYHOST, 0L);
        }

        return curl_easy_setopt(this->curl, CURLOPT_URL, ss.str().c_str());
    }

    CURLcode curl_easy::set_authentication(const std::string & username, 
                                           const std::string & password) const
    {
        FAIL_IF_NOT_INITIALIZED();

        SET_CURLOPT_OR_FAIL(CURLOPT_USERNAME, username.c_str());
        SET_CURLOPT_OR_FAIL(CURLOPT_PASSWORD, password.c_str());
        SET_CURLOPT_OR_FAIL(CURLOPT_HTTPAUTH, CURLAUTH_ANYSAFE);
        return CURLE_OK;
    }

    void curl_easy::set_verbose(bool isVerbose) const
    {
        FAIL_IF_NOT_INITIALIZED();
        curl_easy_setopt(this->curl, CURLOPT_VERBOSE, isVerbose ? 1L : 0L);
    }

    CURLcode curl_easy::post(const slist & headers, 
                             const std::string & body, 
                             http_response & response)
    {
        FAIL_IF_NOT_INITIALIZED();

        ReadBuffer readBuffer = { body.length(), 0, body.c_str() };

        if (!this->userAgent.empty())
        {
            SET_CURLOPT_OR_FAIL(CURLOPT_USERAGENT, this->userAgent.c_str());
        }
        SET_CURLOPT_OR_FAIL(CURLOPT_POST, 1L);
        SET_CURLOPT_OR_FAIL(CURLOPT_READDATA, &readBuffer);
        SET_CURLOPT_OR_FAIL(CURLOPT_READFUNCTION, read_stream_function);
        SET_CURLOPT_OR_FAIL(CURLOPT_SEEKDATA, &readBuffer);
        SET_CURLOPT_OR_FAIL(CURLOPT_SEEKFUNCTION, seek_stream_function);

        SET_CURLOPT_OR_FAIL(CURLOPT_POSTFIELDSIZE_LARGE, 
                            static_cast<long>(body.length()));

        SET_CURLOPT_OR_FAIL(CURLOPT_HTTPHEADER, headers.get_slist());

        const auto res = curl_easy_perform(this->curl);

        if (res != CURLE_OK)
        {
            LOG_WARN("curl_easy_perform failed: " << res << " - " <<
                     this->get_last_error());
        }

        long code = 0;
        curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &code);
        response.set_response(static_cast<unsigned short int>(code), 
                              this->responseHeaderStream.str(), 
                              this->responseBodyStream.str());

        return res;
    }

    CURLcode curl_easy::post(const slist & headers, 
                             const std::vector<unsigned char> & body, 
                             http_response & response)
    {
        FAIL_IF_NOT_INITIALIZED();

        ReadBuffer readBuffer = 
            { 
                body.size(), 
                0, 
                reinterpret_cast<const char *>(body.data())
            };

        if (!this->userAgent.empty())
        {
            SET_CURLOPT_OR_FAIL(CURLOPT_USERAGENT, this->userAgent.c_str());
        }
        SET_CURLOPT_OR_FAIL(CURLOPT_POST, 1L);
        SET_CURLOPT_OR_FAIL(CURLOPT_READDATA, &readBuffer);
        SET_CURLOPT_OR_FAIL(CURLOPT_READFUNCTION, read_stream_function);
        SET_CURLOPT_OR_FAIL(CURLOPT_SEEKDATA, &readBuffer);
        SET_CURLOPT_OR_FAIL(CURLOPT_SEEKFUNCTION, seek_stream_function);

        SET_CURLOPT_OR_FAIL(CURLOPT_POSTFIELDSIZE_LARGE, 
                            static_cast<long>(body.size()));

        SET_CURLOPT_OR_FAIL(CURLOPT_HTTPHEADER, headers.get_slist());

        const auto res = curl_easy_perform(this->curl);

        if (res != CURLE_OK)
        {
            LOG_WARN("curl_easy_perform failed: " << res << " - " <<
                     this->get_last_error());
        }

        long code = 0;
        curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &code);
        response.set_response(static_cast<unsigned short int>(code), 
                              this->responseHeaderStream.str(), 
                              this->responseBodyStream.str());

        return res;
    }

    CURLcode curl_easy::get(const slist & headers, http_response & response)
    {
        FAIL_IF_NOT_INITIALIZED();

        if (!this->userAgent.empty())
        {
            SET_CURLOPT_OR_FAIL(CURLOPT_USERAGENT, this->userAgent.c_str());
        }
        SET_CURLOPT_OR_FAIL(CURLOPT_HTTPGET, 1L);
        SET_CURLOPT_OR_FAIL(CURLOPT_HTTPHEADER, headers.get_slist());

        const auto res = curl_easy_perform(this->curl);

        if (res != CURLE_OK)
        {
            LOG_WARN("curl_easy_perform failed: " << res << " - " <<
                     this->get_last_error());
        }

        long code = 0;
        curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &code);
        response.set_response(static_cast<unsigned short int>(code), 
                              this->responseHeaderStream.str(), 
                              this->responseBodyStream.str());

        return res;
    }
}
