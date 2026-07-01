/*++

Copyright (c) 2008  Microsoft Corporation



Module Name:



    moufiltr.h



Abstract:



    This module contains the common private declarations for the mouse

    packet filter



Environment:



    kernel mode only



Notes:





Revision History:





--*/



#ifndef MOUFILTER_H

#define MOUFILTER_H



#include <ntddk.h>

#include <kbdmou.h>

#include <ntddmou.h>

#include <ntdd8042.h>

#include <wdf.h>

#define MAXTAPS 64

#define IOCTL_UPDATE_FIR_SETTINGS CTL_CODE(FILE_DEVICE_MOUSE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_FILTER_ENABLED CTL_CODE(FILE_DEVICE_MOUSE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

//Debug from windows

#if DBG

#define TRAP()                      DbgBreakPoint()

#define DebugPrint(_x_) DbgPrint _x_

#else   // DBG


#define TRAP()
#define DebugPrint(_x_)

#endif





typedef struct _FIR_SETTINGS {
    LONG numTaps;
    LONG coefficientsX[MAXTAPS];
    LONG coefficientsY[MAXTAPS];

    
} FIR_SETTINGS, * PFIRSETTINGS;




typedef struct _FIR_Filter {


    LONG buffer[MAXTAPS];

    LONG coefficients[MAXTAPS];

    LONG index;

    LONG numTaps;



} FIRFilter, * PFIRfilter;



typedef struct _DEVICE_EXTENSION

{



    //

   // Previous hook routine and context

   //                               

    PVOID UpperContext;

    PI8042_MOUSE_ISR UpperIsrHook;

    ULONG counter;



    //

    // Write to the mouse in the context of MouFilter_IsrHook

    //

    IN PI8042_ISR_WRITE_PORT IsrWritePort;

    //

    // Context for IsrWritePort, QueueMousePacket

    //

    IN PVOID CallContext;

    //

    // Queue the current packet (ie the one passed into MouFilter_IsrHook)

    // to be reported to the class driver

    //

    IN PI8042_QUEUE_PACKET QueueMousePacket;

    //

    // The real connect data that this driver reports to

    //

    CONNECT_DATA UpperConnectData;


    FIRFilter ffilterx;

    FIRFilter ffiltery;

    BOOLEAN filterEnabled;

    WDFSPINLOCK FilterLock;





} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

typedef struct _CDO_CONTEXT {
    PDEVICE_EXTENSION FilterDeviceExtension;

} CDO_CONTEXT, * PCDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION,

    FilterGetData)



    //

    // Prototypes

    //
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD MouFilter_EvtDeviceAdd;



NTSTATUS CreateControlDevice(WDFDRIVER Driver, PDEVICE_EXTENSION MouseExtension);
NTSTATUS ConfigureControlQueue(WDFDEVICE ControlDevice);
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtControlQueueIoDeviceControl;





VOID

FIR_Init(

    PFIRfilter ffilter

);




LONG

FIR_Push(

    PFIRfilter ffilter,

    LONG change

);





VOID

MouFilter_DispatchPassThrough(

    _In_ WDFREQUEST Request,

    _In_ WDFIOTARGET Target

);

BOOLEAN

MouFilter_IsrHook(

    PVOID         DeviceExtension,

    PMOUSE_INPUT_DATA       CurrentInput,

    POUTPUT_PACKET          CurrentOutput,

    UCHAR                   StatusByte,

    PUCHAR                  DataByte,

    PBOOLEAN                ContinueProcessing,

    PMOUSE_STATE            MouseState,

    PMOUSE_RESET_SUBSTATE   ResetSubState

);



VOID

MouFilter_ServiceCallback(

    IN PDEVICE_OBJECT DeviceObject,

    IN PMOUSE_INPUT_DATA InputDataStart,

    IN PMOUSE_INPUT_DATA InputDataEnd,

    IN OUT PULONG InputDataConsumed

);

VOID EvtControlQueueIoDeviceControl( 
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode
);

VOID MouFilter_EvtIoInternalDeviceControl(

    IN WDFQUEUE      Queue,

    IN WDFREQUEST    Request,

    IN size_t        OutputBufferLength,

    IN size_t        InputBufferLength,

    IN ULONG         IoControlCode

);

#endif  // MOUFILTER_H