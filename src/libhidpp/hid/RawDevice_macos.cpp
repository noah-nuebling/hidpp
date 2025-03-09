/*
 * Copyright 2021 Clément Vuchener
 * File created by Noah Nuebling
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
    Notes/discussion

    Investigation: Which APIs / high-level-approach should we use? [Mar 5 2025]
        - The easiest approach would be to use device files in the /dev folder. Then we could probably mostly copy the linux/POSIX implementation. 
            However, macOS doesn't seem to put hid devices into the /dev folder. (Source: https://stackoverflow.com/a/28405248/10601702)
        - The next best approach would be to at least copy the 'semantics' of the linux and windows implementations. That means we want to implement the device-communication via raw 
            USB-reports (implemented by writeReport(), readReport(), interruptRead()) with the same timing and blocking behavior as the linux and windows implementations. (I think everything is just blocking/synchronous in the linux implementation.)
        - What APIs do we have available for this on macOS?
            - IOHIDDevice
                - This is relatively well documented and easy to use.
                - This has synchronous APIs (IOHIDDeviceGetReport, IOHIDDeviceSetReport) and synchronous APIs with timeouts (IOHIDDeviceGetReportWithCallback, IOHIDDeviceSetReportWithCallback). (The docs say the callback APIs are 'asynchronous', but I think that's a mistake, since they also say that they 'block'.)
                    - However, the docs explicitly say that these are only 'relevant' for 'output or feature type reports' because of 'sporadic devicesupport for polling input reports'
                    - The docs tell you to use the runLoop-based asynchronous variant (IOHIDDeviceRegisterInputReportCallback()) for input events instead.
                    - (Not sure if relevant:) IOHIDDeviceSetReportWithCallback was apparently broken until some recent macOS version (Source: https://stackoverflow.com/a/78495232/10601702).
                - Problems:
                    - hidpp's readReport() wants to read input reports synchronously with a timeout, but the only available method for that (IOHIDDeviceGetReportWithCallback) explicitly tells you not to do that.
                    - I don't see any way at all to implement hidpp's interruptRead() with these APIs.
                - Solutions:
                    1. I don't quite understand why Apple's docs say not to use IOHIDDeviceGetReportWithCallback for input reports, even though it seems to work very similarly to linux's read() which is apparently safe. Perhaps the macOS function is also safe for our usecase? TODO: Ask cvuchener what he thinks.
                    2. Create a separate thread with a runloop and then schedule the asynchronous variant (IOHIDDeviceRegisterInputReportCallback) on that runLoop to receive input-report-interrupts. Then use mutex/semaphore/etc to coordinate with the threads calling hidpp's interface functions: writeReport, readReport and interruptRead.
                        -> This is what we have sort of implemented as of [Mar 2025]
                    3. Perhaps we could run the runLoop on the calling thread like (https://stackoverflow.com/a/5878918/10601702) and avoid creating a separate thread.
                        -> My main worry is that for back-to-back writeReport() and readReport() calls, the readReport() call might start its runLoop too late – after the device has already 'responded' to the written report with an interrupt. (IIRC, we had a similar bug with the runLoop-on-a-separate-thread implementation.)
                        -> Smaller concern: Is this slower than the 2-threaded solution?
            - HIDDeviceDeviceInterface
                - IOHIDDevice uses this under-the-hood (See: https://github.com/opensource-apple/IOKitUser/blob/master/hid.subproj/IOHIDDevice.c#L129)
                - Don't see a benefit to using this directly over IOHIDDevice.
            - IOHIDDeviceInterface
                - Docs say it's the 'primary interface to HID devices'
                - Found in the IOHIDLibObsolete.h header. So probably deprecated? Not explicitly marked as deprecated, though.
                - Has a getReport() function with a timeout param.
                - Sidenote: Function docs are found in the header under 'BEGIN READABLE STRUCTURE DEFINITIONS'
            - HIDDeviceClient
                - Swift-only, macOS 15.0+ –> Not usable.
                - Only seems to have non-blocking functions (such as dispatchGetReportRequest)
            - IOUSBHostDevice/IOUSBHostInterface
                - objc interface for low-level communication with USB devices. macOS 10.15+
                - Seems to support all the semantics that hidpp needs. 
                - Tested this and could only get it to work for USB connection, not Bluetooth connection. (Tested VXE R1SE+ mouse in objc-tests-nov-2024 repo in [Mar 2025])
            - IOUSBDeviceInterface
                - I suspect this also won't work for Bluetooth.
                - Also see: 
                    - Used by this Razer driver: https://github.com/1kc/razer-macos/blob/47f2345d32d03d421accd824357709f5bca0d05c/src/driver/razerdevice.c
                    - Razer driver apparently doesn't support Bluetooth, which confirms our suspicions: https://github.com/1kc/razer-macos/issues/566#issuecomment-1049337637

    Problems I can see with the current implementation [Mar 2025]
        - We're running a runLoop on a dispatch_queue instead of NSThread. I'm not sure that could lead to problems. IIRC, dispatch_queues don't have autoreleasepools by default, which could lead to memory leaks when using Apple APIs. Also CFRunLoopGetCurrent may return NULL on dispatchQueues other than the main one IIRC. We wrote more about this somewhere in the MMF source code or somewhere else I think.
        - The 'lookbackThreshold' system seems pretty brittle. Perhaps we could instead ignore any inputReports that occured *before* the last outputReport or do a FIFO queue or something like that? TODO: Perhaps ask cvuchener.
        - Some of the code is a bit stupid, with the excessive comments, sizeof(uint8_t), etc. Perhaps we should look at the rest of the codebase and adopt the style. (But ask cvuchener before wasting time.)
        - CFRunLoopRunInMode has a 1000 second timeout. We don't want a timeout. Use CFRunLoopRun instead.
        - We use an IORegistryEntryID as the device 'path', but IIRC we format it differently than Apple's `hidutil list`. Formatting it the same would be neat.
        - There are some unsolved TODOs in the code – Mostly around error handling. TODO: Ask cvuchener, how to deal with that or copy the approach you see in the rest of the codebase.
        - Commit logs are full of random notes, so we need to squash them or something before making a pull request.
    
    Also see:
        - Apple Docs - Introduction to Accessing Hardware From Applications: https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/AccessingHardware/AH_Intro/AH_Intro.html#//apple_ref/doc/uid/TP40002714
        - The 'macOS support' issue on cvuchener/hidpp: https://github.com/cvuchener/hidpp/issues/17

    ---

    Investigation: solaar [Mar 6 2025]

        Background:
            Solaar is a python GUI app which lets you configure HIDPP devices. 
            I could run it on my Mac after manually creating a python venv and installing some of the dependencies.
            It successfully configures my Logitech devices!

        Looking at 'macOS support' Issue (https://github.com/pwr-Solaar/Solaar/issues/1244)

            Low-level backend solutions mentioned:

            - libusb
                - They have a very detailed and recently updated darwin backend. (https://github.com/libusb/libusb/blob/master/libusb/os/darwin_usb.c)
                - They use (different versions of) IOUSBInterfaceInterface, and functions like ReadStreamsPipeAsyncTO, WriteStreamsPipeAsyncTO.
                -> Problem is this probably doesn't support Bluetooth devices.
                    - However, in examples/xusb.c/display_ps3_status() they do seem to be handling a bluetooth PS3 controller. No clue how that works or if it would be applicable to the hidpp macOS port.
            - libudev
                - Is deprecated according to https://man.archlinux.org/man/libudev.3.en
                - There's a python wrapper: pyudev

            Low-level backend solutions that are mentioned to **not work**
            - udev
            -> AFAIK this just refers to the native POSIX way of accessing devices via files inside /user/dev/, but it doesn't work on Windows and macOS. (Discussed above.)

        Looking at 'partial macOS support' pull request (https://github.com/pwr-Solaar/Solaar/pull/1971)

            - They implemented hidapi as their backend.
            -> This seems really good! See below.
        
    Investigation: hidapi (https://github.com/libusb/hidapi)
        - hidapi has a very detailed and recently-updated macOS backend.
            - The entire macOS implementation is a single 1500 line c file. It seems very simple and lightweight. 
                (Linux and windows implementations look similarly lightweight, and it also supports many other platforms.)
            - The implementation looks simple and very well-written.
            - They use IOHIDDevice and a separate thread with a runLoop and stuff – just like our hidpp implementation. It looks very similar. (Perhaps we even copied some of the logic (It's been years, I can't remember exactly.))
                - From what I can see, hidapi supports all the semantics we need for hidpp. 
                - It seems like a pretty 'complete' implementation that is better than our implementation in every way. 
                    - E.g. we planned to replace the `lookBackThreshold` system with an inputReportQueue, and they've already implemented that. [Mar 2025]
                    - E.g. they have code for robustly handling device-detachment mid operation. Our code didn't consider that at all. 
                    - E.g. hidapi lets you specify the blocking behavior of reads, as well as whether to get 'exclusive access' over the device on macOS.
        - Sidenote: I remember I tried hidapi in the very beginning of developing Mac Mouse Fix, but it always 'seized' the mice, causing them to stop working normally. However, hidapi seems to have fixed that now! (In commit https://github.com/libusb/hidapi/commit/05f05882203d10e67dbd899d8d985888ff72eca6)

    Other libraries
        libratbag
            - Also supports non-logitech devices.
            - Doesn't support macOS, has no plans to add it IIRC. 
            - Relatively slow to add device support / hid++ protocol featuers. See G Pro X Superlight 2 support which has been request over a year ago: https://github.com/libratbag/libratbag/issues/1572
        solaar
            - Not a library and all in python, so perhaps hard to integrate their code into MMF. But has pretty decent device support.
        hidpp
            - Best solution in theory. There should be a cross-platform c library for logitech devices. 
                (Would be even better if there was one lib to support devices from various manufacturers cross-platform, kinda like libratbag.)
            - Not sure how good device-support / hid++ feature support is specifically.

    Conclusion:
        I think the hidpp macOS backend would basically just be a (shittier) cpp reimplementation of hidapi.
        It looks to me like throwing away the custom backends in hidpp and replacing them with hidapi would be the best solution.
            TODO: Ask @cvuchener about this.
        ORRR: We could implement a new lib in pure C...
        
    Investigation: Replace hidpp with pure C lib?
        I've looked at more of the hidpp source code, and it seems to be relatively complicated and abstracted cpp code with suboptimal abstractions for app development.
        
        This might warrant trying to reimplement in pure C on top of hiapi. It might be nice to have a simple, portable, lightweight reference implementation in C for the hidpp protocol that can be reused in several applications or CLTs. Similar to hidapi or cmark.
        Note: We could perhaps copy some of the logic from the solaar hidpp python code which might be simpler (but I haven't confirmed that)

        To come to the conclusion that cvuchener/hidpp might be a bit abstracted/suboptimal for application development, I looked at:
            - hidpp20-raw-touchpad-driver.cpp and saw that the control flow is relatively abstract and complicated, with multpile constructors, classes, inheritance levels. It also depends on DispatcherThread.cpp
            - Dispatcher.cpp and its subclass DispatcherThread.cpp
                - provide multithreading, request-response-matching, and some report parsing. 
                - These are at the core of the library and provide main interface for interacting with devices I think.
                - Since these use advanced CPP features like Promises/Futures (instead of callbacks), you can't use these in simple C/Objective-C code.
                - They manually matches responses to requests, which is relatively complicated.
                    - This should only be necessary if responses are received out-of-order I think? Can that happen? 
                    - I looked at the 'hidpp 2.0 draft specification' (See below) and couldn't see anything about out-of-order responses.
                    - Only reason I can come up with for out-of-order responses: is having multiple devices attached via a Logitech Unifying Receiver and they're all being communicated with simultaneously from different threads or something? 
                        - But even then, I'm not sure this is the best abstraction – why not provide a 'unifyingReceiverID' property and make it the user's responsibility to ensure the unifying receiver is only being interacted with from one thread?
                        - Edit: Well, if one device responds slower than the other, then we might have a problem... Maybe the request-response matching does make sense for unifying receivers.
                        - Edit 2: Looking at solaar's implementation at `lib > logitech_receiver > base.py > request()`, their solution actually seems more hacky and complicated than hidpp's. 
                            Maybe we shouldn't dismiss hidpp solution so easily, cvuchener does seem to have very deep understanding of this domain.

        Question: Is hidapi powerful enough to build a fully-featured hidpp driver on top? 
            - I think so. These vendor-specific protocols seem built on top of the basic hid protocol which I think hidapi fully supports. 
            - Solaar is also built on hidapi.
            - Full USB protocol has some extra features, but those wouldn't work anyways when connecting devices over bluetooth I think. 
            - See https://stackoverflow.com/questions/55272593/hidapi-vs-libusb-for-linux.
            - @YouW said that reports from the Generic, Mouse, and Keyboard USB usage page cannot be accessed under Windows via hidapi. Is that perhaps why cvuchener/hidpp is using a custom backend?
                Source: https://github.com/libusb/hidapi/issues/725#issuecomment-2708187965

        Name ideas: 
            - chidpp
            - libhid-logitech
                - Later we could make: libhid-razer
            - libhidpp
            - hidapi-logitech
        
        Most amazing thing would be if there's some Open-Source maintenance for the library.
            - What if the libusb project adopts it, just like they did hidapi? A man can dream.

        Library design idea: pure-report-parser [Mar 9]
            - I think the best design would be to purely provide parsing/creation functions for HID reports that we send to/receive from the device.
            - Then, the user would have full flexibility to decide how they wanna send/receive those reports. Be it hidapi, IOHIDDevice, or some other mechanism.
            - Possible problems:
                - Afaik, HIDPP responses from the device could arrive out-of-order (at least with a Logitech Unifying receiver), so reports must be programmatically matched against requests. (I believe this is what hidppp's DispatcherThread.cpp is for)
                  Our report-only library couldn't abstract this away completely, it could only provide a function like `bool hidpp_response_matches_request(request, response)`. 
                  Yapping:
                    Therefore, the client application would have to do all the work of setting up an eventLoop that reads all the reports and dispatches response-reports to their request's callback, while dispatching other reports to some different destination. (Or something like that.)
                    On the other hand, if your application either doesn't do input monitoring, or doesn't do requests, then this stuff should be simpler. 
                    Perhaps we could add sample .c files for these patterns into the library. 


    Also see:
        - This MMF issue where a user pointed me to solaar: https://github.com/noah-nuebling/mac-mouse-fix/issues/1277
        - References linked to by the hidpp readme (https://github.com/cvuchener/hidpp/)
            - 'hidpp 2.0 draft specification' from [2012 06 04] https://lekensteyn.nl/files/logitech/logitech_hidpp_2.0_specification_draft_2012-06-04.pdf
*/

