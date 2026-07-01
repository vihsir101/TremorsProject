/*--

Copyright (c) 2008  Microsoft Corporation



Module Name:



    moufiltr.c



Abstract:



Environment:



    Kernel mode only- Framework Version



Notes:





--*/



#include "moufiltr.h"



#ifdef ALLOC_PRAGMA

#pragma alloc_text (INIT, DriverEntry)

#pragma alloc_text (PAGE, MouFilter_EvtDeviceAdd)

#pragma alloc_text (PAGE, MouFilter_EvtIoInternalDeviceControl)

#endif



#pragma warning(push)

#pragma warning(disable:4055) // type case from PVOID to PSERVICE_CALLBACK_ROUTINE

#pragma warning(disable:4152) // function/data pointer conversion in expression

#pragma warning(disable:4101)

#pragma warning(disable:4100)


DECLARE_CONST_UNICODE_STRING(CDOName, L"\\Device\\CalmHandsControl");               //  Create namespace path for CDO for internal usage
DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\CalmHandsLink");     // Create path for user-mode application to use
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CDO_CONTEXT, CdoGetData)                         // 
DECLARE_CONST_UNICODE_STRING(SddlAdminSystemAll, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");   // Create SDDL variable (Tells the driver what privileges are required communicate with driver). Same as SDDL_DEVOBJ_SYS_ALL_ADM_ALL

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)

/*++

Routine Description:



    Installable driver initialization entry point.

    This entry point is called directly by the I/O system.



--*/

{

    WDF_DRIVER_CONFIG config;

    NTSTATUS status;







    DebugPrint(("Mouse Filter Driver - Tremors Stablization with App\n"));

    DebugPrint(("Built %s %s\n", __DATE__, __TIME__));



    // Initialize driver config to control the attributes that

    // are global to the driver. Note that framework by default

    // provides a driver unload routine. If you create any resources

    // in the DriverEntry and want to be cleaned in driver unload,

    // you can override that by manually setting the EvtDriverUnload in the

    // config structure. In general xxx_CONFIG_INIT macros are provided to

    // initialize most commonly used members.



    WDF_DRIVER_CONFIG_INIT(&config, MouFilter_EvtDeviceAdd);



    //

    // Create a framework driver object to represent our driver.

    //

    status = WdfDriverCreate(DriverObject,

        RegistryPath,

        WDF_NO_OBJECT_ATTRIBUTES,

        &config,

        WDF_NO_HANDLE); // hDriver optional

    if (!NT_SUCCESS(status)) {

        DebugPrint(("WdfDriverCreate failed with status 0x%x\n", status));

    }



    return status;

}



NTSTATUS MouFilter_EvtDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT  DeviceInit)

/*++

Routine Description:



    EvtDeviceAdd is called by the framework in response to AddDevice

    call from the PnP manager. Here you can query the device properties

    using WdfFdoInitWdmGetPhysicalDevice/IoGetDeviceProperty and based

    on that, decide to create a filter device object and attach to the

    function stack.



    If you are not interested in filtering this particular instance of the

    device, you can just return STATUS_SUCCESS without creating a framework

    device.



Arguments:



    Driver - Handle to a framework driver object created in DriverEntry



    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.



Return Value:



    NTSTATUS



--*/

