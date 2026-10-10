cmake_minimum_required (VERSION 3.10)

project ("plain_FTP_client_MRE")

set(APP_NAME "plain_FTP_client_MRE")
set(DEVELOPER_NAME "RDZDX")
set(RAM "1500") #Ram requirement (in kb), same as gpt.vxp (console history buffer is the main consumer)
set(IMSI "1234567890" CACHE STRING "Imsi of first SIM-card")
set(API "TCP File SysStorage SysFile") #Api for app, only what is needed for plain FTP + local files
set(APPID "-1")
set(CERTID "1" CACHE STRING "")
set(CERT "none" CACHE STRING "")

option(BACKGROUND "App can work background" OFF)

set(MRE_SDK "$ENV{MRE_SDK}" CACHE STRING "MRE SDK dir")
set(TinyMRESDK "$ENV{TinyMRESDK}" CACHE STRING "TinyMRESDK dir")

if(BACKGROUND)
    add_definitions(-DSUPPORT_BG)
endif()

include(cmake/MreUtils.cmake)

add_subdirectory(resourses)
add_subdirectory(mreapi)
add_subdirectory(run)

add_subdirectory(main)

#Full list: Audio Camera Call TCP File HTTP Sensor SIM card Record SMS(person) SMS(SP) BitStream Contact LBS MMS ProMng SMSMng Video XML Sec SysStorage Payment BT PUSH UDP SysFile