#include "RawDevice.h"

#include <misc/Log.h>

#include <stdexcept>
#include <locale>
#include <codecvt>
#include <array>
#include <map>
#include <set>
#include <cstring>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>
#include "macos/Utility_macos.h"

using namespace HID;

extern "C" { // This needs to be declared after `using namespace HID` for some reason.
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
}

// Pimpl

struct RawDevice::PrivateImpl
{

    // Attributes

    IOHIDDeviceRef iohidDevice;
    CFIndex maxInputReportSize;
    CFIndex maxOutputReportSize;

    dispatch_queue_t inputQueue;

    static void nullifyValues(RawDevice *dev) {
        dev->_p->iohidDevice = nullptr;
        dev->_p->maxInputReportSize = 0;
        dev->_p->maxOutputReportSize = 0;
        dev->_p->inputQueue = nullptr;

        dev->_p->initState(dev);
    }

    // State

    CFRunLoopRef inputReportRunLoop;
    std::vector<uint8_t> lastInputReport; // Can't make this atomic, use explicit mutexes to protect this instead

    std::atomic<bool> ignoreNextRead;
    std::atomic<bool> waitingForInput; 
    std::atomic<double> lastInputReportTime; 
    std::atomic<bool> waitingForInputWasInterrupted; 
    std::atomic<bool> inputRunLoopDidStart;

