import mysql.connector, datetime, requests, sys


CONFIG = {
    'user': 'root',
    'password': '',
    'host': '127.0.0.1',
    'database': 'testing',
    'raise_on_warnings': True
}


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("USAGE: python3 hydro_collector.py IP_ADDR")
        sys.exit(1)

    ip = sys.argv[1]

    req = requests.get(f'http://{ip}/api/read')
    json = req.json()
    json['time'] += 14400    # temporary timestamp offset. FIX IN HYDRO MANAGER
    json['time'] = datetime.datetime.fromtimestamp(json['time'])
    time = json['time']
    json['time'] = str(json['time'])
    print(json)

    cnx = mysql.connector.connect(**CONFIG)
    cursor = cnx.cursor()

    query = ("INSERT INTO sensor_readings (timestamp,sensor_id,sensor_reading,sensor_type_index) "
             "VALUES (%s,%s,%s,%s)")
    cursor.execute(query, (time,1,json['ph'],0))
    cnx.commit()

    cursor.close()
    cnx.close()
    print("Committed reading to database")