{

    WDF_OBJECT_ATTRIBUTES deviceAttributes;

    NTSTATUS status;

    WDFDEVICE hDevice;

    WDF_IO_QUEUE_CONFIG ioQueueConfig;

    PDEVICE_EXTENSION           devExt;
    



    UNREFERENCED_PARAMETER(Driver);



    PAGED_CODE();



    DebugPrint(("Enter FilterEvtDeviceAdd \n"));











    //

    // Tell the framework that you are filter driver. Framework

    // takes care of inherting all the device flags & characterstics

    // from the lower device you are attaching to.

    //

    WdfFdoInitSetFilter(DeviceInit);



    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);



    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_EXTENSION);









    //

    // Create a framework device object.  This call will in turn create

    // a WDM deviceobject, attach to the lower stack and set the

    // appropriate flags and attributes.

    //

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);

    if (!NT_SUCCESS(status)) {

        DebugPrint(("WdfDeviceCreate failed with status code 0x%x\n", status));

        return status;

    }



    

    devExt = FilterGetData(hDevice);

    // Create a SpinLock object, used to access and save data. Prevents interruptions

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &devExt->FilterLock);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("WdfSpinLockCreate failed with status code 0x%x\n", status));
        return status;
    }


    // Initalize filters

    FIR_Init(&devExt->ffilterx);
    FIR_Init(&devExt->ffiltery);


    // Create the CDO (Control Device Object)

    status = CreateControlDevice(Driver, devExt);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("CDO: Failed to create control channel 0x%x\n", status));
        return status;
    }


    //

    // Configure the default queue to be Parallel. Do not use sequential queue

    // if this driver is going to be filtering PS2 ports because it can lead to

    // deadlock. The PS2 port driver sends a request to the top of the stack when it

    // receives an ioctl request and waits for it to be completed. If you use a

    // a sequential queue, this request will be stuck in the queue because of the 

    // outstanding ioctl request sent earlier to the port driver.

    //

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);





    ioQueueConfig.EvtIoInternalDeviceControl = MouFilter_EvtIoInternalDeviceControl;

    //

    // Framework by default creates non-power managed queues for

    // filter drivers.

    //

    status = WdfIoQueueCreate(hDevice,

        &ioQueueConfig,

        WDF_NO_OBJECT_ATTRIBUTES,

        WDF_NO_HANDLE // pointer to default queue

    );

    if (!NT_SUCCESS(status)) {

        DebugPrint(("WdfIoQueueCreate failed 0x%x\n", status));

        return status;

    }



    return status;

}


NTSTATUS CreateControlDevice(WDFDRIVER Driver, PDEVICE_EXTENSION MouseExtension) { 
    NTSTATUS status;
    PWDFDEVICE_INIT pInit = NULL;
    WDFDEVICE controlDevice;
    WDF_OBJECT_ATTRIBUTES attributes;

    pInit = WdfControlDeviceInitAllocate(Driver, &SddlAdminSystemAll);
    if (pInit == NULL) {
        DebugPrint(("CDO: WdfControlDeviceInitAllocate failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = WdfDeviceInitAssignName(pInit, &CDOName);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(pInit);
        DebugPrint(("CDO: WdfDeviceInitFree failed 0x%x\n", status));
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CDO_CONTEXT);

    status = WdfDeviceCreate(&pInit, &attributes, &controlDevice);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("CDO: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    PCDO_CONTEXT cdoContext = CdoGetData(controlDevice);
    cdoContext->FilterDeviceExtension = MouseExtension;

    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(controlDevice);
        DebugPrint(("CDO: WdfDeviceCreateSymbolicLink failed 0x%x\n", status));
        return status;
    }

    status = ConfigureControlQueue(controlDevice);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(controlDevice);
        DebugPrint(("CDO: ConfigureControlQueue failed 0x%x\n", status));
        return status;
    }

    WdfControlFinishInitializing(controlDevice);
    return status;
}


NTSTATUS ConfigureControlQueue(WDFDEVICE ControlDevice) {
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);

    queueConfig.EvtIoDeviceControl = EvtControlQueueIoDeviceControl;

    status = WdfIoQueueCreate(
        ControlDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
    );


    if (!NT_SUCCESS(status)) {
        DebugPrint(("CDO: WdfIoQueueCreate failed 0x%08X\n", status));
        return status;
    }

    return status;


}

VOID EvtControlQueueIoDeviceControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode) 
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    PFIRSETTINGS newSettings = NULL;
    size_t bytesReturned = 0;
    PBOOLEAN filterEnabled = NULL;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WDFDEVICE cdoDevice = WdfIoQueueGetDevice(Queue);


    PDEVICE_EXTENSION devExt = CdoGetData(cdoDevice)->FilterDeviceExtension;

    switch (IoControlCode) {
    case IOCTL_UPDATE_FIR_SETTINGS:


        status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(FIR_SETTINGS),
            (PVOID*)&newSettings,
            NULL);

        if (NT_SUCCESS(status) && newSettings != NULL) {
            if (newSettings->numTaps > 0 && newSettings->numTaps <= MAXTAPS) {

                WdfSpinLockAcquire(devExt->FilterLock);

                devExt->ffilterx.numTaps = newSettings->numTaps;
                devExt->ffiltery.numTaps = newSettings->numTaps;
                devExt->ffilterx.index = 0;
                devExt->ffiltery.index = 0;

                for (LONG i = 0; i < newSettings->numTaps; i++) {
                    devExt->ffilterx.coefficients[i] = newSettings->coefficientsX[i];
                    devExt->ffiltery.coefficients[i] = newSettings->coefficientsY[i];
                    devExt->ffilterx.buffer[i] = 0;
                    devExt->ffiltery.buffer[i] = 0;
                }

                WdfSpinLockRelease(devExt->FilterLock);
                status = STATUS_SUCCESS;
                bytesReturned = sizeof(FIR_SETTINGS);
            }
            else {
                status = STATUS_INVALID_PARAMETER;
            }
        }
        break;

    case IOCTL_FILTER_ENABLED:
        status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(BOOLEAN),
            (PVOID*)&filterEnabled,
            NULL);

        if (NT_SUCCESS(status) && filterEnabled != NULL) {

            WdfSpinLockAcquire(devExt->FilterLock);

            devExt->filterEnabled = *filterEnabled;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(BOOLEAN);

            WdfSpinLockRelease(devExt->FilterLock);
        }
        break;

    default:
        break;
    }


    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}


