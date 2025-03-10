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


#include "Utility_macos.h"
#include <chrono>
#include "../../misc/Log.h"

extern "C" {
    #include <stdbool.h>
}

// Convert Cocoa -> Cpp

long Utility_macos::CFNumberToInt(CFNumberRef cfNumber) {

    long result;

    CFNumberType numberType = CFNumberGetType(cfNumber);
    bool success = CFNumberGetValue(cfNumber, numberType, &result);

    if (!success) {
        // TODO: Throw error or something
    }

    return result;
}

std::string Utility_macos::CFStringToString(CFStringRef cfString) {

    CFStringEncoding encoding = kCFStringEncodingUTF8;

    CFIndex bufSize = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfString), encoding) + 1;
    char buffer[bufSize];
    bool success = CFStringGetCString(cfString, buffer, bufSize, encoding);

    if (!success) {
        // TODO: Throw error
    }

    return std::string(buffer);
}

std::vector<uint8_t> Utility_macos::CFDataToByteVector(CFDataRef cfData) {

    const uint8_t *bytes = CFDataGetBytePtr(cfData);
    CFIndex length = CFDataGetLength(cfData);

    std::vector<uint8_t> byteVector;
    byteVector.assign(bytes, bytes + length);

    return byteVector;
}

// Convenience wrappers IOHIDDeviceGetProperty

long Utility_macos::IOHIDDeviceGetIntProperty(IOHIDDeviceRef device, CFStringRef key) {

    CFNumberRef cfValue = (CFNumberRef)IOHIDDeviceGetProperty(device, key);

    if (cfValue != NULL) {
        return CFNumberToInt(cfValue);
    } else {
        // Log warning
        Log::warning().printf("Property \"%s\" was NULL on device \"%s\". Using 0 instead.\n", CFStringGetCStringPtr(key, kCFStringEncodingUTF8), IOHIDDeviceGetDebugIdentifier(device).c_str());
        // Return
        return 0;
    }
}

std::string Utility_macos::IOHIDDeviceGetStringProperty(IOHIDDeviceRef device, CFStringRef key) {
    CFStringRef cfValue = (CFStringRef)IOHIDDeviceGetProperty(device, key);
    if (cfValue != NULL) {
        return CFStringToString(cfValue);
    } else {
        // Log warning
        Log::warning().printf("Property \"%s\" was NULL on device \"%s\". Using empty string instead.\n", CFStringGetCStringPtr(key, kCFStringEncodingUTF8), IOHIDDeviceGetDebugIdentifier(device).c_str());
        // Return
        return "";
    }
}

HID::ReportDescriptor Utility_macos::IOHIDDeviceGetReportDescriptor(IOHIDDeviceRef device) {

    CFTypeRef cfValue = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey));
    std::vector<uint8_t> byteVector;

    if (cfValue != NULL) {
        byteVector = CFDataToByteVector((CFDataRef)cfValue);
        return HID::ReportDescriptor::fromRawData(byteVector.data(), byteVector.size());
    } else {
        // Log warning
        Log::warning().printf("Report descriptor was NULL on device \"%s\". Using empty vector instead.\n", IOHIDDeviceGetDebugIdentifier(device).c_str());
        // We want to return an empty report descriptor here. TODO: Make sure `return HID::ReportDescriptor();` works for that.
        return HID::ReportDescriptor();
    }
}

// Other IOHIDDevice helpers

std::string Utility_macos::pathPrefix = /*"dev:"*/ "dev://";

std::string Utility_macos::IOHIDDeviceGetPath(IOHIDDeviceRef device) {

    // Option 3: Use IORegistryEntry ID but prepend "dev:" to make it more clear

    std::string id = Utility_macos::IOHIDDeviceGetUniqueIdentifier(device);
    char result[strlen(pathPrefix.c_str()) + strlen(id.c_str())];
    sprintf(result, "%s%s", pathPrefix.c_str(), id.c_str());
    return std::string(result);

    // Option 2: Use IORegistryEntry ID

    // return Utility_macos::IOHIDDeviceGetUniqueIdentifier(device);

    // Option 1: Use IORegistryEntry path
    //  These paths are way to long and annoying to deal with

    // // Get service
    // io_service_t service = IOHIDDeviceGetService(device);
    // // Get cfPath
    // //  IORegistryEntryCopyPath allows for unlimited path length (not just 500 characters), which avoids crashes, but it's not available on older macOS verisons
    // CFStringRef cfPath = IORegistryEntryCopyPath(service, kIOServicePlane);
    // // Get path
    // const char *path = CFStringGetCStringPtr(cfPath, kCFStringEncodingUTF8);

    // return path;
}

std::string Utility_macos::IOHIDDeviceGetUniqueIdentifier(IOHIDDeviceRef device) {
    // This used to be the device path. Now it's the registryEntryID, because of issues with long paths (see HIDAPI Github issues)

    // Get service
    io_service_t service = IOHIDDeviceGetService(device);
    
    // Get id
    uint64_t id;
    IORegistryEntryGetRegistryEntryID(service, &id);
    // Convert to string
    std::string id_str = std::to_string(id);
    
    // Return
    return id_str;
}
std::string Utility_macos::IOHIDDeviceGetDebugIdentifier(IOHIDDeviceRef device) {
    // Human readable identifier that doesn't clog up the debug output too much
    //  Since we're using the simple and short registryEntryID as the IOHIDDeviceGetUniqueIdentifier, this is now the same as uniqueIdentifier

    return Utility_macos::IOHIDDeviceGetPath(device);
}

// Other

double Utility_macos::timestamp() {
    // Src: https://stackoverflow.com/a/45465312/10601702
    // Returns seconds since January 1st 1970
    //      Why not use CACurrentMediaTime()?
    auto now = std::chrono::system_clock::now();
    auto timeSinceEpoch = std::chrono::duration<double>(now.time_since_epoch()); // Epoch is usually January 1st 1970
    return timeSinceEpoch.count();
}
