# Hydroponic Manager

This set of software is used for managing a hydroponic system. This includes
keeping a reservoir's pH in range, refilling a reservoir, collecting sensor data
and events, and displaying collected data in a website.

Currently, there are 3 programs:

* HydroManager - An Arduino sketch that implements most of the basic hydroponic
management functions; pH stabilization, reservoir refilling, and providing access
to sensor readings and events through an HTTP server.
* HydroCollector - A Python script that connects to a HydroManger to request
the most recent data. It is meant to be run continuously using cron.
* HydroDataView - A Flask server for a website to view data that has been collected
from HydroManagers. Currently, there is a pH chart using the Highcharts Javascript
framework.

