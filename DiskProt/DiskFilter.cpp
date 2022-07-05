#include "DiskFilter.h"
#include "ntdddisk.h"
#include <ntddvol.h>
STORAGE_BUS_TYPE DiskProtGetBusType(WDFDEVICE wdfDevice);
extern "C"
NTSTATUS
DriverEntry(PDRIVER_OBJECT  DriverObject,
            PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS          status;

    WDF_DRIVER_CONFIG_INIT(&config,
        DiskProtEvtDeviceAdd);
    //创建一个wdf驱动对象
    status = WdfDriverCreate(DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);

    if (!NT_SUCCESS(status)) {
#if DBG
        DbgPrint("WdfDriverCreate failed - 0x%x\n", status);
#endif
        return status;
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
DiskProtEvtDeviceAdd(WDFDRIVER       Driver,
                      PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS                  status;
    WDF_OBJECT_ATTRIBUTES     wdfObjectAttr;
    WDFDEVICE                 wdfDevice;
    PDISKPROT_DEVICE_CONTEXT devContext;
    WDF_IO_QUEUE_CONFIG       ioQueueConfig;


    //PnP管理器报告新设备存在时 回调该 api 并执行设备初始化操作
#if DBG
    DbgPrint("DiskProtEvtDeviceAdd: Adding device...\n");
#endif

    UNREFERENCED_PARAMETER(Driver);

    //PnP管理器报告新设备存在时 回调该 api 并执行设备初始化操作
    WdfFdoInitSetFilter(DeviceInit);


    //设置PnP电源管理相关回调

    // WDF_PNPPOWER_EVENT_CALLBACKS  pnpPowerCallbacks;
    // WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    // pnpPowerCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    // WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // 指定设备上下文
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&wdfObjectAttr,
                                            DISKPROT_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit,
                             &wdfObjectAttr,
                             &wdfDevice);

    if (!NT_SUCCESS(status)) {
#if DBG
        DbgPrint("WdfDeviceCreate failed - 0x%x\n",
                 status);
#endif
        return status;
    }
    devContext = DiskProtGetDeviceContext(wdfDevice);
    devContext->WdfDevice = wdfDevice;
    // devContext->type = DiskProtGetBusType(wdfDevice); 


    //创建默认队列 以及注册相关回调函数
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                                           WdfIoQueueDispatchParallel);
    ioQueueConfig.EvtIoRead          = DiskProtEvtRead;
    ioQueueConfig.EvtIoWrite         = DiskProtEvtWrite;
    ioQueueConfig.EvtIoDeviceControl = DiskProtEvtDeviceControl;

    //
    // 创建WDF队列
    //
    status = WdfIoQueueCreate(devContext->WdfDevice,
                              &ioQueueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              WDF_NO_HANDLE);

    if (!NT_SUCCESS(status)) {
#if DBG
        DbgPrint("WdfIoQueueCreate failed - 0x%x\n",
                 status);
#endif
        return status;
    }
    return STATUS_SUCCESS;
}


NTSTATUS
EvtDevicePrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
)
{
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);
    DbgPrint("EvtDevicePrepareHardware  GetIn \n");

    if (DiskProtGetBusType(Device) == STORAGE_BUS_TYPE::BusTypeUsb)
    {
        //返回一个状态码
        DbgPrint("EvtDevicePrepareHardware StorageDeviceProperty\n");
        return STATUS_UNSUCCESSFUL;
    }


    return  STATUS_SUCCESS;
}


