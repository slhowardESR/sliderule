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

#include "LuaEndpoint.h"
#include "core.h"

/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

const char* LuaEndpoint::LUA_META_NAME = "LuaEndpoint";
const struct luaL_Reg LuaEndpoint::LUA_META_TABLE[] = {
    {"auth",        luaAuth},
    {NULL,          NULL}
};

const char* LuaEndpoint::EndpointExceptionRecType = "exceptrec";
const RecordObject::fieldDef_t LuaEndpoint::EndpointExceptionRecDef[] = {
    {"code",        RecordObject::INT32,    offsetof(response_exception_t, code),   1,                       NULL, NATIVE_FLAGS},
    {"level",       RecordObject::INT32,    offsetof(response_exception_t, level),  1,                       NULL, NATIVE_FLAGS},
    {"text",        RecordObject::STRING,   offsetof(response_exception_t, text),   MAX_EXCEPTION_TEXT_SIZE, NULL, NATIVE_FLAGS}
};

const double LuaEndpoint::DEFAULT_NORMAL_REQUEST_MEMORY_THRESHOLD = 1.0;
const double LuaEndpoint::DEFAULT_STREAM_REQUEST_MEMORY_THRESHOLD = 1.0;

FString LuaEndpoint::serverHead("sliderule/%s", LIBID);

const char* LuaEndpoint::LUA_RESPONSE_QUEUE = "rspq";
const char* LuaEndpoint::LUA_REQUEST_ID = "rqstid";


/******************************************************************************
 * AUTHENTICATOR SUBCLASS
 ******************************************************************************/