    static void initState(RawDevice *dev) {

        dev->_p->inputReportRunLoop = nullptr;
        dev->_p->lastInputReport = std::vector<uint8_t>();

        dev->_p->ignoreNextRead = false;
        dev->_p->waitingForInput = false;
        dev->_p->lastInputReportTime = -1; 
        dev->_p->waitingForInputWasInterrupted = false;
        dev->_p->inputRunLoopDidStart = false;
    }

    // Concurrency

    std::mutex generalLock;
    std::condition_variable shouldStopWaitingForInputSignal;
    std::condition_variable inputRunLoopDidStartSignal;

    // Dispatch queue config

    static dispatch_queue_attr_t getInputReportQueueAttrs() {
        return dispatch_queue_attr_make_with_qos_class(NULL, QOS_CLASS_USER_INITIATED, -1);
    }
    static std::string getInputReportQueueLabel(RawDevice *dev) {
        const char *prefix = "com.cvuchener.hidpp.input-reports.";
        std::string debugID = Utility_macos::IOHIDDeviceGetDebugIdentifier(dev->_p->iohidDevice);
        char queueLabel[strlen(prefix) + strlen(debugID.c_str())];
        sprintf(queueLabel, "%s%s", prefix, debugID.c_str());
        return std::string(queueLabel);
    }

