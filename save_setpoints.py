import os
import pymysql
import json
from datetime import datetime


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
            # Deactivate all existing setpoints
            cur.execute("UPDATE setpoints_table SET activated = FALSE")

            # Insert new active setpoint
            sql = """
                INSERT INTO setpoints_table (
                    datetime,
                    min_temp,
                    max_temp,
                    min_humid,
                    max_humid,
                    min_press,
                    max_press,
                    activated
                )
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """
            cur.execute(sql, (
                event.get('datetime'),
                event.get('min_temp'),
                event.get('max_temp'),
                event.get('min_humid'),
                event.get('max_humid'),
                event.get('min_press'),
                event.get('max_press'),
                event.get('activated')
            ))
            conn.commit()

        return {
            'statusCode': 200,
            'body': json.dumps('Setpoints saved successfully!')
        }
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Error saving to RDS: {str(e)}")
        }
    finally:
        conn.close()