const char* LuaEndpoint::Authenticator::OBJECT_TYPE = "Authenticator";
const char* LuaEndpoint::Authenticator::LUA_META_NAME = "Authenticator";
const struct luaL_Reg LuaEndpoint::Authenticator::LUA_META_TABLE[] = {
    {NULL,          NULL}
};

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
LuaEndpoint::Authenticator::Authenticator(lua_State* L):
    LuaObject(L, OBJECT_TYPE, LUA_META_NAME, LUA_META_TABLE)
{
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
LuaEndpoint::Authenticator::~Authenticator(void)
{
}

/******************************************************************************
 * PUBLIC METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * init
 *----------------------------------------------------------------------------*/
void LuaEndpoint::init (void)
{
    RECDEF(EndpointExceptionRecType, EndpointExceptionRecDef, sizeof(response_exception_t), "code");
}

/*----------------------------------------------------------------------------
 * luaCreate - endpoint([<normal memory threshold>], [<stream memory threshold>])
 *----------------------------------------------------------------------------*/
int LuaEndpoint::luaCreate (lua_State* L)
{
    try
    {
        /* Get Parameters */
        double normal_mem_thresh = getLuaFloat(L, 1, true, DEFAULT_NORMAL_REQUEST_MEMORY_THRESHOLD);
        double stream_mem_thresh = getLuaFloat(L, 2, true, DEFAULT_STREAM_REQUEST_MEMORY_THRESHOLD);
        event_level_t lvl = (event_level_t)getLuaInteger(L, 3, true, INFO);

        /* Create Lua Endpoint */
        return createLuaObject(L, new LuaEndpoint(L, normal_mem_thresh, stream_mem_thresh, lvl));
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error creating %s: %s", LUA_META_NAME, e.what());
        return returnLuaStatus(L, false);
    }
}

/*----------------------------------------------------------------------------
 * generateExceptionStatus
 *----------------------------------------------------------------------------*/
void LuaEndpoint::generateExceptionStatus (int code, event_level_t level, Publisher* outq, bool* active, const char* errmsg, ...)
{
    /* Build Error Message */
    char error_buf[MAX_EXCEPTION_TEXT_SIZE];
    va_list args;
    va_start(args, errmsg);
    int vlen = vsnprintf(error_buf, MAX_EXCEPTION_TEXT_SIZE - 1, errmsg, args);
    int attr_size = MAX(MIN(vlen + 1, MAX_EXCEPTION_TEXT_SIZE), 1);
    error_buf[attr_size - 1] = '\0';
    va_end(args);

    /* Post Endpoint Exception Record */
    RecordObject record(EndpointExceptionRecType);
    response_exception_t* exception = (response_exception_t*)record.getRecordData();
    exception->code = code;
    exception->level = (int32_t)level;
    StringLib::format(exception->text, MAX_EXCEPTION_TEXT_SIZE, "%s", error_buf);
    record.post(outq, 0, active);
}

/******************************************************************************
 * PROTECTED METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
LuaEndpoint::LuaEndpoint(lua_State* L, double normal_mem_thresh, double stream_mem_thresh, event_level_t lvl):
    EndpointObject(L, LUA_META_NAME, LUA_META_TABLE),
    normalRequestMemoryThreshold(normal_mem_thresh),
    streamRequestMemoryThreshold(stream_mem_thresh),
    logLevel(lvl),
    authenticator(NULL)
{
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
LuaEndpoint::~LuaEndpoint(void)
{
}

/*----------------------------------------------------------------------------
 * requestThread
 *----------------------------------------------------------------------------*/
void* LuaEndpoint::requestThread (void* parm)
{
    EndpointObject::info_t* info = (EndpointObject::info_t*)parm;
    EndpointObject::Request* request = info->request;
    LuaEndpoint* lua_endpoint = dynamic_cast<LuaEndpoint*>(info->endpoint);
    double start = TimeLib::latchtime();

    /* Get Request Script */
    const char* script_pathname = LuaEngine::sanitize(request->resource);

    /* Start Trace */
    uint32_t trace_id = start_trace(INFO, request->trace_id, "lua_endpoint", "{\"verb\":\"%s\", \"resource\":\"%s\"}", verb2str(request->verb), request->resource);

    /* Log Request */
    mlog(lua_endpoint->logLevel, "%s %s: %s", verb2str(request->verb), request->resource, request->body);

    /* Create Publisher */
    Publisher* rspq = new Publisher(request->id);

    /* Check Authentication */
    bool authorized = false;
    if(lua_endpoint->authenticator)
    {
        char* bearer_token = NULL;

        /* Extract Bearer Token */
        string* auth_hdr;
        if(request->headers.find("Authorization", &auth_hdr))
        {
            bearer_token = StringLib::find(auth_hdr->c_str(), ' ');
            if(bearer_token) bearer_token += 1;
        }

        /* Validate Bearer Token */
        authorized = lua_endpoint->authenticator->isValid(bearer_token);
    }
    else // no authentication required
    {
        authorized = true;
    }

    /* Dispatch Handle Request */
    if(authorized)
    {
        switch(request->verb)
        {
            case GET:   lua_endpoint->normalResponse(script_pathname, request, rspq, trace_id); break;
            case POST:  lua_endpoint->streamResponse(script_pathname, request, rspq, trace_id); break;
            default:    break;
        }
    }
    else
    {
        char header[MAX_HDR_SIZE];
        int header_length = buildheader(header, Unauthorized);
        rspq->postCopy(header, header_length);
    }

    /* End Response */
    rspq->postCopy("", 0);

    /* Generate Metric for Endpoint */
    double duration = TimeLib::latchtime() - start;
    gauge_metric(INFO, request->resource, duration);

    /* Clean Up */
    delete rspq;
    delete [] script_pathname;
    delete request;
    delete info;

    /* Stop Trace */
    stop_trace(INFO, trace_id);
    
    /* Return */
    return NULL;
}

/*----------------------------------------------------------------------------
 * handleRequest
 *----------------------------------------------------------------------------*/
EndpointObject::rsptype_t LuaEndpoint::handleRequest (Request* request)
{
    EndpointObject::info_t* info = new EndpointObject::info_t;
    info->endpoint = this;
    info->request = request;

    /* Start Thread */
    Thread pid(requestThread, info, false);

    /* Return Response Type */
    if(request->verb == POST) return STREAMING;
    return NORMAL;
}

/*----------------------------------------------------------------------------
 * normalResponse
 *----------------------------------------------------------------------------*/
void LuaEndpoint::normalResponse (const char* scriptpath, Request* request, Publisher* rspq, uint32_t trace_id) const
{
    char header[MAX_HDR_SIZE];
    double mem;

    LuaEngine* engine = NULL;

    /* Check Memory */
    if( (normalRequestMemoryThreshold >= 1.0) ||
        ((mem = OsApi::memusage()) < normalRequestMemoryThreshold) )
    {
        /* Launch Engine */
        engine = new LuaEngine(scriptpath, (const char*)request->body, trace_id, NULL, true);
        bool status = engine->executeEngine(MAX_RESPONSE_TIME_MS);

        /* Send Response */
        if(status)
        {
            const char* result = engine->getResult();
            if(result)
            {
                int result_length = StringLib::size(result);
                int header_length = buildheader(header, OK, "text/plain", result_length, NULL, serverHead.c_str());
                rspq->postCopy(header, header_length);
                rspq->postCopy(result, result_length);
            }
            else
            {
                int header_length = buildheader(header, Not_Found);
                rspq->postCopy(header, header_length);
            }
        }
        else
        {
            mlog(ERROR, "Failed to execute request: %s", scriptpath);
            int header_length = buildheader(header, Internal_Server_Error);
            rspq->postCopy(header, header_length);
        }
    }
    else
    {
        mlog(CRITICAL, "Memory (%d%%) exceeded threshold, not performing request: %s", (int)(mem * 100.0), scriptpath);
        int header_length = buildheader(header, Service_Unavailable);
        rspq->postCopy(header, header_length);
    }

    /* Clean Up */
    delete engine;
}

/*----------------------------------------------------------------------------
 * streamResponse
 *----------------------------------------------------------------------------*/
void LuaEndpoint::streamResponse (const char* scriptpath, Request* request, Publisher* rspq, uint32_t trace_id) const
{
    char header[MAX_HDR_SIZE];
    double mem;

    LuaEngine* engine = NULL;

    /* Check Memory */
    if( (streamRequestMemoryThreshold >= 1.0) ||
        ((mem = OsApi::memusage()) < streamRequestMemoryThreshold) )
    {
        /* Send Header */
        int header_length = buildheader(header, OK, "application/octet-stream", 0, "chunked", serverHead.c_str());
        rspq->postCopy(header, header_length);

        /* Create Engine */
        engine = new LuaEngine(scriptpath, (const char*)request->body, trace_id, NULL, true);

        /* Supply Global Variables to Script */
        engine->setString(LUA_RESPONSE_QUEUE, rspq->getName());
        engine->setString(LUA_REQUEST_ID, request->id);

        /* Execute Engine
        *  The call to execute the script blocks on completion of the script. The lua state context
        *  is locked and cannot be accessed until the script completes */
        engine->executeEngine(IO_PEND);
    }
    else
    {
        mlog(CRITICAL, "Memory (%d%%) exceeded threshold, not performing request: %s", (int)(mem * 100.0), scriptpath);
        int header_length = buildheader(header, Service_Unavailable);
        rspq->postCopy(header, header_length);
    }

    /* Clean Up */
    delete engine;
}

/*----------------------------------------------------------------------------
 * luaAuth - :auth(<authentication object>)
 *
 * Note: NOT thread safe, must be called prior to attaching endpoint to server
 *----------------------------------------------------------------------------*/
int LuaEndpoint::luaAuth (lua_State* L)
{
    bool status = false;

    try
    {
        /* Get Self */
        LuaEndpoint* lua_obj = dynamic_cast<LuaEndpoint*>(getLuaSelf(L, 1));

        /* Get Authenticator */
        Authenticator* auth = dynamic_cast<Authenticator*>(getLuaObject(L, 2, LuaEndpoint::Authenticator::OBJECT_TYPE));

        /* Set Authenticator */
        lua_obj->authenticator = auth;

        /* Set return Status */
        status = true;
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error setting authenticator: %s", e.what());
    }

    /* Return Status */
    return returnLuaStatus(L, status);
}