    // Reset input buffer

    static void deleteInputBuffer(RawDevice *dev) {
        dev->_p->lastInputReport = std::vector<uint8_t>();
        dev->_p->lastInputReportTime = 0;
    }

    // Read input reports
    //  Read thread should be active throughout the lifetime of this RawDevice

    static void stopReadThread(RawDevice *dev) {
        CFRunLoopStop(dev->_p->inputReportRunLoop);
    }
    static void readThread(RawDevice *dev) {

        // Does this:
        // - Read input reports 
        // - Store result into lastInputReport
        // - Send notifications that there is new report (via std::condition_variable)
        // Discussion:
        // - This function is blocking. The thread that calls it becomes the read thread until stopReadThread() is called from another thread.

        // Convenience
        RawDevice::PrivateImpl *_p = dev->_p.get();

        // Setup reportBuffer
        CFIndex reportBufferSize = _p->maxInputReportSize;
        uint8_t *reportBuffer = (uint8_t *) malloc(reportBufferSize * sizeof(uint8_t));
        memset(reportBuffer, 0, reportBufferSize); // Init with 0s

        // Setup report callback
        //  IOHIDDeviceGetReportWithCallback has a built-in timeout and might make for more straight forward code
        //      but Apple docs say it should only be used for feature reports. So we're using 
        //      IOHIDDeviceRegisterInputReportCallback instead.
        IOHIDDeviceRegisterInputReportCallback(
            _p->iohidDevice, 
            reportBuffer, // This will get filled when an inputReport occurs
            _p->maxInputReportSize,
            [] (void *context, IOReturn result, void *sender, IOHIDReportType type, uint32_t reportID, uint8_t *report, CFIndex reportLength) {
                
                //  Get dev from context
                //  ^ We can't capture `dev` or anything else, because then the enclosing lambda wouldn't decay to a pure c function
                RawDevice *devvv = static_cast<RawDevice *>(context);

                // Lock
                std::unique_lock lock(devvv->_p->generalLock);

                // Debug
                Log::debug() << "Received input from device " << Utility_macos::IOHIDDeviceGetDebugIdentifier(devvv->_p->iohidDevice) << std::endl;

                // Store new report
                devvv->_p->lastInputReport.assign(report, report + reportLength);
                devvv->_p->lastInputReportTime = Utility_macos::timestamp();

                // Notify waiting thread
                devvv->_p->shouldStopWaitingForInputSignal.notify_one(); // We assume that there's only one waiting thread

            }, 
            dev // Pass `dev` to context
        );

        // Start runLoop

        // Params

        CFRunLoopMode runLoopMode = kCFRunLoopDefaultMode;

        // Store current runLoop      

        _p->inputReportRunLoop = CFRunLoopGetCurrent();

        // Add IOHIDDevice to runLoop.

        //  Async callbacks for this IOHIDDevice will be delivered to this runLoop
        //  We need to call this before CFRunLoopRun, because if the runLoop has nothing to do, it'll immediately exit when we try to run it.
        IOHIDDeviceScheduleWithRunLoop(_p->iohidDevice, _p->inputReportRunLoop, runLoopMode);

        // Debug
        
        Log::debug() << "Starting inputRunLoop on device " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;

        // Observer

        CFRunLoopObserverContext ctx = {
            .version = 0,
            .info = dev,
            .retain = NULL,
            .release = NULL,
            .copyDescription = NULL,
        };

        CFRunLoopObserverRef observer = CFRunLoopObserverCreate(
            kCFAllocatorDefault, 
            kCFRunLoopEntry | kCFRunLoopExit, 
            false, 
            0, 
            [](CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *info){
                RawDevice *devvv = (RawDevice *)info;

                if (activity == kCFRunLoopEntry) {
                    devvv->_p->inputRunLoopDidStart = true;
                    devvv->_p->inputRunLoopDidStartSignal.notify_one();
                } else {
                    devvv->_p->inputRunLoopDidStart = false; // Not sure if useful
                }
            }, 
            &ctx
        );

        CFRunLoopAddObserver(_p->inputReportRunLoop, observer, runLoopMode);

        // Start runLoop
        //  Calling this blocks this thread and until the runLoop exits.
        // See HIDAPI https://github.com/libusb/hidapi/blob/master/mac/hid.c for reference

        while (true) {

            // Run runLoop

            CFRunLoopRunResult runLoopResult = CFRunLoopRunInMode(runLoopMode, 1000 /*sec*/, false); 

            // Analyze runLoop exit reason

            std::string runLoopResultString;
            if (runLoopResult == kCFRunLoopRunFinished) {
                runLoopResultString = "Finished";
            } else if (runLoopResult == kCFRunLoopRunHandledSource) {
                runLoopResultString = "HandledSource";
            } else if (runLoopResult == kCFRunLoopRunStopped) {
                runLoopResultString = "Stopped";
            } else if (runLoopResult == kCFRunLoopRunTimedOut) {
                runLoopResultString = "TimedOut";
            } else {
                runLoopResultString = "UnknownResult";
            }
            Log::debug() << "inputReportRunLoop exited with result: " << runLoopResultString << std::endl;

            // Exit condition

            if (runLoopResult == kCFRunLoopRunFinished // Not sure if this makes sense. I've never seen the result be `finished`
                || runLoopResult == kCFRunLoopRunStopped) { 
                break;
            }

            Log::debug() << "Restarting runloop" << std::endl;
        }

        // Free reportBuffer
        free(reportBuffer);

        // Tear down runLoop 
        //  Edit: disabling for now since it accesses _p which can lead to crash when RunLoopStop() is called from the deconstructor
        
        return;

        Log::debug() << "Tearing down runloop" << std::endl;

        // Unregister input report callback
        //  This is probably unnecessary
        uint8_t nullReportBuffer[0];
        CFIndex nullReportLength = 0;
        IOHIDDeviceRegisterInputReportCallback(_p->iohidDevice, nullReportBuffer, nullReportLength, NULL, NULL); 
        //  ^ Passing NULL for the callback unregisters the previous callback.
        //      Not sure if redundant when already calling IOHIDDeviceUnscheduleFromRunLoop.

        // Remove device from runLoop
        //  This is probably unnecessary since we're already stopping the runLoop via CFRunLoopStop()
        IOHIDDeviceUnscheduleFromRunLoop(_p->iohidDevice, _p->inputReportRunLoop, kCFRunLoopCommonModes);
    }

};


