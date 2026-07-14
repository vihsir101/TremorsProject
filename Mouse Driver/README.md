# Calm Hands Mouse Driver


## Overview
The driver is used to reduce unwanted mouse movement from tremors. Base filter is taken from the offical Windows filter driver sample. The driver uses two FIR filters (For x and y axis) on incoming mouse movement for stabilization. The driver assumes that the tremor is a specific frequency and smoothen it.

## WPF app communication
The driver uses a Control Device Object (CDO) to recieve and send data to the WPF app. For security, the driver requires the app to have adminstrator permissions.

## Universal Windows Driver Compliant

This sample builds a Universal Windows Driver. It uses only APIs and DDIs that are included in OneCoreUAP.

## Installation

This sample is installed via an .inf file (In the Installation folder). The .inf file included in this sample is designed to filter a PS/2 mouse. Right-click on the .inf file and click install. If that doesn't work, navigate to Device Manager, mouse and other pointing device, and right-click your current mouse. Then click update driver, browse my computer for drivers, let me pick from a list of availble drivers on my computer, and navigate to the .inf file.
