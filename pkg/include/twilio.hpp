#pragma once
#include <curl/curl.h>

#include <string>
#include <sstream>
#include "type_conversion.hpp"

namespace twilio {

class Twilio {
public:
        Twilio(std::string const& account_sid_in, 
               std::string const& auth_token_in)
                : account_sid(account_sid_in)
                , auth_token(auth_token_in)
        {}
        // Nothing in destructor
        ~Twilio() = default;

        bool send_message(
                std::string const& to_number,
                std::string const& from_number,
                std::string const& message_body,
                std::string& response,
                std::string const& picture_url = "",
                bool verbose = false
        );

private:
        // Account SID and Auth Token come from the Twilio console.
        // See: https://twilio.com/console for more.

        // Used for the username of the auth header
        std::string const account_sid;
        // Used for the password of the auth header
        std::string const auth_token;

        // Portably ignore curl response
        static size_t _null_write(char *, size_t, size_t, void *);
        // Write curl response to a stringstream
        static size_t _stream_write(char *, size_t, size_t, void *);
};
size_t Twilio::_null_write(
        char *ptr, 
        size_t size, 
        size_t nmemb, 
        void *userdata)
{
        return size*nmemb;
}

// Write curl response to a stringstream
size_t Twilio::_stream_write(
        char *ptr,
        size_t size,
        size_t nmemb,
        void *userdata) 
{
        size_t response_size = size * nmemb;
        std::stringstream *ss = (std::stringstream*)userdata;
        ss->write(ptr, response_size);
        return response_size;
}

// Method send_message:
//   Returns 'true' if the result of the eventual HTTP post to Twilio is status
//   code 200 or 201.  Either other status codes or errors in curl will cause
//   a false result.
//   Inputs:
//        - to_number: Where to send the MMS or SMS
//        - from_number: Number in your Twilio account to use as a sender.
//        - message_body: (Max: 1600 unicode characters) The body of the MMS 
//                        or SMS message which will be sent to the to_number.
//
//   Outputs:
//        - response: Either the curl error message or the Twilio response
//                if verbose.
//   Optional:
//        - picture_url: If picture URL is included, a MMS will be sent
//        - verbose: Whether to print all the responses
bool Twilio::send_message(
        std::string const& to_number,
        std::string const& from_number,
        std::string const& message_body,
        std::string& response,
        std::string const& picture_url,
        bool verbose)
{
        std::stringstream response_stream;
        std::u16string converted_message_body;

        // Assume UTF-8 input, convert to UCS-2 to check size
        // See: https://www.twilio.com/docs/api/rest/sending-messages for
        // information on Twilio body size limits.
        try {
                converted_message_body = utf8_to_ucs2(message_body);
        } catch(const std::range_error& e) {
                response = e.what();
                return false;
        }

        if (converted_message_body.size() > 1600) {
                response_stream << "Message body must have 1600 or fewer"
                        << " characters. Cannot send message with "
                        << converted_message_body.size() << " characters.";
                response = response_stream.str();
                return false;
        }

        CURL *curl;
        curl_global_init(CURL_GLOBAL_ALL);
        curl = curl_easy_init();

        // Percent encode special characters
        char *message_body_escaped = curl_easy_escape(
                curl, 
                message_body.c_str(), 
                0
        );


        std::stringstream url;
        std::string url_string;
        url << "https://api.twilio.com/2010-04-01/Accounts/" << account_sid
                << "/Messages";
        url_string = url.str();


        std::stringstream parameters;
        std::string parameter_string;
        parameters << "To=" << to_number << "&From=" << from_number 
                << "&Body=" << message_body_escaped;
        if (!picture_url.empty()) {
                parameters << "&MediaUrl=" << picture_url;
        }
        parameter_string = parameters.str();


        curl_easy_setopt(curl, CURLOPT_POST, 1);
        curl_easy_setopt(curl, CURLOPT_URL, url_string.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, parameter_string.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME, account_sid.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, auth_token.c_str());
        if (!verbose) {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _null_write);
        } else {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _stream_write);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_stream);
        }


        CURLcode res = curl_easy_perform(curl);
        curl_free(message_body_escaped);
        curl_easy_cleanup(curl);
        long http_code = 0;
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

        // Check for curl errors and Twilio failure status codes.
        if (res != CURLE_OK) {
                response = curl_easy_strerror(res);
                return false;
        } else if (http_code != 200 && http_code != 201) {
                response = response_stream.str();
                return false;
        } else {
                response = response_stream.str();
                return true;
        }
}
} // end namespace twilio