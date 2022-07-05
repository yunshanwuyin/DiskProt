#pragma once
#include <wdm.h>
#include <wdf.h>
#pragma warning(disable: 26438)     // avoid "goto"
#pragma warning(disable: 26440)     // Function can be delcared "noexcept"
#pragma warning(disable: 26485)     // No array to pointer decay
#pragma warning(disable: 26493)     // C-style casts
#pragma warning(disable: 26494)     // Variable is uninitialized

#define FREE_POOL(_PoolPtr)     \
    if (_PoolPtr != NULL) {     \
        ExFreePool(_PoolPtr);   \
        _PoolPtr = NULL;        \
    }

typedef struct _DISKPROT_DEVICE_CONTEXT { 
    WDFDEVICE       WdfDevice;
//    int type;

} DISKPROT_DEVICE_CONTEXT, *PDISKPROT_DEVICE_CONTEXT;


WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DISKPROT_DEVICE_CONTEXT,
                                   DiskProtGetDeviceContext)



extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD DiskProtEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_READ DiskProtEvtRead;
EVT_WDF_IO_QUEUE_IO_WRITE DiskProtEvtWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DiskProtEvtDeviceControl;

EVT_WDF_REQUEST_COMPLETION_ROUTINE FilterCompletionCallback;
VOID
FilterSendAndForget(_In_ WDFREQUEST Request, _In_ PDISKPROT_DEVICE_CONTEXT DevContext);
VOID
FilterSendWithCallback(_In_ WDFREQUEST Request, _In_ PDISKPROT_DEVICE_CONTEXT DevContext);
NTSTATUS
EvtDevicePrepareHardware(IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
);
