/*
 * N-DISE: NDN for Data Intensive Science Experiments
 * Author: Catalin Iordache <catalin.iordache@cern.ch>
 *
 * MIT License
 *
 * Copyright (c) 2021 California Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef NDNC_MGMT_CLIENT_HPP
#define NDNC_MGMT_CLIENT_HPP

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>

namespace ndnc::mgmt {
class Client {
  public:
    Client();
    ~Client();

    /**
     * @brief Create face on NDN-DPDK forwarder side
     *
     * @param id Local face id
     * @param dataroom Dataroom size
     * @param gqlserver GraphQL server address used in POST
     * @return true Successfully create a new face
     * @return false Unable to create a new face
     */
    bool createFace(int id, int dataroom, std::string gqlserver);

    /**
     * @brief Advertise NDN Name prefix on NDN-DPDK face
     *
     * @param prefix Name prefix
     * @return true Successfully advertise prefix on previously created face
     * @return false Unable to advertise prefix on previously created face
     */
    bool insertFibEntry(const std::string prefix);

    /**
     * @brief Delete face on NDN-DPDK forwarder side
     *
     * @return true Successfully deleted previously created face
     * @return false Unable to deleted previously created face
     */
    bool deleteFace();

    std::string getSocketPath() {
        return socketPath_;
    }

    std::string getFaceID() {
        return faceID_;
    }

    std::string getFibEntryID() {
        return fibEntryID_;
    }

  private:
    bool deleteID(std::string id);
    void logResponseError(const nlohmann::json response);

  private:
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                                void *userdata) {
        ((std::string *)userdata)->append((char *)ptr, size * nmemb);
        return size * nmemb;
    }

    static CURLcode doOperation(const nlohmann::json request,
                                nlohmann::json &response,
                                std::string gqlserver) {
        curl_global_init(CURL_GLOBAL_ALL);
        auto curl = curl_easy_init();
        if (curl == nullptr) {
            return CURLE_FAILED_INIT;
        }

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_URL, gqlserver.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        auto postFields = strdup(request.dump().c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields);

        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);

        std::string data;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

        auto code = curl_easy_perform(curl);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(postFields);
        curl_global_cleanup();

        if (data.empty()) {
            return CURLE_RECV_ERROR;
        }

        response = nlohmann::json::parse(data);
        return code;
    }

  private:
    std::string socketPath_;
    std::string gqlserver_;
    std::string faceID_;
    std::string fibEntryID_;
};
}; // namespace ndnc::mgmt

#endif // NDNC_MGMT_CLIENT_HPP
