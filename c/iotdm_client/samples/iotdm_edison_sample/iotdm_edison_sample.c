

// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// APPLICATION TEMPLATE FOR IoTHub LWM2M client code
//

//     This code was generated by a tool.
//
//     Changes to this file may cause incorrect behavior and will be lost if
//     the code is regenerated.

#include "iothub_lwm2m.h"
#include "threadapi.h"
#include "device_object.h"
#include "firmwareupdate_object.h"
#include "edison.h"


IOTHUB_CLIENT_RESULT start_firmware_download(object_firmwareupdate *obj);
IOTHUB_CLIENT_RESULT start_firmware_update(object_firmwareupdate *obj);
IOTHUB_CLIENT_RESULT start_factory_reset(object_device *obj);
IOTHUB_CLIENT_RESULT start_reboot(object_device *obj);

// To hardcode your connection string, put it here.  If you leave this as NULL,
// we will look on the command line and in ~/.cs
static const char *connectionString = NULL;

// LWM2M Update State -- for /5/0/3 resource
typedef enum FIRMWARE_UPDATE_STATE_TAG
{
    LWM2M_UPDATE_STATE_NONE = 1,
    LWM2M_UPDATE_STATE_DOWNLOADING_IMAGE = 2,
    LWM2M_UPDATE_STATE_IMAGE_DOWNLOADED = 3
} LWM2M_FIRMWARE_UPDATE_STATE;

// LWM2M Update Result -- for /5/0/5 resource
typedef enum FIRMWARE_UPDATE_RESULT
{
    LWM2M_UPDATE_RESULT_DEFAULT = 0,
    LWM2M_UPDATE_RESULT_UPDATE_SUCCESSFUL = 1,
    LWM2M_UPDATE_RESULT_NOT_ENOUGH_STORAGE = 2,
    LWM2M_UPDATE_RESULT_OUT_OF_MEMORY = 3,
    LWM2M_UPDATE_RESULT_CONNECTION_LOST = 4,
    LWM2M_UPDATE_RESULT_CRC_FAILURE = 5,
    LWM2M_UPDATE_RESULT_UNSUPPORTED_PACKAGE = 6,
    LWM2M_UPDATE_RESULT_INVALID_URI = 7
} LWM2M_FIRMWARE_UPDATE_RESULT;

// App state.  This is our own construct and encapsulates update state and update result.
#define APP_UPDATE_STATE_VALUES \
    APP_UPDATE_STATE_IDLE = 0, \
    APP_UPDATE_STATE_DOWNLOAD_IN_PROGRESS = 2, \
    APP_UPDATE_STATE_DOWNLOAD_COMPLETE = 3, \
    APP_UPDATE_STATE_UPDATE_IN_PROGRESS = 5, \
    APP_UPDATE_STATE_UPDATE_SUCCESSFUL = 6 
DEFINE_ENUM(APP_UPDATE_STATE, APP_UPDATE_STATE_VALUES);
DEFINE_ENUM_STRINGS(APP_UPDATE_STATE, APP_UPDATE_STATE_VALUES);

APP_UPDATE_STATE update_state = APP_UPDATE_STATE_IDLE;

// Forwards declarations
IOTHUB_CLIENT_RESULT create_update_thread();
void set_device_state();

// Read the connection string from a file
const char *read_connection_string()
{
    return read_string_from_file("/home/root/.cs");
}

void print_usage()
{
    LogInfo("Usage: IotdmEdisonSample [--service] [connection_string]\r\n ");
    LogInfo("--service               = Run as a service\r\n");
    LogInfo("connection_string   = connection string for hub.\r\n");
}

/**********************************************************************************
 * MAIN LOOP
 *
 **********************************************************************************/
