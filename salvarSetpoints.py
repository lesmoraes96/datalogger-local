import os
import pymysql
import json
from datetime import datetime

# Variáveis de ambiente configuradas no Lambda
RDS_HOST = os.environ['RDS_HOST']
RDS_USER = os.environ['RDS_USER']
RDS_PASS = os.environ['RDS_PASS']
RDS_DB   = os.environ['RDS_DB']
RDS_PORT = int(os.environ['RDS_PORT'])

def lambda_handler(event, context):
    # Conectar ao RDS
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
                INSERT INTO tabela_setpoints (datahora, temp_min, temp_max, umid_min, umid_max, pressao_min, pressao_max, ativo)
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """
            cur.execute(sql, (
                event.get('datahora'),
                event.get('temp_min'),
                event.get('temp_max'),
                event.get('umid_min'),
                event.get('umid_max'),
                event.get('pressao_min'),
                event.get('pressao_max'),
                event.get('ativo')
            ))
            conn.commit()

        return {
            'statusCode': 200,
            'body': json.dumps('Setpoints salvos com sucesso!')
        }
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Erro ao salvar no RDS: {str(e)}")
        }
    finally:
        conn.close()
