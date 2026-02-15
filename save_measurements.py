import os
import pymysql
import json


# Environment variables configured in Lambda
RDS_HOST = os.environ['RDS_HOST']
RDS_USER = os.environ['RDS_USER']
RDS_PASS = os.environ['RDS_PASS']
RDS_DB   = os.environ['RDS_DB']
RDS_PORT = int(os.environ['RDS_PORT'])


def lambda_handler(event, context):
    # Connect to RDS
    conn = pymysql.connect(
        host=RDS_HOST,
        user=RDS_USER,
        password=RDS_PASS,
        database=RDS_DB,
        port=RDS_PORT,
        connect_timeout=5
    )

    try:
        with conn.cursor() as cur:
            sql = """
                INSERT INTO measurements_table (
                    datetime,
                    temperature,
                    humidity,
                    pressure,
                    door_state,
                    alarm_state
                )
                VALUES (%s, %s, %s, %s, %s, %s)
            """
            cur.execute(sql, (
                event.get('datetime'),
                event.get('temperature'),
                event.get('humidity'),
                event.get('pressure'),
                event.get('door_state'),
                event.get('alarm_state')
            ))
            conn.commit()

        return {
            'statusCode': 200,
            'body': json.dumps('Measurements saved successfully!')
        }
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Error saving measurements to RDS: {str(e)}")
        }
    finally:
        conn.close()