_Use_decl_annotations_
VOID
DiskProtEvtDeviceControl(WDFQUEUE   Queue,
                          WDFREQUEST Request,
                          size_t     OutputBufferLength,
                          size_t     InputBufferLength,
                          ULONG      IoControlCode)
{
    PDISKPROT_DEVICE_CONTEXT devContext;

    devContext = DiskProtGetDeviceContext(WdfIoQueueGetDevice(Queue));
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

#if DBG
    DbgPrint("DiskProtEvtDeviceControl -- Request 0x%p\n",
             Request);
#endif

    //获取磁盘是否只读IOCTL
    if (IoControlCode == IOCTL_DISK_IS_WRITABLE) {
        if(DiskProtGetBusType(devContext->WdfDevice) == STORAGE_BUS_TYPE::BusTypeUsb)
        {
            //返回一个只读的状态码
            WdfRequestComplete(Request, STATUS_MEDIA_WRITE_PROTECTED);
            return;
        }
        FilterSendWithCallback(Request,
                                    devContext);
        return;
    }
    FilterSendAndForget(Request,
                           devContext);
}
_Use_decl_annotations_
STORAGE_BUS_TYPE DiskProtGetBusType(WDFDEVICE wdfDevice)
{
    WDFIOTARGET                 hidTarget = nullptr;
    WDF_MEMORY_DESCRIPTOR       outputDescriptor;
    STORAGE_PROPERTY_QUERY  query = {};
    PSTORAGE_DESCRIPTOR_HEADER  descriptor = nullptr;
    PSTORAGE_DEVICE_DESCRIPTOR  DeviceDescriptor = nullptr;
    STORAGE_BUS_TYPE currentBusType = STORAGE_BUS_TYPE::BusTypeUnknown;
    hidTarget = WdfDeviceGetIoTarget(wdfDevice);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor,
        (PVOID)&query,
        sizeof(STORAGE_PROPERTY_QUERY));

    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    descriptor = (PSTORAGE_DESCRIPTOR_HEADER)&query;
    NTSTATUS status = WdfIoTargetSendIoctlSynchronously(hidTarget,
        NULL,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &outputDescriptor,
        &outputDescriptor,
        NULL,
        NULL);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("DiskProtEvtDeviceControl WdfIoTargetSendIoctlSynchronously failed 0x%x\n", status);
        return currentBusType;
    }
    else
    {
        DbgPrint("DiskProtEvtDeviceControl %d\n", descriptor->Size);

        ULONG                   bufferLength = 0;
        bufferLength = descriptor->Size;
        NT_ASSERT(bufferLength >= sizeof(STORAGE_PROPERTY_QUERY));
        bufferLength = max(bufferLength, sizeof(STORAGE_PROPERTY_QUERY));
        descriptor = (PSTORAGE_DESCRIPTOR_HEADER)ExAllocatePoolWithTag(NonPagedPoolNx, bufferLength, 'GYqw');
        RtlZeroMemory(&query, sizeof(STORAGE_PROPERTY_QUERY));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;

        RtlCopyMemory(descriptor,
            &query,
            sizeof(STORAGE_PROPERTY_QUERY));

        //开辟完空间重新获取数据
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor,
            (PVOID)descriptor,
            bufferLength);

        status = WdfIoTargetSendIoctlSynchronously(hidTarget,
            NULL,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &outputDescriptor,
            &outputDescriptor,
            NULL,
            NULL);
        DeviceDescriptor = (PSTORAGE_DEVICE_DESCRIPTOR)descriptor;
        DbgPrint("DiskProtEvtDeviceControl StorageDeviceProperty %d\n", DeviceDescriptor->BusType);
        currentBusType = DeviceDescriptor->BusType;
        FREE_POOL(DeviceDescriptor);
    }
   
    return currentBusType;
}

_Use_decl_annotations_
VOID
DiskProtEvtRead(WDFQUEUE   Queue,
                 WDFREQUEST Request,
                 size_t     Length)
{
    PDISKPROT_DEVICE_CONTEXT devContext;

    UNREFERENCED_PARAMETER(Length);
    devContext = DiskProtGetDeviceContext(WdfIoQueueGetDevice(Queue));

#if DBG
    DbgPrint("DiskProtEvtRead -- Request 0x%p\n",
        Request);
#endif
    
    FilterSendAndForget(Request,
                           devContext);
}

_Use_decl_annotations_
VOID
DiskProtEvtWrite(WDFQUEUE   Queue,
                  WDFREQUEST Request,
                  size_t     Length)
{
    PDISKPROT_DEVICE_CONTEXT devContext;

    UNREFERENCED_PARAMETER(Length);

    devContext = DiskProtGetDeviceContext(WdfIoQueueGetDevice(Queue));

#if DBG
    DbgPrint("DiskProtEvtWrite -- Request 0x%p\n",
             Request);
#endif

    FilterSendAndForget(Request,
                           devContext);
}
_Use_decl_annotations_
VOID
FilterSendAndForget(WDFREQUEST                Request,
                       PDISKPROT_DEVICE_CONTEXT DevContext)
{
    NTSTATUS status;

    WDF_REQUEST_SEND_OPTIONS sendOpts;

    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOpts,
                                  WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(Request,
                        WdfDeviceGetIoTarget(DevContext->WdfDevice),
                        &sendOpts)) {
        status = WdfRequestGetStatus(Request);
#if DBG
        DbgPrint("WdfRequestSend 0x%p failed - 0x%x\n",
                 Request,
                 status);
#endif
        WdfRequestComplete(Request,
                           status);
    }
}


_Use_decl_annotations_
VOID
FilterCompletionCallback(WDFREQUEST                     Request,
                            WDFIOTARGET                    Target,
                            PWDF_REQUEST_COMPLETION_PARAMS Params,
                            WDFCONTEXT                     Context)
{
    NTSTATUS status;
    auto*    devContext = (PDISKPROT_DEVICE_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(devContext);

    DbgPrint("FilterCompletionCallback: Request=%p, Status=0x%x; Information=0x%Ix\n",
             Request,
             Params->IoStatus.Status,
             Params->IoStatus.Information);

    status = Params->IoStatus.Status;

    WdfRequestComplete(Request,
                       status);
}


_Use_decl_annotations_
VOID
FilterSendWithCallback(WDFREQUEST                Request,
                          PDISKPROT_DEVICE_CONTEXT DevContext)
{
    NTSTATUS status;

    DbgPrint("Sending %p with completion\n",
             Request);
    WdfRequestFormatRequestUsingCurrentType(Request);

    WdfRequestSetCompletionRoutine(Request,
                                   FilterCompletionCallback,
                                   DevContext);
    if (!WdfRequestSend(Request,
                        WdfDeviceGetIoTarget(DevContext->WdfDevice),
                        WDF_NO_SEND_OPTIONS)) {
        status = WdfRequestGetStatus(Request);

        DbgPrint("WdfRequestSend failed = 0x%x\n",
                 status);
        WdfRequestComplete(Request,
                           status);
    }
}
