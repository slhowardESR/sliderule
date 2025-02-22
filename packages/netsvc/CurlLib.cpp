/*
 * Copyright (c) 2021, University of Washington
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the University of Washington nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF WASHINGTON AND CONTRIBUTORS
 * “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF WASHINGTON OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************************************
 * INCLUDES
 ******************************************************************************/


#include "CurlLib.h"
#include "core.h"

#include <curl/curl.h>

/******************************************************************************
 * cURL LIBRARY CLASS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * init
 *----------------------------------------------------------------------------*/
void CurlLib::init (void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/*----------------------------------------------------------------------------
 * deinit
 *----------------------------------------------------------------------------*/
void CurlLib::deinit (void)
{
    curl_global_cleanup();
}

/*----------------------------------------------------------------------------
 * request
 *----------------------------------------------------------------------------*/
long CurlLib::request (EndpointObject::verb_t verb, const char* url, const char* data, const char** response, int* size, bool verify_peer, bool verify_hostname, List<string*>* headers)
{
    long http_code = 0;
    CURL* curl = NULL;

    /* Initialize Request */
    data_t rqst;
    if(data)
    {
        rqst.data = (char*)data;
        rqst.size = StringLib::size(data);
    }
    else
    {
        rqst.data = NULL;
        rqst.size = 0;
    }

    /* Initialize Response */
    List<data_t> rsps_set(EXPECTED_RESPONSE_SEGMENTS);
    if(response) *response = NULL;
    if(size) *size = 0;

    /* Initialize cURL */
    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, CURL_MAX_READ_SIZE); // TODO: test out performance
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT); // seconds
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, DATA_TIMEOUT); // seconds
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlLib::writeData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsps_set);
        curl_easy_setopt(curl, CURLOPT_NETRC, 1L);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, ".cookies");
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR, ".cookies");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        if(verb == EndpointObject::GET && rqst.size > 0)
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, rqst.data);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)rqst.size);
        }
        else if(verb == EndpointObject::POST)
        {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, CurlLib::readData);
            curl_easy_setopt(curl, CURLOPT_READDATA, &rqst);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)rqst.size);
        }
        else if(verb == EndpointObject::PUT)
        {
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, CurlLib::readData);
            curl_easy_setopt(curl, CURLOPT_READDATA, &rqst);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)rqst.size);
        }

        /* Add Headers */
        struct curl_slist* hdr_slist = NULL;
        if(headers && headers->length() > 0)
        {
            for(int i = 0; i < headers->length(); i++)
            {
                hdr_slist = curl_slist_append(hdr_slist, headers->get(i)->c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_slist);
        }

        /*
        * If you want to connect to a site who isn't using a certificate that is
        * signed by one of the certs in the CA bundle you have, you can skip the
        * verification of the server's certificate. This makes the connection
        * A LOT LESS SECURE.
        *
        * If you have a CA cert for the server stored someplace else than in the
        * default bundle, then the CURLOPT_CAPATH option might come handy for
        * you.
        */
        if(!verify_peer)
        {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        }

        /*
        * If the site you're connecting to uses a different host name that what
        * they have mentioned in their server certificate's commonName (or
        * subjectAltName) fields, libcurl will refuse to connect. You can skip
        * this check, but this will make the connection less secure.
        */
        if(!verify_hostname)
        {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        /* Perform the request, res will get the return code */
        CURLcode res = curl_easy_perform(curl);

        /* Check for Success */
        if(res == CURLE_OK)
        {
            /* Get HTTP Code */
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            /* Get Response */
            CurlLib::combineResponse(&rsps_set, response, size);
        }
        else
        {
            /* Unable to Perform cURL Call */
            FString error_msg("%s", curl_easy_strerror(res));
            if(response) *response = error_msg.c_str(true);
            http_code = EndpointObject::Service_Unavailable;
        }

        /* Always Cleanup */
        curl_easy_cleanup(curl);
        curl_slist_free_all(hdr_slist);
    }

    /* Return HTTP Code */
    return http_code;
}

/*----------------------------------------------------------------------------
 * postAsStream
 *----------------------------------------------------------------------------*/
