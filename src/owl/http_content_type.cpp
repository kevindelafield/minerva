#include "http_content_type.h"

namespace owl
{

    const char * http_content_type::get_content_type_string(code code)
    {
        switch (code)
        {
        case CONTENT_TYPE_UNKNOWN:
            return CONTENT_TYPE_ALL;
        case CONTENT_TYPE_TEXT_PLAIN:
            return CONTENT_TYPE_TEXT_PLAIN_TEXT;
        case CONTENT_TYPE_TEXT_HTML:
            return CONTENT_TYPE_TEXT_HTML_TEXT;
        case CONTENT_TYPE_TEXT_XML:
            return CONTENT_TYPE_TEXT_XML_TEXT;
        case CONTENT_TYPE_TEXT_CSS:
            return CONTENT_TYPE_TEXT_CSS_TEXT;
        case CONTENT_TYPE_TEXT_JAVASCRIPT:
            return CONTENT_TYPE_TEXT_JAVASCRIPT_TEXT;
        case CONTENT_TYPE_IMAGE_JPEG:
            return CONTENT_TYPE_JPEG_IMAGE_TEXT;
        case CONTENT_TYPE_IMAGE_PNG:
            return CONTENT_TYPE_PNG_IMAGE_TEXT;
        case CONTENT_TYPE_MULTIPART_MIXED:
            return CONTENT_TYPE_MULTIPART_MIXED_TEXT;
        case CONTENT_TYPE_MULTIPART_X_MIXED_REPLACE:
            return CONTENT_TYPE_MULTIPART_X_MIXED_REPLACE_TEXT;
        case CONTENT_TYPE_MULTIPART_FORM:
            return CONTENT_TYPE_MULTIPART_FORM_TEXT;
        case CONTENT_TYPE_APPLICATION_XML:
            return CONTENT_TYPE_APPLICATION_XML_TEXT;
        case CONTENT_TYPE_APPLICATION_JSON:
            return CONTENT_TYPE_APPLICATION_JSON_TEXT;
        case CONTENT_TYPE_RTSP_TUNNELLED:
            return CONTENT_TYPE_RTSP_TUNNELLED_TEXT;
        case CONTENT_TYPE_OCTET_STREAM:
            return CONTENT_TYPE_OCTET_STREAM_TEXT;
        case CONTENT_TYPE_CSV:
            return CONTENT_TYPE_CSV_TEXT;
        case CONTENT_TYPE_APPLICATION_X_TAR:
            return CONTENT_TYPE_APPLICATION_X_TAR_TEXT;
        case CONTENT_TYPE_APPLICATION_ZIP:
            return CONTENT_TYPE_APPLICATION_ZIP_TEXT;
        default:
            return CONTENT_TYPE_ALL;
        }
    }

    http_content_type::code http_content_type::parse(const std::string & contentType)
    {
        if (contentType == CONTENT_TYPE_TEXT_PLAIN_TEXT)
        {
            return code::CONTENT_TYPE_TEXT_PLAIN;
        }
        else if (contentType == CONTENT_TYPE_TEXT_HTML_TEXT)
        {
            return code::CONTENT_TYPE_TEXT_HTML;
        }
        else if (contentType == CONTENT_TYPE_TEXT_XML_TEXT)
        {
            return code::CONTENT_TYPE_TEXT_XML;
        }
        else if (contentType == CONTENT_TYPE_APPLICATION_XML_TEXT)
        {
            return code::CONTENT_TYPE_APPLICATION_XML;
        }
        else if (contentType == CONTENT_TYPE_APPLICATION_JSON_TEXT)
        {
            return code::CONTENT_TYPE_APPLICATION_JSON;
        }
        else if (contentType == CONTENT_TYPE_TEXT_CSS_TEXT)
        {
            return code::CONTENT_TYPE_TEXT_CSS;
        }
        else if (contentType == CONTENT_TYPE_TEXT_JAVASCRIPT_TEXT)
        {
            return code::CONTENT_TYPE_TEXT_JAVASCRIPT;
        }
        else if (contentType == CONTENT_TYPE_JPEG_IMAGE_TEXT)
        {
            return code::CONTENT_TYPE_IMAGE_JPEG;
        }
        else if (contentType == CONTENT_TYPE_PNG_IMAGE_TEXT)
        {
            return code::CONTENT_TYPE_IMAGE_PNG;
        }
        else if (contentType == CONTENT_TYPE_MULTIPART_FORM_TEXT)
        {
            return code::CONTENT_TYPE_MULTIPART_FORM;
        }
        else if (contentType == CONTENT_TYPE_ALL)
        {
            return code::CONTENT_TYPE_UNKNOWN;
        }
        else if (contentType == CONTENT_TYPE_MULTIPART_MIXED_TEXT)
        {
            return code::CONTENT_TYPE_MULTIPART_MIXED;
        }
        else if (contentType == CONTENT_TYPE_MULTIPART_X_MIXED_REPLACE_TEXT)
        {
            return code::CONTENT_TYPE_MULTIPART_X_MIXED_REPLACE;
        }
        else if (contentType == CONTENT_TYPE_RTSP_TUNNELLED_TEXT)
        {
            return code::CONTENT_TYPE_RTSP_TUNNELLED;
        }
        else if (contentType == CONTENT_TYPE_OCTET_STREAM_TEXT)
        {
            return code::CONTENT_TYPE_OCTET_STREAM;
        }
        else if (contentType == CONTENT_TYPE_CSV_TEXT)
        {
            return code::CONTENT_TYPE_CSV;
        }
        else if (contentType == CONTENT_TYPE_APPLICATION_X_TAR_TEXT)
        {
            return code::CONTENT_TYPE_APPLICATION_X_TAR;
        }
        else if (contentType == CONTENT_TYPE_APPLICATION_ZIP_TEXT)
        {
            return code::CONTENT_TYPE_APPLICATION_ZIP;
        }
        else
        {
            return code::CONTENT_TYPE_UNKNOWN;
        }
    }
}
