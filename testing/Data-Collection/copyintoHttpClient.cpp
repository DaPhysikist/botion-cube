int HTTPClient::sendRequestFile(const char *type, String bodyFirstHalf, File file, String bodySecondHalf) {
    int code;
    bool redirect = false;
    uint16_t redirectCount = 0;
    do {
        // Wipe out any existing headers from previous request
        for (size_t i = 0; i < _headerKeysCount; i++) {
            if (_currentHeaders[i].value.length() > 0) {
                _currentHeaders[i].value.clear();
            }
        }

        log_d("request type: '%s' redirCount: %d\n", type, redirectCount);

        // Connect to server
        if (!connect()) {
            return returnError(HTTPC_ERROR_CONNECTION_REFUSED);
        }

        uint8_t *bodyFHBytes = (uint8_t *)bodyFirstHalf.c_str();
        uint8_t *bodySHBytes = (uint8_t *)bodySecondHalf.c_str();
        size_t bodyFHSize = bodyFirstHalf.length();
        size_t bodySHSize = bodySecondHalf.length();
        size_t fileSize = file.size();
        size_t totalSize = bodyFHSize + bodySHSize + fileSize;

        if (bodyFHBytes && bodySHBytes && totalSize > 0) {
            addHeader(F("Content-Length"), String(totalSize));
        }

        // Add cookies to header, if present
        String cookie_string;
        if (generateCookieString(&cookie_string)) {
            addHeader("Cookie", cookie_string);
        }

        // Send Header
        if (!sendHeader(type)) {
            return returnError(HTTPC_ERROR_SEND_HEADER_FAILED);
        }

        // Send Payload if needed
        if (bodyFHBytes && bodySHBytes && totalSize > 0) {
            size_t sent_bytes_total = 0;

            // Send the first half of the body
            size_t sent_bytes_FH = 0;
            while (sent_bytes_FH < bodyFHSize) {
                size_t sent = _client->write(&bodyFHBytes[sent_bytes_FH], bodyFHSize - sent_bytes_FH);
                if (sent == 0) {
                    log_w("Failed to send chunk! Lets wait a bit");
                    delay(100);
                    sent = _client->write(&bodyFHBytes[sent_bytes_FH], bodyFHSize - sent_bytes_FH);
                    if (sent == 0) {
                        log_e("Failed to send chunk!");
                        break;
                    }
                }
                sent_bytes_FH += sent;
                sent_bytes_total += sent;
            }

            // Send the file data
            uint8_t buffer[512]; // Buffer to hold file data
            while (file.available()) {
                size_t bytesRead = file.read(buffer, sizeof(buffer));
                size_t sent = _client->write(buffer, bytesRead);
                if (sent == 0) {
                    log_w("Failed to send file chunk! Let's wait a bit");
                    delay(100);
                    sent = _client->write(buffer, bytesRead);
                    if (sent == 0) {
                        log_e("Failed to send file chunk!");
                        break;
                    }
                }
                sent_bytes_total += sent;
            }

            // Send the second half of the body
            size_t sent_bytes_SH = 0;
            while (sent_bytes_SH < bodySHSize) {
                size_t sent = _client->write(&bodySHBytes[sent_bytes_SH], bodySHSize - sent_bytes_SH);
                if (sent == 0) {
                    log_w("Failed to send chunk! Lets wait a bit");
                    delay(100);
                    sent = _client->write(&bodySHBytes[sent_bytes_SH], bodySHSize - sent_bytes_SH);
                    if (sent == 0) {
                        log_e("Failed to send chunk!");
                        break;
                    }
                }
                sent_bytes_SH += sent;
                sent_bytes_total += sent;
            }

            if (sent_bytes_total != totalSize) {
                return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
            }
        }

        code = handleHeaderResponse();
        log_d("sendRequest code=%d\n", code);

        // Handle redirections as stated in RFC document:
        // https://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
        //
        // Implementing HTTP_CODE_FOUND as redirection with GET method,
        // to follow most of existing user agent implementations.
        //
        redirect = false;
        if (_followRedirects != HTTPC_DISABLE_FOLLOW_REDIRECTS && 
            redirectCount < _redirectLimit && 
            _location.length() > 0) {
            switch (code) {
                // Redirecting using the same method
                case HTTP_CODE_MOVED_PERMANENTLY:
                case HTTP_CODE_TEMPORARY_REDIRECT: {
                    if (_followRedirects == HTTPC_FORCE_FOLLOW_REDIRECTS || 
                        !strcmp(type, "GET") || 
                        !strcmp(type, "HEAD")) {
                        redirectCount += 1;
                        log_d("following redirect (the same method): '%s' redirCount: %d\n", _location.c_str(), redirectCount);
                        if (!setURL(_location)) {
                            log_d("failed setting URL for redirection\n");
                            // No redirection
                            break;
                        }
                        // Redirect using the same request method and payload, different URL
                        redirect = true;
                    }
                    break;
                }
                // Redirecting with method dropped to GET or HEAD
                case HTTP_CODE_FOUND:
                case HTTP_CODE_SEE_OTHER: {
                    redirectCount += 1;
                    log_d("following redirect (dropped to GET/HEAD): '%s' redirCount: %d\n", _location.c_str(), redirectCount);
                    if (!setURL(_location)) {
                        log_d("failed setting URL for redirection\n");
                        // No redirection
                        break;
                    }
                    // Redirect after changing method to GET/HEAD and dropping payload
                    type = "GET";
                    bodyFHBytes = nullptr;
                    bodySHBytes = nullptr;
                    totalSize = 0;
                    redirect = true;
                    break;
                }
                default:
                    break;
            }
        }
    } while (redirect);

    // Handle Server Response (Header)
    return returnError(code);
}
