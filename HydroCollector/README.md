# Hydroponic Collector

This is a Python script that connects to a Hydroponic Manger to request
the most recent data. It is meant to be run continuously using cron.

## Hydroponic Logger Version 0.1 Plan

The hydroponic logger will be a Linux-hosted program that sends HTTP requests to the
hydroponic manager and logs data and events in a database.

Version 0.1 will be a Python program that implements the basic functions:

* Log pH and PPM to SQL database every X minutes
* Log events such as pH pump pulses or settings changes
* Log errors that come from the hydroponic manager

All logs are currently structured in JSON. The hydro manager will keep a buffer for all
recent logs. When requested via HTTP, it will send those logs to another server where
they can be saved to a database.

Every X minutes, the hydro logger will make two HTTP requests (maybe combine them?) to the
hydro manager; one for the current data readings and another for the log buffer. It will
then add these data points to a database.
