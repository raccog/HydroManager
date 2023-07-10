# Hydroponic Data View - Ryan Cohen, 2023
# Version 0.1.0
# 
# This program serves a website that will be used to view the database in
# charts and tables.
#
# Currently, nothing is displayed yet. The first view will be a pH chart.
from flask import Flask, render_template, url_for
import mysql.connector, datetime
from highcharts_stock import highcharts

app = Flask(__name__)

USER = 'root'
PASSWORD = ''
HOST = '127.0.0.1'
DATABASE = 'testing'

options = {
    'chart': {
        'type': 'spline'
    },
    'title': {
        'text': 'pH Over Time'
    },
    'subtitle': {
        'text': 'Irregular time data in Highcharts JS'
    },
    'xAxis': {
        'type': 'datetime',
        'title': {
            'text': 'Date'
        }
    },
    'yAxis': {
        'title': {
            'text': 'pH'
        },
        'min': 3
    },
    'tooltip': {
        'headerFormat': '<b>{series.name}</b><br>',
    },

    'plotOptions': {
        'series': {
            'marker': {
                'enabled': True,
                'radius': 2.5
            }
        }
    },

    'colors': ['#6CF', '#39F', '#06C', '#036', '#000']
}

@app.route("/")
def root_page():
    return "<p>KEEP GOING</>"


@app.route("/status")
def status_page():
    cnx = mysql.connector.connect(user=USER, password=PASSWORD,
                                  host=HOST, database=DATABASE)
    cursor = cnx.cursor()
    query = ("SELECT timestamp, sensor_reading FROM sensor_readings")
    cursor.execute(query)

    ph_data = []
    ph_down = []
    i = 0
    for (timestamp, sensor_reading) in cursor:
        # TODO: Find better way to convert timezones
        # This is a hack to convert the timezone from UTC seconds to EST milliseconds
        timestamp = (timestamp.timestamp() - 14400.0) * 1000.0
        # TODO: Remove these lines and add in the ph pump events
        # A test to see if flags work on highcharts
        if i == 5:
            ph_down.append(timestamp)
        i += 1

        ph_data.append([timestamp, float(sensor_reading)])
    cursor.close()
    cnx.close()
    
    return render_template('status.html', ph_data=ph_data, ph_down=ph_down)