// Private constructor

RawDevice::RawDevice() 
    : _p(std::make_unique<PrivateImpl>())
{

}

// Copy constructor

RawDevice::RawDevice(const RawDevice &other) : _p(std::make_unique<PrivateImpl>()),
                                               _vendor_id(other._vendor_id), _product_id(other._product_id),
                                               _name(other._name),
                                               _report_desc(other._report_desc)
{

    // Don't use this
    //  I don't think it makes sense for RawDevice to be copied, since it's a wrapper for a real physical device that only exists once.
    std::__throw_bad_function_call();
    
    // Lock
    std::unique_lock lock1(_p->generalLock);
    std::unique_lock lock2(other._p->generalLock); // Probably not necessary

    // Copy attributes from `other` to `this`

    io_service_t service = IOHIDDeviceGetService(other._p->iohidDevice);
    _p->iohidDevice = IOHIDDeviceCreate(kCFAllocatorDefault, service);
    // ^ Copy iohidDevice. I'm not sure this way of copying works
    _p->maxInputReportSize = other._p->maxInputReportSize;
    _p->maxOutputReportSize = other._p->maxOutputReportSize;
    _p->inputQueue = dispatch_queue_create(_p->getInputReportQueueLabel(this).c_str(), _p->getInputReportQueueAttrs());

    // Reset state
    _p->initState(this);
}

