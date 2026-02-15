import json
import pymysql
import os


# Environment variables configured in Lambda
RDS_HOST = os.environ['RDS_HOST']
RDS_USER = os.environ['RDS_USER']
RDS_PASS = os.environ['RDS_PASS']
RDS_DB   = os.environ['RDS_DB']
RDS_PORT = int(os.environ['RDS_PORT'])


def lambda_handler(event, context):
    try:
        # Connect to RDS
        conn = pymysql.connect(
            host=RDS_HOST,
            user=RDS_USER,
            password=RDS_PASS,
            database=RDS_DB,
            port=RDS_PORT,
            connect_timeout=5
        )

        with conn.cursor() as cursor:
            sql = """
                INSERT INTO logs_table (
                    datetime,
                    log_level,
                    message
                )
                VALUES (%s, %s, %s)
            """
            values = (
                event['datetime'],
                event['log_level'],
                event['message']
            )
            cursor.execute(sql, values)
            conn.commit()

        return {
            'statusCode': 200,
            'body': json.dumps('Log inserted successfully!')
        }

    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Error saving to RDS: {str(e)}")
        }
    finally:
        conn.close()
