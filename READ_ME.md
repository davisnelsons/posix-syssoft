# posix-syssoft

A posix-thread implementation of a temperature sensor gateway I wrote for a university course some time ago as a first foray in Linux-C and operating system principles.
Because this implementation is partly based on code provided by course instructors which I did not include for copyright purposes, it is non functional with just this codeset.
Moreover a double-pointed list implementation is necessary, which I only partly wrote so it cannot be included.
Two processes - a main process and a log process are created, with the log process saving relevant info in log file, accessible by all three threads in the main process:

Main process threads: 
* connmgr - receives sensor data from a TCP/IP socket, puts it on a shared buffer (sbuffer.c)
* datamgr - takes sensor data from shared buffer, calculates running average, depending on config shows messages to user
* storagemgr - saves sensor data received from shared buffer in an SQL database

When connmgr receives new data, it wakes up, and puts data on the shared buffer - this in turn wakes up datamgr and storagemgr, with some mechanisms in place to prioritize
access for datamgr as it has to immediately throw caution messages in case of high temperatures.

__Shared buffer__

A custom thread safe shared buffer implementation for one writer and two consumers.

# Issues

* Slight memory leak issues
* Main file needs refactoring into multiple files
* Priority mechanism for datamgr/storagemgr breaks down when high frequency of data is being sent
* Confusing and hard to maintain mutex locking
* Buggy log process, the killing of this process needs rework, every once in a while a zombie process occurs