// Move constructor

RawDevice::RawDevice(RawDevice &&other) : _p(std::make_unique<PrivateImpl>()),
                                          _vendor_id(other._vendor_id), _product_id(other._product_id),
                                          _name(std::move(other._name)),
                                          _report_desc(std::move(other._report_desc))
{
    // How to write move constructor: https://stackoverflow.com/a/43387612/10601702

    // Lock
    std::unique_lock lock1(_p->generalLock);
    std::unique_lock lock2(other._p->generalLock); // Probably not necessary

    // Assign values from `other` to `this` (without copying them)

    _p->iohidDevice = other._p->iohidDevice;
    _p->maxInputReportSize = other._p->maxInputReportSize;
    _p->maxOutputReportSize = other._p->maxOutputReportSize;
    _p->inputQueue = other._p->inputQueue;

    // Init state
    _p->initState(this);

    // Delete values in `other` 
    //  (so that it can't manipulate values in `this` through dangling references)
    other._p->nullifyValues(&other);
}

// Destructor

RawDevice::~RawDevice(){

    // Lock
    std::unique_lock lock(_p->generalLock);

    // Debug
    Log::debug() << "Destroying device " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl; 

    // Stop inputQueue
    _p->stopReadThread(this);

    // Close IOHIDDevice
    IOHIDDeviceClose(_p->iohidDevice, kIOHIDOptionsTypeNone); // Not sure if necessary
    CFRelease(_p->iohidDevice); // Not sure if necessary
}

// Main constructor

