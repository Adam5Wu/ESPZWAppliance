# ESP ZWAppliance
[![Build Status](https://travis-ci.org/Adam5Wu/ESPZWAppliance.svg?branch=master)](https://travis-ci.org/Adam5Wu/ESPZWAppliance)
[![GitHub issues](https://img.shields.io/github/issues/Adam5Wu/ESPZWAppliance.svg)](https://github.com/Adam5Wu/ESPZWAppliance/issues)
[![GitHub forks](https://img.shields.io/github/forks/Adam5Wu/ESPZWAppliance.svg)](https://github.com/Adam5Wu/ESPZWAppliance/network)
[![License](https://img.shields.io/github/license/Adam5Wu/ESPZWAppliance.svg)](./LICENSE)

All-in-one appliance middle-ware for ESP8266 projects.

## Prologue

So you have a bright new idea, and you want to put it on your ESP8266.

Hack ... hack ... hack ... DONE! It works!! Yay!!!

Then you want to show it off to your friends and/or potential customers.

It is so easy, just bring the tiny module and a battery to wherever and turn it on
and

... Ooops!

*Why the module cannot get Internet?* (Because the AP / password is different?)

*How do I set the new AP and password?* (Hack and rebuild?)

*Wait... I didn't bring my dev environment with me to rerebuild!*

*And why is the time off by a couple of hours?* (Because you are in a different time zone?)

*How do I set the local timezone?* (Hack and rebuild?)

*Wait...*

Well, it is not so simple after all.

You have an application, but you need an appliance.

## Motivation

While every application is very unique and brilliant, they all need some basic supporting
functionalities to turn them into an appliance.

These supporting functions are more-or-less generic, which means "boring" to develop.
However, if not handled well, a defect in the supporting layer can significantly impact
the overall performance of the application.

This project is my effort to provide a unified middle-ware of basic supporting services
for any ESP8266 projects, so that making a functional and reliable appliance from an
application becomes truly easy.

## Features

* Automatically detects and connects to configured access point
* When access point is unavailable, automatically start hosted AP
* Hosted AP runs a captive portal,
when connected to triggers automatic browser display of the main index Webpage
* (WIP) Built-in Web UI for modifying configurations, such as:
	* Access point name / password
	* Hostname and hosted AP name
	* Optional NTP server to sync with when connected to access point
	* Optional timezone with daylight-saving support
* Built-in support for Web OTA
* Built-in mechanism for Web authentication and access control
	* (WIP) Create / modify / remove users and passwords
	* (WIP) Define custom access control rules
* Ability to start Web server in non-captive mode
* Ability to customize every part of the Web UI
	* Authenticated HTTP GET/PUT/DELETE access to the whole file system
	* (WIP) Authenticated WebDAV access to the whole file system
* (WIP) Provides whole file system backup and restore (via storage partitioning)
* More to come!

## How to use

[ESP8266 Arduino Core Fork](https://github.com/Adam5Wu/Arduino-esp8266) is required, it peeks the latest fixes from the upstream, plus some of my own improvements and enhancements, such as support of ESPVFATFS, improved String/Stream implementation, BearSSL, etc.

Install the library - clone this repo into the "libraries" folder in your Arduino projects (found in your home / document directory).

Install all dependent libraries in similar fashion:
- [ArduinoJson Fork](https://github.com/Adam5Wu/ArduinoJson)
- [ZWUtils-Arduino](https://github.com/Adam5Wu/ZWUtils-Arduino)
- [ESPVFATFS](https://github.com/Adam5Wu/ESPVFATFS)
- [ESPEasyAuth](https://github.com/Adam5Wu/ESPEasyAuth)
- [ESPAsyncTCP Fork](https://github.com/Adam5Wu/ESPAsyncTCP)
- [ESPAsyncWebServer Permanent Fork](https://github.com/Adam5Wu/ESPAsyncWebServer)
- [Arduino Time Library Fork](https://github.com/Adam5Wu/Time)
- [Arduino Timezone Library Fork](https://github.com/Adam5Wu/Timezone)
- [ESPAsyncUDP](https://github.com/me-no-dev/ESPAsyncUDP)

(Re)start the Arduino, open up the example from menu [File] - [Examples] - [ESP ZWAppliance] - [Blank]

Simply fill in your own application logic in appropriate functions.

## Bugs / Suggestions?

No problem, just let me know by filing issues.
