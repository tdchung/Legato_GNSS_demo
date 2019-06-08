//--------------------------------------------------------------------------------------------------
/** @file httpGet.c
 *
 * Demonstrates opening a data connection and libcurl usage
 *
 * Copyright (C) Sierra Wireless Inc.
 */

#include "legato.h"
#include "le_data_interface.h"
#include "le_timer.h"
#include <curl/curl.h>
#include <time.h>

#include "legato.h"
#include "interfaces.h"

#define DATA_TIMEOUT_SECS   10
#define SSL_ERROR_HELP      "Make sure your system date is set correctly (e.g. `date -s '2016-7-7'`)"
#define SSL_ERROR_HELP_2    "You can check the minimum date for this SSL cert to work using: `openssl s_client -connect httpbin.org:443 2>/dev/null | openssl x509 -noout -dates`"

#define MAX_RECEIVE_LEN     1024

#define CHANNEL_ID "XXXXXX"
#define CHANNEL_WRITE_API "XXXXXXXXXXXXXXXX"
#define CHANNEL_READ_API  "XXXXXXXXXXXXXXXX"

// string struct for curl get library
typedef struct
{
    char *ptr;
    size_t len;
} curlStr_t;

static const char* write_api_key = "XXXXXXXXXXXXXXXX";
// const char* thingspeak_server = "";

// static const char * Url = "http://httpbin.org/get";

static le_data_RequestObjRef_t ConnectionRef;
static bool WaitingForConnection;
static le_timer_Ref_t dataTimeoutTimerPtr = NULL;

static le_gnss_PositionHandlerRef_t PositionHandlerRef = NULL;

// Location
static int32_t latitude = 0;
static int32_t longitude = 0;
static int32_t altitude = 0;
static int32_t hAccuracy = 0;
static int32_t magneticDeviation = 0;

// Time parameters
static uint16_t hours = 0;
static uint16_t minutes = 0;
static uint16_t seconds = 0;
static uint16_t milliseconds = 0;
static char time_string[30] = {0};

//--------------------------------------------------------------------------------------------------
/**
 * Callback for printing the response of a succesful request
 */
//--------------------------------------------------------------------------------------------------
// static size_t PrintCallback
// (
//     void *bufferPtr,
//     size_t size,
//     size_t nbMember,
//     void *userData // unused, but can be set with CURLOPT_WRITEDATA
// )
// {
//     printf("Succesfully received data:\n");
//     fwrite(bufferPtr, size, nbMember, stdout);
//     return size * nbMember;
// }

//--------------------------------------------------------------------------------------------------
/**
 * Callback for the timeout timer
 */
//--------------------------------------------------------------------------------------------------

static void TimeoutHandler
(
    le_timer_Ref_t timerRef
)
{
    if (WaitingForConnection)
    {
        LE_ERROR("Couldn't establish connection after " STRINGIZE(TIMEOUT_SECS) " seconds");
        le_timer_Stop(dataTimeoutTimerPtr);
        // exit(EXIT_FAILURE);
    }
}

// initial setting for struct string curl library
void initString(curlStr_t *str)
{
    str->len = 0;
    str->ptr = malloc(str->len+1);
    if (str->ptr == NULL) {
        fprintf(stderr, "malloc() failed\n");
        exit(EXIT_FAILURE);
    }
    str->ptr[0] = '\0';
}

// callback function curl library
size_t writeFunction(void *ptr, size_t size, size_t nmemb, curlStr_t *str)
{
    size_t new_len = str->len + size*nmemb;
    str->ptr = realloc(str->ptr, new_len+1);
    if (str->ptr == NULL) {
        fprintf(stderr, "realloc() failed\n");
        exit(EXIT_FAILURE);
    }
    memcpy(str->ptr+str->len, ptr, size*nmemb);
    str->ptr[new_len] = '\0';
    str->len = new_len;

    return size*nmemb;
}