long CurlLib::postAsStream (const char* url, const char* data, Publisher* outq, bool with_terminator)
{
    long http_code = 0;
    CURL* curl = NULL;

    /* Initialize Request */
    data_t rqst;
    rqst.data = (char*)data;
    rqst.size = StringLib::size(data);

    /* Initialize cURL */
    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, CURL_MAX_READ_SIZE); // TODO: test out performance
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT); // seconds
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, DATA_TIMEOUT); // seconds
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, CurlLib::readData);
        curl_easy_setopt(curl, CURLOPT_READDATA, &rqst);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)rqst.size);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlLib::postData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, outq);

        /* Perform the request, res will get the return code */
        CURLcode res = curl_easy_perform(curl);

        /* Check for Success */
        if(res == CURLE_OK)
        {
            /* Get HTTP Code */
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }
        else
        {
            /* Unable to Perform cURL Call */
            mlog(ERROR, "Unable to perform cRUL call on %s: %s", url, curl_easy_strerror(res));
            http_code = EndpointObject::Service_Unavailable;
        }

        /* Always Cleanup */
        curl_easy_cleanup(curl);
    }

    /* Terminate Stream */
    if(with_terminator)
    {
        outq->postCopy("", 0);
    }

    /* Return HTTP Code */
    return http_code;
}

/*----------------------------------------------------------------------------
 * postAsRecord
 *----------------------------------------------------------------------------*/
long CurlLib::postAsRecord (const char* url, const char* data, Publisher* outq, bool with_terminator, int timeout, bool* active)
{
    long http_code = 0;
    CURL* curl = NULL;

    /* Initialize Request */
    data_t rqst;
    rqst.data = (char*)data;
    rqst.size = StringLib::size(data);

    /* Initialize Response (only used if as_record is true) */
    parser_t parser = {
        .hdr_buf = {0, 0, 0, 0, 0, 0, 0, 0},
        .hdr_index = 0,
        .rec_size = 0,
        .rec_index = 0,
        .rec_buf = NULL,
        .outq = outq,
        .url = url,
        .active = active
    };

    /* Initialize cURL */
    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, CURL_MAX_READ_SIZE); // TODO: test out performance
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT); // seconds
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout); // seconds
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, CurlLib::readData);
        curl_easy_setopt(curl, CURLOPT_READDATA, &rqst);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)rqst.size);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlLib::postRecords);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &parser);

        /* Perform the request, res will get the return code */
        CURLcode res = curl_easy_perform(curl);

        /* Check for Success */
        if(res == CURLE_OK)
        {
            /* Get HTTP Code */
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }
        else
        {
            /* Unable to Perform cURL Call */
            mlog(ERROR, "Unable to perform cRUL call on %s: %s", url, curl_easy_strerror(res));
            http_code = EndpointObject::Service_Unavailable;
        }

        /* Always Cleanup */
        curl_easy_cleanup(curl);

        /* Free Left-Over Response (if present) */
        if(parser.rec_size > 0)
        {
            delete [] parser.rec_buf;
        }
    }

    /* Terminate Stream */
    if(with_terminator)
    {
        outq->postCopy("", 0);
    }

    /* Return HTTP Code */
    return http_code;
}

/*----------------------------------------------------------------------------
 * getHeaders
 *----------------------------------------------------------------------------*/
int CurlLib::getHeaders (lua_State* L, int index, List<string*>& header_list)
{
    int num_hdrs = 0;

    /* Must be table of strings */
    if((lua_gettop(L) >= index) && lua_istable(L, index))
    {
        /* Iterate through each item in table */
        int num_strings = lua_rawlen(L, index);
        for(int i = 0; i < num_strings; i++)
        {
            /* Get item */
            lua_rawgeti(L, index, i+1);
            if(lua_isstring(L, -1))
            {
                string* header = new string(LuaObject::getLuaString(L, -1));
                header_list.add(header);
                num_hdrs++;
            }

            /* Clean up stack */
            lua_pop(L, 1);
        }
    }

    return num_hdrs;
}

/*----------------------------------------------------------------------------
 * luaGet
 *----------------------------------------------------------------------------*/
int CurlLib::luaGet (lua_State* L)
{
    bool status = false;
    List<string*> header_list(EXPECTED_MAX_HEADERS);

    try
    {
        /* Get Parameters */
        const char* url             = LuaObject::getLuaString(L, 1);
        const char* data            = LuaObject::getLuaString(L, 2, true, NULL);
        int         num_hdrs        = CurlLib::getHeaders(L, 3, header_list); (void)num_hdrs;
        bool        verify_peer     = LuaObject::getLuaBoolean(L, 4, true, false);
        bool        verify_hostname = LuaObject::getLuaBoolean(L, 5, true, false);

        /* Perform Request */
        const char* response = NULL;
        int size = 0;
        long http_code = CurlLib::request(EndpointObject::GET, url, data, &response, &size, verify_peer, verify_hostname, &header_list);
        if(response)
        {
            status = (http_code >= 200 && http_code < 300);
            lua_pushlstring(L, response, size);
            delete [] response;
        }
        else
        {
            lua_pushnil(L);
        }
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error performing netsvc GET: %s", e.what());
        lua_pushnil(L);
    }

    /* Return Status */
    lua_pushboolean(L, status);
    return 2;
}

