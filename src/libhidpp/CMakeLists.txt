cmake_minimum_required(VERSION 3.8)
project(libhidpp)

set(LIBHIDPP_SOURCES
	misc/Log.cpp
	misc/CRC.cpp
	hid/RawDevice.cpp
	hid/RawDevice_${HID_BACKEND}.cpp
	hid/DeviceMonitor_${HID_BACKEND}.cpp
	hid/UsageStrings.cpp
	hid/ReportDescriptor.cpp
	hidpp/Dispatcher.cpp
	hidpp/SimpleDispatcher.cpp
	hidpp/DispatcherThread.cpp
	hidpp/Device.cpp
	hidpp/Report.cpp
	hidpp/DeviceInfo.cpp
	hidpp/Setting.cpp
	hidpp/SettingLookup.cpp
	hidpp/Enum.cpp
	hidpp/Address.cpp
	hidpp/Profile.cpp
	hidpp/Macro.cpp
	hidpp/AbstractProfileFormat.cpp
	hidpp/AbstractMemoryMapping.cpp
	hidpp/AbstractMacroFormat.cpp
	hidpp10/Device.cpp
	hidpp10/Error.cpp
	hidpp10/WriteError.cpp
	hidpp10/IMemory.cpp
	hidpp10/IReceiver.cpp
	hidpp10/IIndividualFeatures.cpp
	hidpp10/Sensor.cpp
	hidpp10/IResolution.cpp
	hidpp10/IProfile.cpp
	hidpp10/ProfileDirectoryFormat.cpp
	hidpp10/ProfileFormat.cpp
	hidpp10/ProfileFormatCommon.cpp
	hidpp10/ProfileFormatG9.cpp
	hidpp10/ProfileFormatG500.cpp
	hidpp10/ProfileFormatG700.cpp
	hidpp10/MemoryMapping.cpp
	hidpp10/RAMMapping.cpp
	hidpp10/MacroFormat.cpp
	hidpp20/Device.cpp
	hidpp20/Error.cpp
	hidpp20/UnsupportedFeature.cpp
	hidpp20/IRoot.cpp
	hidpp20/FeatureInterface.cpp
	hidpp20/IFeatureSet.cpp
	hidpp20/IOnboardProfiles.cpp
	hidpp20/IAdjustableDPI.cpp
	hidpp20/IReprogControlsV4.cpp
	hidpp20/IMouseButtonSpy.cpp
	hidpp20/ITouchpadRawXY.cpp
	hidpp20/ILEDControl.cpp
	hidpp20/IBatteryLevelStatus.cpp
	hidpp20/ProfileDirectoryFormat.cpp
	hidpp20/ProfileFormat.cpp
	hidpp20/MemoryMapping.cpp
	hidpp20/MacroFormat.cpp
)

if("${HID_BACKEND}" STREQUAL "windows")
	set(LIBHIDPP_SOURCES ${LIBHIDPP_SOURCES}
		hid/windows/error_category.cpp
		hid/windows/DeviceData.cpp
	)
elseif("${HID_BACKEND}" STREQUAL "macos")
	set(LIBHIDPP_SOURCES ${LIBHIDPP_SOURCES}
		hid/macos/Utility_macos.cpp
	)
endif()

# include_directories(.)
# add_library(hidpp SHARED ${LIBHIDPP_SOURCES})
add_library(hidpp ${LIBHIDPP_SOURCES})
set_target_properties(hidpp PROPERTIES VERSION 0.2)
target_include_directories(hidpp PUBLIC
	$<INSTALL_INTERFACE:include/hidpp>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

if("${HID_BACKEND}" STREQUAL "linux")
	target_include_directories(hidpp PRIVATE ${LIBUDEV_INCLUDE_DIRECTORIES})
	target_link_libraries(hidpp ${LIBUDEV_LIBRARIES})
elseif("${HID_BACKEND}" STREQUAL "windows")
	target_compile_definitions(hidpp PRIVATE
		-DUNICODE -D_UNICODE
		-D_WIN32_WINNT=0x0600 # Use vista or later
	)
	target_link_libraries(hidpp setupapi hid cfgmgr32)
elseif("${HID_BACKEND}" STREQUAL "macos")
	target_link_libraries(hidpp stdc++ "-framework IOKit" "-framework CoreFoundation" objc)
	# 	^ Src: https://stackoverflow.com/a/25286420/10601702
	#		The approach using find_library() didn't work
	# set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
endif()

install(DIRECTORY "./" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hidpp"
	FILES_MATCHING PATTERN "*.h")
install(TARGETS hidpp EXPORT hidpp-config
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT hidpp-config
	NAMESPACE hidpp::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hidpp"
)