// function execute HTTP get with url
bool getUrl
(
    char *url,    // IN:  The url using for get
    char *dataout  // OUT: The variable will have data will be received
)
{
    CURL *curl;
    CURLcode res;
    bool result = false;

    curl = curl_easy_init();
    if (curl) {
        curlStr_t str;
        initString(&str);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            LE_ERROR("curl_easy_perform() failed: %s", curl_easy_strerror(res));
            if (res == CURLE_SSL_CACERT)
            {
                LE_ERROR(SSL_ERROR_HELP);
                LE_ERROR(SSL_ERROR_HELP_2);
            }
        }
        else
        {
            strcpy(dataout, str.ptr);
            free(str.ptr);
            result = true;
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback for when the connection state changes
 */
//--------------------------------------------------------------------------------------------------

static void ConnectionStateHandler
(
    const char *intfName,
    bool isConnected,
    void *contextPtr
)
{
    char dataout[MAX_RECEIVE_LEN] = {0};
    char url[MAX_RECEIVE_LEN] = {0};

    if (isConnected)
    {
        // stop timeout timer as it already connected
        le_timer_Stop(dataTimeoutTimerPtr);

        WaitingForConnection = false;
        LE_INFO("Interface %s connected.", intfName);
        // sleep(5);
        if (getUrl("http://httpbin.org/get", dataout))
        {
            LE_INFO(dataout);
        }

        sprintf(url, "https://api.thingspeak.com/update?api_key=%s&field1=%f&field2=%f&field3=%s",
               write_api_key, (float)longitude/1000000.0, (float)latitude/1000000.0, time_string);
        LE_INFO("---URL---: %s", url);
        if (getUrl(url, dataout))
        {
            LE_INFO(dataout);
        }
        le_data_Release(ConnectionRef);
    }
    else
    {
        LE_INFO("Interface %s disconnected.", intfName);

    }
}

static void TimerHandler
(
    le_timer_Ref_t timerRef
)
{
    WaitingForConnection = true;
    le_timer_Start(dataTimeoutTimerPtr);
    le_gnss_ConnectService();
    le_gnss_ForceHotRestart();
    sleep(10);
    LE_INFO("Requesting connection...");
    LE_INFO("---------Long, Lat is updated:---------");
    LE_INFO("--------  %f ------ %f ----------", (float)longitude/1000000.0, (float)latitude/1000000.0);
    ConnectionRef = le_data_Request();
}

//--------------------------------------------------------------------------------------------------
/**
 * Get Position info and different parameter values.
 */
//--------------------------------------------------------------------------------------------------
static void getPositionInfo(le_gnss_SampleRef_t positionSampleRef)
{
    le_result_t result;
    // Get Location
    result = le_gnss_GetLocation(positionSampleRef, &latitude, &longitude, &hAccuracy);
    if ( (LE_OK == result) || (LE_OUT_OF_RANGE == result) )
    {
        LE_INFO("INFO. Latitude %f\n", (float)latitude/1000000.0);
        LE_INFO("INFO. Longitude %f\n", (float)longitude/1000000.0);
        LE_INFO("INFO. Horizontal Accuracy %f\n", (float)hAccuracy/100.0);
    }

    // Get UTC time
    result = le_gnss_GetTime(positionSampleRef, &hours, &minutes, &seconds, &milliseconds);
    if ( (LE_OK == result) || (LE_OUT_OF_RANGE == result))
    {
        LE_INFO("INFO. Get Time:  %02d:%02d:%02d:%02d. (%d)\n", hours, minutes, seconds, milliseconds, (int)result);
        sprintf(time_string, "%02d:%02d:%02d:%02d", hours, minutes, seconds, milliseconds);
        // LE_INFO("");
    }
    else
    {
        LE_INFO("ERROR. le_gnss_GetTime() returns %d. Expected:  LE_OK or LE_OUT_OF_RANGE\n",(int)result);
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Handler function for Position Notifications.
 */
//--------------------------------------------------------------------------------------------------
static void PositionHandlerFunction(le_gnss_SampleRef_t positionSampleRef, void* contextPtr)
{
    le_result_t result;
    le_gnss_FixState_t state;

    LE_INFO(" New Position sample %p\n", positionSampleRef);

    //Get Position State
    result = le_gnss_GetPositionState(positionSampleRef, &state);
    LE_INFO("INFO. Position state: %s\n",(LE_GNSS_STATE_FIX_NO_POS == state)?"No Fix"
                                 :(LE_GNSS_STATE_FIX_2D == state)?"2D Fix"
                                 :(LE_GNSS_STATE_FIX_3D == state)?"3D Fix"
                                 : "Unknown");

    if ( (result != LE_OK) || (state == LE_GNSS_STATE_FIX_NO_POS) )
    {
        // Wait until a position fixed
        le_gnss_ReleaseSampleRef(positionSampleRef);
        return;
    }

    // Position info
    getPositionInfo(positionSampleRef);

    // Release provided Position sample reference
    le_gnss_ReleaseSampleRef(positionSampleRef);
}

//--------------------------------------------------------------------------------------------------
/**
 * Test: Add Position Handler
*/
//--------------------------------------------------------------------------------------------------
static void* PositionThread(void* context)
{
    le_gnss_ConnectService();

    LE_INFO("INFO. In Position Handler Thread\n");
    PositionHandlerRef = le_gnss_AddPositionHandler(PositionHandlerFunction, NULL);
    if (NULL == PositionHandlerRef)
    {
        LE_INFO("ERROR. le_gnss_AddPositionHandler returns NULL!\n");
    }
    le_event_RunLoop();
    return NULL;
}

COMPONENT_INIT
{
    printf("======================GNSS DEMO=========================\n");

    le_data_AddConnectionStateHandler(&ConnectionStateHandler, NULL);


    // config timer update data
    le_timer_Ref_t functionTimerPtr = le_timer_Create("get URL timer");
    le_clk_Time_t interval = {30, 0};
    le_timer_SetInterval(functionTimerPtr, interval);
    le_timer_SetRepeat(functionTimerPtr, 0);
    le_timer_SetHandler(functionTimerPtr, &TimerHandler);
    le_timer_Start(functionTimerPtr);

    // config timeout for data connection
    dataTimeoutTimerPtr = le_timer_Create("Data connection timeout timer");
    le_clk_Time_t interval2 = {DATA_TIMEOUT_SECS, 1};
    le_timer_SetInterval(dataTimeoutTimerPtr, interval2);
    le_timer_SetHandler(dataTimeoutTimerPtr, &TimeoutHandler);

    // config gnss thread
    le_thread_Ref_t positionThreadRef;
    positionThreadRef = le_thread_Create("PositionThread", PositionThread, NULL);
    le_thread_Start(positionThreadRef);
    // start gnss
    le_gnss_Start();
}
