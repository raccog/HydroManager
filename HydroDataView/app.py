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

    data = []
    flags = []
    i = 0
    for (timestamp, sensor_reading) in cursor:
        if i == 5:
            flags.append({'y': sensor_reading, 'x': timestamp.strftime('%s'), 'title': 'FLAG', 'text': 'PH'})
        data.append([timestamp, sensor_reading])
        i += 1
    cursor.close()
    cnx.close()

    series = highcharts.SplineSeries(name = "pH Over Time", data = data, id='phseries')
    #flags = highcharts.FlagsSeries(name="pH Flags", data_labels=flags, on_series='phseries')

    # Fuck this crap that does not work.
    # For "some reason" the python library for highcharts consistently removes all of my flags' data points
    # UGH... Find another way to use highchart or use another chart framework
    options['series'] = [{
        'type': 'flags',
        'name': 'FLAGS',
        'onSeries': 'phseries',
        'data': flags,
        'shape': 'flag',
        'title': 'ph down'
    }];

    chart = highcharts.Chart.from_options(options, chart_kwargs={'is_stock_chart': True})
    chart.container = 'container'
    chart.add_series(series)

    chart.to_js_literal(filename='static/js/test.js')
    
    return render_template('status.html', chart_js=url_for('static', filename='js/test.js'))

