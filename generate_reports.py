import os
import pymysql
import csv
import io
import json
import boto3
from datetime import datetime

# Variáveis de ambiente configuradas no Lambda
RDS_HOST = os.environ['RDS_HOST']
RDS_USER = os.environ['RDS_USER']
RDS_PASS = os.environ['RDS_PASS']
RDS_DB   = os.environ['RDS_DB']
RDS_PORT = int(os.environ['RDS_PORT'])
BUCKET_NAME = os.environ['BUCKET_NAME']

# Cliente S3
s3_client = boto3.client("s3")

# Prefixos por tabela
PREFIX_MAP = {
    "tabela_logs": "logs/",
    "tabela_medicoes": "medicoes/",
    "tabela_setpoints": "relatorios/"
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

    tabela = event.get('tabela')
    data_inicio = event.get('data_inicio')
    data_fim = event.get('data_fim')

    try:
        if not tabela:
            return {"statusCode": 400, "body": "Parâmetro 'tabela' é obrigatório."}
        
        if tabela not in PREFIX_MAP:
            return {"statusCode": 400, "body": f"Tabela '{tabela}' não suportada."}

        with conn.cursor() as cur:
            sql = f"SELECT * FROM {tabela}"
            if data_inicio and data_fim and tabela in ["tabela_medicoes", "tabela_logs", "tabela_setpoints"]:
                sql += f" WHERE datahora BETWEEN '{data_inicio}' AND '{data_fim}'"

            cur.execute(sql)
            rows = cur.fetchall()
            colnames = [desc[0] for desc in cur.description]

        # Criar CSV
        output = io.StringIO()
        writer = csv.writer(output)
        writer.writerow(colnames)
        writer.writerows(rows)
        csv_data = output.getvalue()

        # Nome do arquivo
        prefix = PREFIX_MAP[tabela]
        filename = f"{prefix}{tabela}_report_{datetime.utcnow().strftime('%Y%m%d_%H%M%S')}.csv"

        # Upload para o S3
        s3_client.put_object(
            Bucket=BUCKET_NAME,
            Key=filename,
            Body=csv_data.encode("utf-8"),
            ContentType="text/csv"
        )

        return {
            'statusCode': 200,
            'body': f"Relatório salvo em s3://{BUCKET_NAME}/{filename}"
        }

    except Exception as e:
        return {'statusCode': 500, 'body': json.dumps(f"Erro ao gerar relatorio: {str(e)}")}
    finally:
        conn.close()