/*----------------------------------------------------------------------------
 * luaPut
 *----------------------------------------------------------------------------*/
int CurlLib::luaPut (lua_State* L)
{
    bool status = false;
    List<string*> header_list(EXPECTED_MAX_HEADERS);

    try
    {
        /* Get Parameters */
        const char* url             = LuaObject::getLuaString(L, 1);
        const char* data            = LuaObject::getLuaString(L, 2, true, NULL);
        int         num_hdrs        = CurlLib::getHeaders(L, 3, header_list); (void)num_hdrs;
        bool        verify_peer     = LuaObject::getLuaBoolean(L, 4, true, false);
        bool        verify_hostname = LuaObject::getLuaBoolean(L, 5, true, false);

        /* Perform Request */
        const char* response = NULL;
        int size = 0;
        long http_code = CurlLib::request(EndpointObject::PUT, url, data, &response, &size, verify_peer, verify_hostname, &header_list);
        if(response)
        {
            status = (http_code >= 200 && http_code < 300);
            lua_pushlstring(L, response, size);
            delete [] response;
        }
        else
        {
            lua_pushnil(L);
        }
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error performing netsvc GET: %s", e.what());
        lua_pushnil(L);
    }

    /* Return Status */
    lua_pushboolean(L, status);
    return 2;
}

/*----------------------------------------------------------------------------
 * luaPost
 *----------------------------------------------------------------------------*/
int CurlLib::luaPost (lua_State* L)
{
    bool status = false;
    List<string*> header_list(EXPECTED_MAX_HEADERS);

    try
    {
        /* Get Parameters */
        const char* url         = LuaObject::getLuaString(L, 1);
        const char* data        = LuaObject::getLuaString(L, 2, true, "{}");
        int         num_hdrs    = CurlLib::getHeaders(L, 3, header_list); (void)num_hdrs;

        /* Perform Request */
        const char* response = NULL;
        int size = 0;
        long http_code = CurlLib::request(EndpointObject::POST, url, data, &response, &size, false, false, &header_list);
        if(response)
        {
            status = (http_code >= 200 && http_code < 300);
            lua_pushlstring(L, response, size);
            delete [] response;
        }
        else
        {
            lua_pushnil(L);
        }
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error performing netsvc POST: %s", e.what());
        lua_pushnil(L);
    }

    /* Return Status */
    lua_pushboolean(L, status);
    return 2;
}

/*----------------------------------------------------------------------------
 * CurlLib::combineResponse
 *----------------------------------------------------------------------------*/
void CurlLib::combineResponse (List<data_t>* rsps_set, const char** response, int* size)
{
    /* Get Total Response Size */
    int total_rsps_size = 0;
    for(int i = 0; i < rsps_set->length(); i++)
    {
        total_rsps_size += (*rsps_set)[i].size;
    }

    /* Allocate and Populate Total Response */
    int total_rsps_index = 0;
    char* total_rsps = new char [total_rsps_size + 1];
    for(int i = 0; i < rsps_set->length(); i++)
    {
        memcpy(&total_rsps[total_rsps_index], (*rsps_set)[i].data, (*rsps_set)[i].size);
        total_rsps_index += (*rsps_set)[i].size;
        delete [] (*rsps_set)[i].data;
    }
    total_rsps[total_rsps_index] = '\0';

    /* Return Response */
    if(size) *size = total_rsps_index;
    if(response) *response = total_rsps;
    else delete [] total_rsps;
}

/*----------------------------------------------------------------------------
 * CurlLib::postRecords
 *----------------------------------------------------------------------------*/