int main(int argc, char *argv[])
{
    const char *cs = NULL;
    bool runAsService = false;

    // Look for --service and connection string on command line
    if (argc >= 2)
    {
        for (int i = 1; i <  argc; i++)
        {
            if (strcasecmp(argv[i], "--service") == 0)
            {
                runAsService = true;
            }
            else
            {
                if (cs == NULL)
                {
                    LogInfo("Getting connection string from command line\r\n");
                    cs = argv[i];
                }
                else
                {
                    print_usage();
                    return -1;
                }
            }
        }
    }

    // No connection string yet, look in ~/.cs
    if (cs ==NULL)
    {
        cs = read_connection_string();
        if (cs != NULL)
        {
            LogInfo("Got connection string from ~/.cs file\r\n");
        }
    }

    // Still no connection string?  Look for something hardcoded
    if (cs == NULL)
    {
        cs = connectionString;
    }
    
    // We've tried everything to get our connection string.  Error out if we don't have one.
    if (cs == NULL)
    {
        print_usage();
        return -1;
    }
    
    LogInfo("Connection string is \"%s\"\r\n",cs);

    IOTHUB_CHANNEL_HANDLE IoTHubChannel = IoTHubClient_DM_Open(cs, COAP_TCPIP);
    if (NULL == IoTHubChannel)
    {
        LogError("IoTHubClientHandle is NULL!\r\n");

        return -1;
    }

    LogInfo("IoTHubClientHandle: %p\r\n", IoTHubChannel);

    LogInfo("prepare LWM2M objects");
    if (IOTHUB_CLIENT_OK != IoTHubClient_DM_CreateDefaultObjects(IoTHubChannel))
    {
        LogError("failure to create LWM2M objects for client: %p\r\n", IoTHubChannel);

        return -1;
    }

    set_device_state();
    object_firmwareupdate *f_obj = get_firmwareupdate_object(0);
    if (f_obj == NULL)
    {
        LogError("failure to get firmware update object for %p\r\n", IoTHubChannel);

        return -1;
    }
    f_obj->firmwareupdate_packageuri_write_callback = start_firmware_download;
    f_obj->firmwareupdate_update_execute_callback = start_firmware_update;

    object_device*d_obj = get_device_object(0);
    if (d_obj == NULL)
    {
        LogError("failure to get device object for %p\r\n", IoTHubChannel);

        return -1;
    }
    d_obj->device_reboot_execute_callback = start_reboot;
    d_obj->device_factoryreset_execute_callback = start_factory_reset;


    if (IOTHUB_CLIENT_OK != create_update_thread())
    {
        LogError("failure to create the udpate thread\r\n");

        return -1;
    }

    if (runAsService)
    {
        LogInfo("--service flag specified.  Running as service\r\n ");
        
        chdir("/");
        daemon(0, 1);
    }

    if (IOTHUB_CLIENT_OK != IoTHubClient_DM_Start(IoTHubChannel))
    {
        LogError("failure to start the client: %p\r\n", IoTHubChannel);

        return -1;
    }

    /* Disconnect and cleanup */
    IoTHubClient_DM_Close(IoTHubChannel);
}

void set_device_state()
{
    set_firmwareupdate_state(0, LWM2M_UPDATE_STATE_NONE);
    set_firmwareupdate_updateresult(0, LWM2M_UPDATE_RESULT_DEFAULT);

    char *serial = get_serial_number();
    if (NULL != serial)
    {
        set_device_serialnumber(0, serial);
        lwm2m_free(serial);
    }

    char *version = get_firmware_version();
    if (NULL != version)
    {
        set_device_firmwareversion(0, version);
        lwm2m_free(version);
    }

}

