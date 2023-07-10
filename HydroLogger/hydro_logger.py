from flask import Flask
import mysql.connector

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
    cnx = mysql.connector.connect(user=USER, password=PASSWORD,
                                  host=HOST, database=DATABASE)
    cursor = cnx.cursor()
    query = ("create table qtesting ( id int )")
    cursor.execute(query)
    cursor.close()
    cnx.close()
    return f"<p>TESTING</p>"