size_t CurlLib::postRecords(void *buffer, size_t size, size_t nmemb, void *userp)
{
    parser_t* parser = static_cast<parser_t*>(userp);
    int32_t bytes_to_process = static_cast<int32_t>(size * nmemb);
    uint8_t* input_data = static_cast<uint8_t*>(buffer);
    uint32_t input_index = 0;

    while(bytes_to_process > 0)
    {
        if(parser->rec_size == 0) // record header
        {
            int32_t hdr_bytes_needed = RECOBJ_HDR_SIZE - parser->hdr_index;
            int32_t hdr_bytes_to_process = MIN(hdr_bytes_needed, bytes_to_process);
            for(int i = 0; i < hdr_bytes_to_process; i++) parser->hdr_buf[parser->hdr_index++] = input_data[input_index++];
            bytes_to_process -= hdr_bytes_to_process;

            // check header complete
            if(parser->hdr_index == RECOBJ_HDR_SIZE)
            {
                // parser header
                RecordObject::rec_hdr_t* rec_hdr = reinterpret_cast<RecordObject::rec_hdr_t*>(parser->hdr_buf);
                uint16_t version = OsApi::swaps(rec_hdr->version);
                uint16_t type_size = OsApi::swaps(rec_hdr->type_size);
                uint32_t data_size = OsApi::swapl(rec_hdr->data_size);
                if(version != RecordObject::RECORD_FORMAT_VERSION)
                {
                    mlog(CRITICAL, "Invalid record version in response from %s: %d", parser->url, version);
                    return 0;
                }

                // allocate record and copy header
                parser->rec_size = sizeof(RecordObject::rec_hdr_t) + type_size + data_size;
                parser->rec_buf = new uint8_t [parser->rec_size];
                memcpy(&parser->rec_buf[0], &parser->hdr_buf[0], sizeof(RecordObject::rec_hdr_t));
                parser->rec_index = sizeof(RecordObject::rec_hdr_t);

                // reset header
                parser->hdr_index = 0;
            }
        }
        else // record body
        {
            int32_t rec_bytes_needed = parser->rec_size - parser->rec_index;
            int32_t rec_bytes_to_process = MIN(rec_bytes_needed, bytes_to_process);
            memcpy(&parser->rec_buf[parser->rec_index], &input_data[input_index], rec_bytes_to_process);
            parser->rec_index += rec_bytes_to_process;
            input_index += rec_bytes_to_process;
            bytes_to_process -= rec_bytes_to_process;

            // check body complete
            if(parser->rec_index == parser->rec_size)
            {
                // post record
                int post_status = MsgQ::STATE_TIMEOUT;
                while((!parser->active || *parser->active) && post_status == MsgQ::STATE_TIMEOUT)
                {
                    post_status = parser->outq->postRef(parser->rec_buf, parser->rec_size, SYS_TIMEOUT);
                    if(post_status < 0)
                    {
                        // handle post errors
                        delete [] parser->rec_buf;
                        mlog(CRITICAL, "Failed to post response for %s: %d", parser->url, post_status);
                    }
                }

                // reset body
                parser->rec_index = 0;
                parser->rec_size = 0;
            }
        }
    }

    return size * nmemb;
}

/*----------------------------------------------------------------------------
 * CurlLib::postData
 *----------------------------------------------------------------------------*/
size_t CurlLib::postData(void *buffer, size_t size, size_t nmemb, void *userp)
{
    Publisher* outq = static_cast<Publisher*>(userp);
    return outq->postCopy(buffer, size * nmemb);
}

/*----------------------------------------------------------------------------
 * CurlLib::writeData
 *----------------------------------------------------------------------------*/
size_t CurlLib::writeData(void *buffer, size_t size, size_t nmemb, void *userp)
{
    List<data_t>* rsps_set = static_cast<List<data_t>*>(userp);

    data_t rsps;
    rsps.size = size * nmemb;
    rsps.data = new char [rsps.size + 1];

    memcpy(rsps.data, buffer, rsps.size);
    rsps.data[rsps.size] = '\0';

    rsps_set->add(rsps);

    return rsps.size;
}

/*----------------------------------------------------------------------------
 * CurlLib::readData
 *----------------------------------------------------------------------------*/
size_t CurlLib::readData(void* buffer, size_t size, size_t nmemb, void *userp)
{
    data_t* rqst = static_cast<data_t*>(userp);

    size_t buffer_size = size * nmemb;
    size_t bytes_to_copy = rqst->size;
    if(bytes_to_copy > buffer_size) bytes_to_copy = buffer_size;

    if(bytes_to_copy)
    {
        memcpy(buffer, rqst->data, bytes_to_copy);
        rqst->data += bytes_to_copy;
        rqst->size -= bytes_to_copy;
    }

    return bytes_to_copy;
}