void update_firmware_udpate_progress()
{
    switch (update_state)
    {
    case APP_UPDATE_STATE_IDLE:
        // nothing to do
        break;
    case APP_UPDATE_STATE_DOWNLOAD_IN_PROGRESS:
        if (is_download_happening() == false)
        {
            if (was_file_successfully_downloaded())
            {
                LogInfo("** firmware download completed\r\n");
                set_firmwareupdate_state(0, LWM2M_UPDATE_STATE_IMAGE_DOWNLOADED);
                LogInfo("** %s - > APP_UPDATE_STATE_DOWNLOAD_COMPLETE\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
                update_state = APP_UPDATE_STATE_DOWNLOAD_COMPLETE;
            }
            else
            {
                LogInfo("** firmware download failed\r\n");
                set_firmwareupdate_updateresult(0, LWM2M_UPDATE_RESULT_INVALID_URI);
                set_firmwareupdate_state(0, LWM2M_UPDATE_STATE_NONE);
                LogInfo("** %s - > APP_UPDATE_STATE_IDLE\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
                update_state = APP_UPDATE_STATE_IDLE;
            }
        }
        break;
    case APP_UPDATE_STATE_DOWNLOAD_COMPLETE:
        // Nothing to do
        break;
    case APP_UPDATE_STATE_UPDATE_IN_PROGRESS:
        if (is_update_happening() == false)
        {
            LogInfo("** firmware update completed\r\n");
            set_firmwareupdate_updateresult(0, LWM2M_UPDATE_RESULT_UPDATE_SUCCESSFUL);
            LogInfo("** %s - > APP_UPDATE_STATE_UPDATE_SUCCESSFUL\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
            update_state  = APP_UPDATE_STATE_UPDATE_SUCCESSFUL;
        }
        break;
    case APP_UPDATE_STATE_UPDATE_SUCCESSFUL:
        // Nothing to do
        break;
    }
}


IOTHUB_CLIENT_RESULT start_firmware_download(object_firmwareupdate *obj)
{
    const char *uri = NULL;

    if (obj == NULL)
    {
        return IOTHUB_CLIENT_ERROR;
    }

    uri = obj->propval_firmwareupdate_packageuri;
    if (uri == NULL || *uri == 0)
    {
        LogInfo("** Empty URI received.  Resetting state machine\r\n");
        LogInfo("** %s - > APP_UPDATE_STATE_IDLE\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
        update_state = APP_UPDATE_STATE_IDLE;
        set_firmwareupdate_state(0, LWM2M_UPDATE_STATE_NONE);
        set_firmwareupdate_updateresult(0, LWM2M_UPDATE_RESULT_DEFAULT);
        return IOTHUB_CLIENT_OK;
    }
    else
    {
        if (update_state != APP_UPDATE_STATE_IDLE)
        {
            LogError("Error: cannot download from state %s\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
            return IOTHUB_CLIENT_INVALID_ARG;
        }
        else
        {
            LogInfo("** URI received from server.  starting download\r\n");
            if (spawn_download_process(uri))
            {
                LogInfo("** %s - > APP_UPDATE_STATE_DOWNLOAD_IN_PROGRESS\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
                update_state = APP_UPDATE_STATE_DOWNLOAD_IN_PROGRESS;
                set_firmwareupdate_state(0, LWM2M_UPDATE_STATE_DOWNLOADING_IMAGE);
                set_firmwareupdate_updateresult(0, LWM2M_UPDATE_RESULT_DEFAULT);
                return IOTHUB_CLIENT_OK;
            }
            else
            {
                LogError("spawn_download_process failed\r\n");
                return IOTHUB_CLIENT_ERROR;
            }
        }
    }
}

IOTHUB_CLIENT_RESULT start_firmware_update(object_firmwareupdate *obj)
{
    LogInfo("** firmware update request posted\r\n");
    if (update_state != APP_UPDATE_STATE_DOWNLOAD_COMPLETE)
    {
        LogError("Error: cannot update from state %s\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
        return  IOTHUB_CLIENT_INVALID_ARG;
    }
    else
    {
        if (spawn_update_process())
        {
            LogInfo("** %s - > APP_UPDATE_STATE_UPDATE_IN_PROGRESS\r\n", ENUM_TO_STRING(APP_UPDATE_STATE, update_state));
            update_state = APP_UPDATE_STATE_UPDATE_IN_PROGRESS;
            set_firmwareupdate_state(0, LWM2M_UPDATE_STATE_NONE);
            return IOTHUB_CLIENT_OK;
        }
        else
        {
            LogError("spawn_update_process failed\r\n");
            return IOTHUB_CLIENT_ERROR;
        }
    }
}

IOTHUB_CLIENT_RESULT start_reboot(object_device *obj)
{
    return spawn_reboot_process() ? IOTHUB_CLIENT_OK : IOTHUB_CLIENT_ERROR;
}

IOTHUB_CLIENT_RESULT start_factory_reset(object_device *obj)
{
    return spawn_factoryreset_process() ? IOTHUB_CLIENT_OK : IOTHUB_CLIENT_ERROR;
}


int update_thread(void *v)
{
    while (true)
    {
        ThreadAPI_Sleep(1000);

        update_firmware_udpate_progress();
    }

    return 0;
    
}

IOTHUB_CLIENT_RESULT create_update_thread()
{
    IOTHUB_CLIENT_RESULT result = IOTHUB_CLIENT_OK;
    THREAD_HANDLE th = NULL;

    THREADAPI_RESULT tr = ThreadAPI_Create(&th, &update_thread, NULL);
    if (tr != THREADAPI_OK)
    {
        LogError("failed to create background thread, error=%d\r\n", tr);
        result = IOTHUB_CLIENT_ERROR;
    }

    return result;
}
 
