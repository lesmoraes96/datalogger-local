import os
import pymysql
import json

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
                INSERT INTO tabela_medicoes (datahora, temperatura, umidade, pressao, estado_porta, estado_alarme)
                VALUES (%s, %s, %s, %s, %s, %s)
            """
            cur.execute(sql, (
                event.get('datahora'),
                event.get('temperatura'),
                event.get('umidade'),
                event.get('pressao'),
                event.get('estado_porta'),
                event.get('estado_alarme')
            ))
            conn.commit()

        return {
            'statusCode': 200,
            'body': json.dumps('Medições salvas com sucesso!')
        }
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Erro ao salvar medições no RDS: {str(e)}")
        }
    finally:
        conn.close()