// Same as the sample Windows driver on GitHub
VOID MouFilter_DispatchPassThrough(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)

/*++

Routine Description:



    Passes a request on to the lower driver.





--*/

{

    //

    // Pass the IRP to the target

    //



    WDF_REQUEST_SEND_OPTIONS options;

    BOOLEAN ret;

    NTSTATUS status = STATUS_SUCCESS;



    //

    // We are not interested in post processing the IRP so 

    // fire and forget.

    //

    WDF_REQUEST_SEND_OPTIONS_INIT(&options,

        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);



    ret = WdfRequestSend(Request, Target, &options);



    if (ret == FALSE) {

        status = WdfRequestGetStatus(Request);

        DebugPrint(("WdfRequestSend failed: 0x%x\n", status));

        WdfRequestComplete(Request, status);

    }



    return;

}


// Same as the sample Windows driver on GitHub
VOID MouFilter_EvtIoInternalDeviceControl(

    IN WDFQUEUE      Queue,

    IN WDFREQUEST    Request,

    IN size_t        OutputBufferLength,

    IN size_t        InputBufferLength,

    IN ULONG         IoControlCode

)

/*++



Routine Description:



    This routine is the dispatch routine for internal device control requests.

    There are two specific control codes that are of interest:



    IOCTL_INTERNAL_MOUSE_CONNECT:

        Store the old context and function pointer and replace it with our own.

        This makes life much simpler than intercepting IRPs sent by the RIT and

        modifying them on the way back up.



    IOCTL_INTERNAL_I8042_HOOK_MOUSE:

        Add in the necessary function pointers and context values so that we can

        alter how the ps/2 mouse is initialized.



    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_MOUSE is *NOT* necessary if

           all you want to do is filter MOUSE_INPUT_DATAs.  You can remove

           the handling code and all related device extension fields and

           functions to conserve space.





--*/

