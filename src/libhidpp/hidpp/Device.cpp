/*
 * Copyright 2015 Clément Vuchener
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

#include <hidpp/Device.h>

#include <hidpp20/IRoot.h>

#include <algorithm>

using namespace HIDPP;

Device::NoHIDPPReportException::NoHIDPPReportException ()
{
}

const char *Device::NoHIDPPReportException::what () const noexcept
{
	return "No HID++ report";
}

static const std::array<uint8_t, 27> ShortReportDesc = {
	0x06, 0x00, 0xFF,	// Usage Page (FF00 - Vendor)
	0x09, 0x01,		// Usage (0001 - Vendor)
	0xA1, 0x01,		// Collection (Application)
	0x85, 0x10,		//   Report ID (16)
	0x75, 0x08,		//   Report Size (8)
	0x95, 0x06,		//   Report Count (6)
	0x15, 0x00,		//   Logical Minimum (0)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x09, 0x01,		//   Usage (0001 - Vendor)
	0x81, 0x00,		//   Input (Data, Array, Absolute)
	0x09, 0x01,		//   Usage (0001 - Vendor)
	0x91, 0x00,		//   Output (Data, Array, Absolute)
	0xC0			// End Collection
};

static const std::array<uint8_t, 27> LongReportDesc = {
	0x06, 0x00, 0xFF,	// Usage Page (FF00 - Vendor)
	0x09, 0x02,		// Usage (0002 - Vendor)
	0xA1, 0x01,		// Collection (Application)
	0x85, 0x11,		//   Report ID (17)
	0x75, 0x08,		//   Report Size (8)
	0x95, 0x13,		//   Report Count (19)
	0x15, 0x00,		//   Logical Minimum (0)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x09, 0x02,		//   Usage (0002 - Vendor)
	0x81, 0x00,		//   Input (Data, Array, Absolute)
	0x09, 0x02,		//   Usage (0002 - Vendor)
	0x91, 0x00,		//   Output (Data, Array, Absolute)
	0xC0			// End Collection
};

/* Alternative versions from the G602 */
static const std::array<uint8_t, 27> ShortReportDesc2 = {
	0x06, 0x00, 0xFF,	// Usage Page (FF00 - Vendor)
	0x09, 0x01,		// Usage (0001 - Vendor)
	0xA1, 0x01,		// Collection (Application)
	0x85, 0x10,		//   Report ID (16)
	0x95, 0x06,		//   Report Count (6)
	0x75, 0x08,		//   Report Size (8)
	0x15, 0x00,		//   Logical Minimum (0)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x09, 0x01,		//   Usage (0001 - Vendor)
	0x81, 0x00,		//   Input (Data, Array, Absolute)
	0x09, 0x01,		//   Usage (0001 - Vendor)
	0x91, 0x00,		//   Output (Data, Array, Absolute)
	0xC0			// End Collection
};

static const std::array<uint8_t, 27> LongReportDesc2 = {
	0x06, 0x00, 0xFF,	// Usage Page (FF00 - Vendor)
	0x09, 0x02,		// Usage (0002 - Vendor)
	0xA1, 0x01,		// Collection (Application)
	0x85, 0x11,		//   Report ID (17)
	0x95, 0x13,		//   Report Count (19)
	0x75, 0x08,		//   Report Size (8)
	0x15, 0x00,		//   Logical Minimum (0)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x09, 0x02,		//   Usage (0002 - Vendor)
	0x81, 0x00,		//   Input (Data, Array, Absolute)
	0x09, 0x02,		//   Usage (0002 - Vendor)
	0x91, 0x00,		//   Output (Data, Array, Absolute)
	0xC0			// End Collection
};

Device::Device (const std::string &path, DeviceIndex device_index):
	HIDRaw (path), _device_index (device_index)
{
	const HIDRaw::ReportDescriptor &rdesc = getReportDescriptor ();
	if (rdesc.end () == std::search (rdesc.begin (), rdesc.end (),
					 ShortReportDesc.begin (),
					 ShortReportDesc.end ()) &&
	    rdesc.end () == std::search (rdesc.begin (), rdesc.end (),
					 ShortReportDesc2.begin (),
					 ShortReportDesc2.end ()))
		throw NoHIDPPReportException ();
	if (rdesc.end () == std::search (rdesc.begin (), rdesc.end (),
	    				 LongReportDesc.begin (),
					 LongReportDesc.end ()) &&
	    rdesc.end () == std::search (rdesc.begin (), rdesc.end (),
	    				 LongReportDesc2.begin (),
					 LongReportDesc2.end ())) 
		throw NoHIDPPReportException ();
	
}

DeviceIndex Device::deviceIndex () const
{
	return _device_index;
}

void Device::getProtocolVersion (unsigned int &major, unsigned int &minor)
{
	constexpr int software_id = 1; // Must be a 4 bit unsigned value
	Report request (Report::Short,
			_device_index,
			HIDPP20::IRoot::index,
			HIDPP20::IRoot::Ping,
			software_id);
	sendReport (request);
	while (true) {
		Report response = getReport ();

		if (response.deviceIndex () != _device_index)
			continue;

		uint8_t sub_id, address, error_code;
		if (response.checkErrorMessage10 (&sub_id, &address, &error_code)) {
			if (sub_id != HIDPP20::IRoot::index ||
			    address != (HIDPP20::IRoot::Ping << 4 | software_id))
				continue;
			major = 1;
			minor = 0;
			return;
		}
		if (response.featureIndex () == HIDPP20::IRoot::index &&
		    response.function () == HIDPP20::IRoot::Ping &&
		    response.softwareID () == software_id) {
			major = response.params ()[0];
			minor = response.params ()[1];
			return;
		}
	}
}

int Device::sendReport (const Report &report)
{
	return writeReport (report.rawReport ());
}

Report Device::getReport ()
{
	std::vector<uint8_t> raw_report;
	while (true) {
		try {
			raw_report.resize (Report::MaxDataLength+1);
			readReport (raw_report);
			return Report (raw_report[0], &raw_report[1], raw_report.size () - 1);
		}
		catch (Report::InvalidReportID e) {
			// Ignore non-HID++ reports
			continue;
		}
		catch (Report::InvalidReportLength e) {
			// Ignore non-HID++ reports
			continue;
		}
	}
}
