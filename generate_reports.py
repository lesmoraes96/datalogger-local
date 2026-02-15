import os
import pymysql
import csv
import io
import json
import boto3
from datetime import datetime


# Environment variables configured in Lambda
RDS_HOST = os.environ['RDS_HOST']
RDS_USER = os.environ['RDS_USER']
RDS_PASS = os.environ['RDS_PASS']
RDS_DB   = os.environ['RDS_DB']
RDS_PORT = int(os.environ['RDS_PORT'])
BUCKET_NAME = os.environ['BUCKET_NAME']


# S3 client
s3_client = boto3.client("s3")


# Prefixes by table
PREFIX_MAP = {
    "logs_table": "logs/",
    "measurements_table": "measurements/",
    "setpoints_table": "reports/"
}


def lambda_handler(event, context):
    conn = pymysql.connect(
        host=RDS_HOST,
        user=RDS_USER,
        password=RDS_PASS,
        database=RDS_DB,
        port=RDS_PORT,
        connect_timeout=5
    )

    table_name = event.get('table_name')
    start_date = event.get('start_date')
    end_date = event.get('end_date')

    try:
        if not table_name:
            return {"statusCode": 400, "body": "Parameter 'table_name' is required."}
        
        if table_name not in PREFIX_MAP:
            return {"statusCode": 400, "body": f"Table '{table_name}' not supported."}

        with conn.cursor() as cur:
            sql = f"SELECT * FROM {table_name}"
            if start_date and end_date and table_name in ["measurements_table", "logs_table", "setpoints_table"]:
                sql += f" WHERE datetime BETWEEN '{start_date}' AND '{end_date}'"

            cur.execute(sql)
            rows = cur.fetchall()
            colnames = [desc[0] for desc in cur.description]

        # Create CSV
        output = io.StringIO()
        writer = csv.writer(output)
        writer.writerow(colnames)
        writer.writerows(rows)
        csv_data = output.getvalue()

        # Filename
        prefix = PREFIX_MAP[table_name]
        filename = f"{prefix}{table_name}_report_{datetime.utcnow().strftime('%Y%m%d_%H%M%S')}.csv"

        # Upload to S3
        s3_client.put_object(
            Bucket=BUCKET_NAME,
            Key=filename,
            Body=csv_data.encode("utf-8"),
            ContentType="text/csv"
        )

        return {
            'statusCode': 200,
            'body': f"Report saved to s3://{BUCKET_NAME}/{filename}"
        }

    except Exception as e:
        return {'statusCode': 500, 'body': json.dumps(f"Error generating report: {str(e)}")}
    finally:
        conn.close()