{



    PDEVICE_EXTENSION           devExt;

    PCONNECT_DATA               connectData;

    PINTERNAL_I8042_HOOK_MOUSE  hookMouse;

    NTSTATUS                   status = STATUS_SUCCESS;

    WDFDEVICE                 hDevice;

    size_t                           length;



    UNREFERENCED_PARAMETER(OutputBufferLength);

    UNREFERENCED_PARAMETER(InputBufferLength);



    PAGED_CODE();



    hDevice = WdfIoQueueGetDevice(Queue);

    devExt = FilterGetData(hDevice);







    switch (IoControlCode) {



        //

        // Connect a mouse class device driver to the port driver.

        //

    case IOCTL_INTERNAL_MOUSE_CONNECT:

        //

        // Only allow one connection.

        //

        if (devExt->UpperConnectData.ClassService != NULL) {

            status = STATUS_SHARING_VIOLATION;

            break;

        }



        //

        // Copy the connection parameters to the device extension.

        //

        status = WdfRequestRetrieveInputBuffer(Request,

            sizeof(CONNECT_DATA),

            &connectData,

            &length);

        if (!NT_SUCCESS(status)) {

            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));

            break;

        }





        devExt->UpperConnectData = *connectData;



        //

        // Hook into the report chain.  Everytime a mouse packet is reported to

        // the system, MouFilter_ServiceCallback will be called

        //

        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);

        connectData->ClassService = MouFilter_ServiceCallback;



        break;



        //

        // Disconnect a mouse class device driver from the port driver.

        //

    case IOCTL_INTERNAL_MOUSE_DISCONNECT:



        //

        // Clear the connection parameters in the device extension.

        //

        // devExt->UpperConnectData.ClassDeviceObject = NULL;

        // devExt->UpperConnectData.ClassService = NULL;



        status = STATUS_NOT_IMPLEMENTED;

        break;



        //

        // Attach this driver to the initialization and byte processing of the 

        // i8042 (ie PS/2) mouse.  This is only necessary if you want to do PS/2

        // specific functions, otherwise hooking the CONNECT_DATA is sufficient

        //

    case IOCTL_INTERNAL_I8042_HOOK_MOUSE:



        DebugPrint(("hook mouse received!\n"));



        // Get the input buffer from the request

        // (Parameters.DeviceIoControl.Type3InputBuffer)

        //

        status = WdfRequestRetrieveInputBuffer(Request,

            sizeof(INTERNAL_I8042_HOOK_MOUSE),

            &hookMouse,

            &length);

        if (!NT_SUCCESS(status)) {

            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));

            break;

        }



        //

        // Set isr routine and context and record any values from above this driver

        //

        devExt->UpperContext = hookMouse->Context;

        hookMouse->Context = (PVOID)devExt;



        if (hookMouse->IsrRoutine) {

            devExt->UpperIsrHook = hookMouse->IsrRoutine;

        }

        hookMouse->IsrRoutine = (PI8042_MOUSE_ISR)MouFilter_IsrHook;



        //

        // Store all of the other functions we might need in the future

        //

        devExt->IsrWritePort = hookMouse->IsrWritePort;

        devExt->CallContext = hookMouse->CallContext;

        devExt->QueueMousePacket = hookMouse->QueueMousePacket;



        status = STATUS_SUCCESS;

        break;



        //

        // Might want to capture this in the future.  For now, then pass it down

        // the stack.  These queries must be successful for the RIT to communicate

        // with the mouse.

        //

    case IOCTL_MOUSE_QUERY_ATTRIBUTES:

    default:

        break;

    }



    if (!NT_SUCCESS(status)) {

        WdfRequestComplete(Request, status);

        return;

    }



    MouFilter_DispatchPassThrough(Request, WdfDeviceGetIoTarget(hDevice));

}




// Same as the sample Windows driver on GitHub
BOOLEAN MouFilter_IsrHook(

    PVOID                   DeviceExtension,

    PMOUSE_INPUT_DATA       CurrentInput,

    POUTPUT_PACKET          CurrentOutput,

    UCHAR                   StatusByte,

    PUCHAR                  DataByte,

    PBOOLEAN                ContinueProcessing,

    PMOUSE_STATE            MouseState,

    PMOUSE_RESET_SUBSTATE   ResetSubState

)

/*++



Remarks:

    i8042prt specific code, if you are writing a packet only filter driver, you

    can remove this function



Arguments:



    DeviceExtension - Our context passed during IOCTL_INTERNAL_I8042_HOOK_MOUSE



    CurrentInput - Current input packet being formulated by processing all the

                    interrupts



    CurrentOutput - Current list of bytes being written to the mouse or the

                    i8042 port.



    StatusByte    - Byte read from I/O port 60 when the interrupt occurred



    DataByte      - Byte read from I/O port 64 when the interrupt occurred.

                    This value can be modified and i8042prt will use this value

                    if ContinueProcessing is TRUE



    ContinueProcessing - If TRUE, i8042prt will proceed with normal processing of

                         the interrupt.  If FALSE, i8042prt will return from the

                         interrupt after this function returns.  Also, if FALSE,

                         it is this functions responsibilityt to report the input

                         packet via the function provided in the hook IOCTL or via

                         queueing a DPC within this driver and calling the

                         service callback function acquired from the connect IOCTL



Return Value:



    Status is returned.



  --+*/

{

    PDEVICE_EXTENSION   devExt;

    BOOLEAN             retVal = TRUE;



    devExt = DeviceExtension;



    if (devExt->UpperIsrHook) {

        retVal = (*devExt->UpperIsrHook) (devExt->UpperContext,

            CurrentInput,

            CurrentOutput,

            StatusByte,

            DataByte,

            ContinueProcessing,

            MouseState,

            ResetSubState

            );



        if (!retVal || !(*ContinueProcessing)) {

            return retVal;

        }

    }



    *ContinueProcessing = TRUE;

    return retVal;

}







