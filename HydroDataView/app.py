# Hydroponic Data View - Ryan Cohen, 2023
# Version 0.1.0
# 
# This program serves a website that will be used to view the database in
# charts and tables.
#
# Currently, nothing is displayed yet. The first view will be a pH chart.
from flask import Flask, render_template
import mysql.connector, datetime

app = Flask(__name__)

USER = 'root'
PASSWORD = ''
HOST = '127.0.0.1'
DATABASE = 'testing'

@app.route("/")
def root_page():
    return "<p>KEEP GOING</>"


@app.route("/status")
def status_page():
    return render_template('status.html', timestamp=datetime.datetime.now(), ph=6.12)

    cnx = mysql.connector.connect(user=USER, password=PASSWORD,
                                  host=HOST, database=DATABASE)
    cursor = cnx.cursor()
    query = ("create table qtesting ( id int )")
    cursor.execute(query)
    cursor.close()
    cnx.close()
    return f"<p>TESTING</p>"
