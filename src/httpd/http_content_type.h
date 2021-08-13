#pragma once

#include <string>

namespace httpd
{

    class http_content_type
    {
    public:
    
        enum code {
            CONTENT_TYPE_UNKNOWN = 0, // Unknown
            CONTENT_TYPE_TEXT_PLAIN,  //Plain text content type
            CONTENT_TYPE_TEXT_HTML,   // HTML content type
            CONTENT_TYPE_TEXT_XML,    // XML content type 
            CONTENT_TYPE_TEXT_CSS,    // Cascading style sheet content type
            CONTENT_TYPE_TEXT_JAVASCRIPT, // JavaScript content type
            CONTENT_TYPE_IMAGE_JPEG,    // Binary image content type*/
            CONTENT_TYPE_IMAGE_PNG,    // Binary image content type*/
            CONTENT_TYPE_MULTIPART_MIXED, // multipart/mixed content type
            CONTENT_TYPE_MULTIPART_X_MIXED_REPLACE, // multipart/x-mixed-replace content type
            CONTENT_TYPE_MULTIPART_FORM, // multipart/form-data type
            CONTENT_TYPE_APPLICATION_XML, // XML content type
            CONTENT_TYPE_APPLICATION_JSON,    // JSON content type 
            CONTENT_TYPE_RTSP_TUNNELLED,    // RTSP tunnelled content type 
            CONTENT_TYPE_OCTET_STREAM,    // RTSP tunnelled content type 
            CONTENT_TYPE_CSV,    // comma separated values
            CONTENT_TYPE_APPLICATION_X_TAR,    // TAR content type
            CONTENT_TYPE_APPLICATION_ZIP,    // zip content type
        };
    
        constexpr static char CONTENT_TYPE_TEXT_PLAIN_TEXT[] =
            "text/plain";
        constexpr static char CONTENT_TYPE_TEXT_HTML_TEXT[] =
            "text/html";
        constexpr static char CONTENT_TYPE_TEXT_XML_TEXT[] = 
            "text/xml";
        constexpr static char  CONTENT_TYPE_APPLICATION_XML_TEXT[] =
            "application/xml";
        constexpr static char  CONTENT_TYPE_APPLICATION_JSON_TEXT[] =
            "application/json";
        constexpr static char CONTENT_TYPE_TEXT_CSS_TEXT[] = 
            "text/css";
        constexpr static char CONTENT_TYPE_TEXT_JAVASCRIPT_TEXT[] =
            "text/javascript";
        constexpr static char CONTENT_TYPE_JPEG_IMAGE_TEXT[] = 
            "image/jpeg";
        constexpr static char CONTENT_TYPE_PNG_IMAGE_TEXT[] = 
            "image/png";
        constexpr static char CONTENT_TYPE_MULTIPART_FORM_TEXT[] =
            "multipart/form-data";
        constexpr static char CONTENT_TYPE_ALL[] = "*/*";
        constexpr static char CONTENT_TYPE_MULTIPART_MIXED_TEXT[] =
            "multipart/mixed; boundary=\"--ovready";
        constexpr static char CONTENT_TYPE_MULTIPART_X_MIXED_REPLACE_TEXT[] =
            "multipart/x-mixed-replace; boundary=\"--ovready\"";
        constexpr static char CONTENT_TYPE_RTSP_TUNNELLED_TEXT [] = 
            "application/x-rtsp-tunnelled";
        constexpr static char CONTENT_TYPE_OCTET_STREAM_TEXT [] = 
            "application/octet-stream";
        constexpr static char CONTENT_TYPE_CSV_TEXT [] = 
            "text/csv";
        constexpr static char CONTENT_TYPE_APPLICATION_X_TAR_TEXT [] = 
            "application/x-tar";
        constexpr static char CONTENT_TYPE_APPLICATION_ZIP_TEXT [] = 
            "application/zip";

        static code parse(const std::string & contentType);

        static const char * get_content_type_string(code code);

    };
}