VOID MouFilter_ServiceCallback(

    IN PDEVICE_OBJECT DeviceObject,

    IN PMOUSE_INPUT_DATA InputDataStart,

    IN PMOUSE_INPUT_DATA InputDataEnd,

    IN OUT PULONG InputDataConsumed

)

/*++



Routine Description:



    Called when there are mouse packets to report to the RIT.  You can do

    anything you like to the packets.  For instance:



    o Drop a packet altogether

    o Mutate the contents of a packet

    o Insert packets into the stream



Arguments:



    DeviceObject - Context passed during the connect IOCTL



    InputDataStart - First packet to be reported



    InputDataEnd - One past the last packet to be reported.  Total number of

                   packets is equal to InputDataEnd - InputDataStart



    InputDataConsumed - Set to the total number of packets consumed by the RIT

                        (via the function pointer we replaced in the connect

                        IOCTL)



Return Value:



    Status is returned.



--*/

{



    PDEVICE_EXTENSION   devExt;
    WDFDEVICE   hDevice;

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);

    devExt = FilterGetData(hDevice);



    LONG changeX = 0;

    LONG changeY = 0;


    // Start SpinLock

    WdfSpinLockAcquire(devExt->FilterLock);



    for (PMOUSE_INPUT_DATA packet = InputDataStart; packet < InputDataEnd; packet++) {

        devExt->counter++;

        // Check if filter is Enabled otherwise keep same values
        if (devExt->filterEnabled) {

            // change x and y is the new location of the mouse according to the FIR filter
            changeX = FIR_Push(&devExt->ffilterx, packet->LastX);
            changeY = FIR_Push(&devExt->ffiltery, packet->LastY);

            // Change the position of the mouse packet to the updated position
            packet->LastX = changeX;
            packet->LastY = changeY;
        }



        // Print every 100th packet for debugging purposes
        if (devExt->counter % 100 == 0) {

            DebugPrint(("Mouse Packet %d: x,y = %d, %d; new x, y = %d, %d\n", devExt->counter, packet->LastX, packet->LastY, changeX, changeY));

        }



    }

    // Stop SpinLock
    WdfSpinLockRelease(devExt->FilterLock);




    //

    // UpperConnectData must be called at DISPATCH

    //

    (*(PSERVICE_CALLBACK_ROUTINE)devExt->UpperConnectData.ClassService)(

        devExt->UpperConnectData.ClassDeviceObject,

        InputDataStart,

        InputDataEnd,

        InputDataConsumed

        );

}









/*

FIR filter implementation

*/







VOID FIR_Init(PFIRfilter ffilter)
/*
Creates the initial filter

*/
{
    ffilter->numTaps = 21;
    ffilter->index = 0;
    LONG baseWeight = 1024 / ffilter->numTaps;


    // Sets each value in the buffer to 0 and initializes the fir filter cofficients to the same value
    for (LONG i = 0; i < MAXTAPS; i++) {
        ffilter->buffer[i] = 0;
        if (i < ffilter->numTaps) {
            ffilter->coefficients[i] = baseWeight;
        }
        else {
            ffilter->coefficients[i] = 0;
        }
    }


    // Add the remainder of 1024 / ffilter-> numtaps to the middle value to make sure the sum is 1024
    ffilter->coefficients[ffilter->numTaps / 2] += 1024 % ffilter->numTaps;



}



LONG FIR_Push(PFIRfilter ffilter, LONG change)
/*

*/

{
    // updates the circular buffer by replacing the oldest value by new input
    ffilter->buffer[ffilter->index] = change;

    // Makes sure the number of taps is within the range [0, MAXTAPS]
    LONG currentTaps = ffilter->numTaps;
    if (currentTaps <= 0 || currentTaps > MAXTAPS) return change;

    // FIR filter logic, take each value in the buffer, multiply it by its coresponding coefficent, and add to sum. (literally just a weighted sum)
    LONGLONG sum = 0;
    for (LONG i = 0; i < currentTaps; i++) {

        sum += (LONGLONG)ffilter->coefficients[i] * ffilter->buffer[i];

    }
    // Update index
    ffilter->index = (ffilter->index + 1) % currentTaps;

    // Bit shift sum down by 10 (essientially dividing by 1024). Drivers aren't good with the small numbers required for a FIR filter so instead we use larger numbers and then divide them at the end.
    return (LONG)(sum >> 10);

}





#pragma warning(pop) 

