# Hydroponic Data Collector - Ryan Cohen, 2023
# Version 0.1.0
# 
# This is meant to be run as a cron script every 5 minutes.
#
# The script connects to a Hydro Manager and collects data from it. This data
# is immediately logged into a MySQL database.
#
# Currently, it only collects basic pH data. In the future, it will also collect
# other sensors, events, and logs.
import mysql.connector, datetime, requests, sys


CONFIG = {
    'user': 'root',
    'password': '',
    'host': '127.0.0.1',
    'database': 'HydroCollection',
    'raise_on_warnings': True
}


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("USAGE: python3 hydro_collector.py IP_ADDR")
        sys.exit(1)

    ip = sys.argv[1]

    req = requests.get(f'http://{ip}/json/mailbox.json')
    json = req.json()
    json['time'] += 14400    # temporary timestamp offset to UTC. FIX IN HYDRO MANAGER
    json['time'] = datetime.datetime.fromtimestamp(json['time'])
    time = json['time']

    json['time'] = str(json['time'])
    print(json)

    cnx = mysql.connector.connect(**CONFIG)
    cursor = cnx.cursor()

    if 'pulse_events' in json:
        for event in json['pulse_events']:
            query = ("INSERT INTO pump_pulses (timestamp,pump_id,pulse_length,interrupted)"
                     "VALUES (%s,%s,%s,%s)")
            cursor.execute(query, (datetime.datetime.fromtimestamp(event['time'] + 14400), event['type'], event['len'], event['interrupt']))

    query = ("INSERT INTO sensor_readings (timestamp,sensor_id,sensor_reading,sensor_type_index) "
             "VALUES (%s,%s,%s,%s)")
    cursor.execute(query, (time,1,json['ph'],0))
    cnx.commit()

    cursor.close()
    cnx.close()
    print("Committed reading to database")