RawDevice::RawDevice(const std::string &path) : _p(std::make_unique<PrivateImpl>()) {
    // Construct device from path

    // Lock
    //  This only unlocks once the readThread has set up its runLoop
    std::unique_lock lock(_p->generalLock);

    // Init pimpl
    _p->initState(this);

    // Declare vars
    kern_return_t kr;
    IOReturn ior;

    // Convert path to registryEntryId (int64_t)

    // uint64_t entryID = std::stoll(path);
    uint64_t entryID = std::strtoull(path.c_str() + strlen(Utility_macos::pathPrefix.c_str()), NULL, 10);

    CFDictionaryRef matchDict = IORegistryEntryIDMatching(entryID);
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault, matchDict);
    if (service == 0) { // Idk what this returns when it can't find a matching service

    }

    // Create IOHIDDevice from service
    IOHIDDeviceRef device = IOHIDDeviceCreate(kCFAllocatorDefault, service);

    // Store device
    _p->iohidDevice = device;

    // Open device
    ior = IOHIDDeviceOpen(_p->iohidDevice, kIOHIDOptionsTypeNone); // Necessary to change the state of the device
    if (ior != kIOReturnSuccess) {
        Log::warning() << "Opening the device \"" << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << "\" failed with error code " << ior << std::endl;
        
    } else {
        // Log::info().printf("Opening the device \"%s\" succeded", Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice));
        // ^ printf() function on Log doesn't work properly here for some reason. Shows up in vscode debug console but not in Terminal, even with the -vdebug option. Logging with the << syntax instead of printf works though. In Utility_macos the printf function works for some reason. TODO: Ask Clément about this.
        Log::info() << "Opening the device \"" << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << "\" succeded" << std::endl;
    }

    // Store IOHIDDevice in self
    _p->iohidDevice = device;

    // Fill out public member variables

    _vendor_id = Utility_macos::IOHIDDeviceGetIntProperty(device, CFSTR(kIOHIDVendorIDKey));
    _product_id = Utility_macos::IOHIDDeviceGetIntProperty(device, CFSTR(kIOHIDProductIDKey));
    _name = Utility_macos::IOHIDDeviceGetStringProperty(device, CFSTR(kIOHIDProductKey));

    try { // Try-except copied from the Linux implementation
        _report_desc = Utility_macos::IOHIDDeviceGetReportDescriptor(device);
        logReportDescriptor();
    } catch (std::exception &e) {
        Log::error () << "Invalid report descriptor: " << e.what () << std::endl;
    }

    // Fill out private member variables
    _p->maxInputReportSize = Utility_macos::IOHIDDeviceGetIntProperty(device, CFSTR(kIOHIDMaxInputReportSizeKey));
    _p->maxOutputReportSize = Utility_macos::IOHIDDeviceGetIntProperty(device, CFSTR(kIOHIDMaxOutputReportSizeKey));

    // Create dispatch queue
    _p->inputQueue = dispatch_queue_create(_p->getInputReportQueueLabel(this).c_str(), _p->getInputReportQueueAttrs());

    // Start listening to input on queue
    dispatch_async_f(_p->inputQueue, this, [](void *context) {
        RawDevice *thisss = (RawDevice *)context;
        thisss->_p->readThread(thisss);
    });

    // Wait until inputReportRunLoop Started
    while (!_p->inputRunLoopDidStart) {
        _p->inputRunLoopDidStartSignal.wait(lock);
    }

    // Debug
    Log::debug() << "Constructed device " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;
}

// Interface

// writeReport
//  See https://developer.apple.com/library/archive/technotes/tn2187/_index.html
//      for info on how to use IOHID input/output report functions and more.

int RawDevice::writeReport(const std::vector<uint8_t> &report)
{

    // Lock
    std::unique_lock lock(_p->generalLock);

    // Debug
    Log::debug() << "writeReport called on " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;

    // Reset input buffer
    //  So we don't read after this expecting a response, but getting some old value from the buffer
    _p->deleteInputBuffer(this);

    // Guard report size
    if (report.size() > _p->maxOutputReportSize) {
        // TODO: Return meaningful error
        return 1;
    }

    // Gather args for report sending
    IOHIDDeviceRef device = _p->iohidDevice;
    IOHIDReportType reportType = kIOHIDReportTypeOutput;
    CFIndex reportID = report[0];
    const uint8_t *rawReport = report.data();
    CFIndex reportLength = report.size();

    // Send report
    IOReturn r = IOHIDDeviceSetReport(device, reportType, reportID, rawReport, reportLength);

    // Return error code
    if (r != kIOReturnSuccess) {
        // TODO: Return some meaningful error code
        return r;
    }

    // Return success
    return 0;
}

// readReport

