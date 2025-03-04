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

#include "DeviceMonitor.h"

#include <misc/Log.h>

#include <string>
#include <thread>

#include "macos/Utility_macos.h"

extern "C" {
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOKitLib.h>
}

using namespace HID;

// Private

struct DeviceMonitor::PrivateImpl
{
	IOHIDManagerRef manager;
	CFRunLoopRef managerRunLoop;
};

// Constructor & Destructor

DeviceMonitor::DeviceMonitor ():
	_p (std::make_unique<PrivateImpl> ())
{

	// Create manager
	_p->manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

	// Match all devices
	IOHIDManagerSetDeviceMatching(_p->manager, NULL);

	// Setup device matching callback
	IOHIDManagerRegisterDeviceMatchingCallback(
		_p->manager, 
		[] (void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
			// Get this
			DeviceMonitor *thisss = static_cast<DeviceMonitor *>(context);
			// Get path
			std::string path = Utility_macos::IOHIDDeviceGetPath(device);
			// Add device
			thisss->addDevice(path.c_str());
		}, 
		this
	);

	// Setup device removal callback
	IOHIDManagerRegisterDeviceRemovalCallback(
		_p->manager, 
		[] (void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
			// Get this
			DeviceMonitor *thisss = static_cast<DeviceMonitor *>(context);
			// Get path
			std::string path = Utility_macos::IOHIDDeviceGetPath(device);
			// Remove device
			thisss->removeDevice(path.c_str());
		}, 
		NULL
	);

	// Callbacks won't be active until IOHIDManagerScheduleWithRunLoop() is called
}

DeviceMonitor::~DeviceMonitor () {

	// Unregister device added / removed callbacks. 
	//	Not sure if necessary
	IOHIDManagerRegisterDeviceMatchingCallback(_p->manager, NULL, NULL);
	IOHIDManagerRegisterDeviceRemovalCallback(_p->manager, NULL, NULL);
	// Stop
	stop();
	// Release
	// 	Not sure if necessary
	CFRelease(_p->manager);
}

// Interface

void DeviceMonitor::enumerate () {

	// Get C array of devices attached to the manager

	CFSetRef devices = IOHIDManagerCopyDevices(_p->manager);

	CFIndex count = CFSetGetCount(devices);
	IOHIDDeviceRef deviceArray[count];

	CFSetGetValues(devices, (const void **)deviceArray);

	// Add all found devices

	for (int i = 0; i < count; ++i) {

		// Get device
		IOHIDDeviceRef device = deviceArray[i];
		// Get path
		std::string path = Utility_macos::IOHIDDeviceGetPath(device);
		// Add
		addDevice(path.c_str());
	}
}

void DeviceMonitor::run () {

	// Call addDevice() on all currently attached devices
	enumerate();

	// Store runLoop
	this->_p->managerRunLoop = CFRunLoopGetCurrent();

	// Add device manager to runLoop
	IOHIDManagerScheduleWithRunLoop(this->_p->manager, this->_p->managerRunLoop, kCFRunLoopCommonModes); 
	// 	^ Matching and removal callbacks defined in constructor will now be active

	// Run runLoop
	//	This will block the current thread until it exits
	CFRunLoopRun();

	// Cleanup runLoop after it exits

	// Stop monitor
	IOHIDManagerUnscheduleFromRunLoop(_p->manager, _p->managerRunLoop, kCFRunLoopCommonModes); 
	// 	^ Deactivates matching and removal callbacks defined in constructor

	// Set runLoop to NULL after it exits
	this->_p->managerRunLoop = NULL;
}

void DeviceMonitor::stop () {

	// Force-stop the runLoop
	// 	Probably unnecessary. The runLoop should stop automatically after we unschedule the manager, because it won't have anything to do.
	if (_p->managerRunLoop != NULL) { 
		CFRunLoopStop(_p->managerRunLoop);
	}
}