int RawDevice::readReport(std::vector<uint8_t> &report, int timeout) {

    // Lock
    std::unique_lock lock(_p->generalLock);

    // Debug
    Log::debug() << "readReport called on " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;

    // Interrupt read
    if (_p->ignoreNextRead) {

        // Reset input buffer
        //  So we don't later return the value that's queued up now. Not sure if necessary.
        _p->deleteInputBuffer(this);

        // Reset ignoreNextRead flag
        _p->ignoreNextRead = false;

        // Return invalid
        return 0;
    }

    // Define constant
    double lookbackThreshold = 1 / 100.0; // If a report occured no more than `lookbackThreshold` seconds ago, then we use it.

    // Preprocess timeout
    double timeoutSeconds;
    if (timeout < 0) {
        // Disable timeout if negative
        timeoutSeconds = INFINITY;
    } else {
        // Convert timeout to seconds instead of milliseconds
        timeoutSeconds = timeout / 1000.0;
    }

    // Wait for input
    //  Block this thread until the next inputReport is issued
    //  We only stop waiting if one of these happens
    //      - Device sends input report
    //      - Timeout happens
    //      - interruptRead() is called
    //
    //  Use _p->lastInputReport instead of waiting - if it's been less than "lookbackThreshold" since the lastInputReport occured
    //      The hidpp library expects this function to only receive inputReports that are issued after we start reading. But sometimes starting to read takes a fraction too long making us miss events.
    //      I think every mouse movement creates input reports - can that lead to problems?

    double lastInputReportTimeBeforeWaiting = _p->lastInputReportTime;

    if ((Utility_macos::timestamp() - lastInputReportTimeBeforeWaiting) <= lookbackThreshold) {

        // Last received report is still fresh enough. Return that instead of waiting.
        Log::debug() << "Recent event already queued up for device " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;
    } else {
        // Wait for next input report

        // Init loop state
        std::cv_status timeoutStatus = std::cv_status::no_timeout;
        auto timeoutTime = std::chrono::system_clock::now() + std::chrono::duration<double, std::ratio<1>>(timeoutSeconds); // Point in time until which to wait for input

        // Wait for report in a loop
        while (true) {
            
            // Debug
            Log::debug() << "Wait for device " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;

            // Wait
            _p->waitingForInput = true; // Should only be mutated right here.
            timeoutStatus = _p->shouldStopWaitingForInputSignal.wait_until(lock, timeoutTime);
            _p->waitingForInput = false;

            // Check state
            bool newEventReceived = _p->lastInputReportTime > lastInputReportTimeBeforeWaiting;
            bool timedOut = timeoutStatus == std::cv_status::timeout ? true : false; // ? Possible race condition if the wait is interrupted due to spurious wakeup but then the timeoutTime is exceeded before we reach here. But this is very unlikely and doesn't have bad consequences.
            bool interrupted = _p->waitingForInputWasInterrupted; 

            // Update state
            _p->waitingForInputWasInterrupted = false;

            // Stop waiting
            if (newEventReceived || timedOut || interrupted) {

                // Debug state
                if (newEventReceived + timedOut + interrupted > 1) {
                    Log::warning() << "Waiting for inputReport was stopped with WEIRD state: newReport: " << newEventReceived << " timedOut: " << timedOut << " interrupted: " << interrupted << std::endl;
                } else {
                    Log::debug() << "Waiting for inputReport stopped with state: newReport: " << newEventReceived << " timedOut: " << timedOut << " interrupted: " << interrupted << std::endl;
                }

                // Break loop
                break;
            }
        }
    }

    // Get return values

    int returnValue;

    if ((Utility_macos::timestamp() - _p->lastInputReportTime) <= lookbackThreshold) { 
        // ^ Reading was successful. Not sure if this is the best way to check if reading was successful. It might be more robust than using the `newEventReceived` flag.
        //  Edit: Getting a new timestamp here might lead to race condition if lastInputReport was determined to be "fresh enough" above but now is not "fresh enough" anymore. 

        // Write result to the `report` argument and return length
        report = _p->lastInputReport;
        returnValue = report.size();

    } else { // Reading was interrupted or timedOut
        returnValue = 0;
    }

    // Reset input buffer
    //  Not sure if necessary. Theoretically the `lookbackTreshold` should be enough. Edit: Why would `lookbackTreshold` be enough? Coudn't there be a problem where we read the same report twice, if we don't do this?
    _p->deleteInputBuffer(this);

    // Return
    return returnValue;
}

void RawDevice::interruptRead() {

    std::unique_lock lock(_p->generalLock);

    // Debug
    Log::debug() << "interruptRead called on " << Utility_macos::IOHIDDeviceGetDebugIdentifier(_p->iohidDevice) << std::endl;

    // Do stuff

    if (_p->waitingForInput) {

        // Stop readReport() from waiting, if it's waiting
        _p->waitingForInputWasInterrupted = true;
        _p->shouldStopWaitingForInputSignal.notify_one();

    } else {
        _p->ignoreNextRead = true;
        // ^ If readReport() is not currently waiting, we ignore the next call to readReport()
        //      This is the expected behaviour according to https://github.com/cvuchener/hidpp/issues/17#issuecomment-896821785
    }
}
