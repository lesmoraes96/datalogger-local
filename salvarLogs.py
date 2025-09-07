import json
import pymysql
import os

# Variáveis de ambiente configuradas no Lambda
RDS_HOST = os.environ['RDS_HOST']
RDS_USER = os.environ['RDS_USER']
RDS_PASS = os.environ['RDS_PASS']
RDS_DB   = os.environ['RDS_DB']
RDS_PORT = int(os.environ['RDS_PORT'])

def lambda_handler(event, context):
    try:
       # Conectar ao RDS
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
                INSERT INTO tabela_logs (datahora, log_level, mensagem)
                VALUES (%s, %s, %s)
            """
            values = (
                event['datahora'],
                event['log_level'],
                event['mensagem']
            )
            cursor.execute(sql, values)
            conn.commit()

        return {
            'statusCode': 200,
            'body': json.dumps('Log inserido com sucesso!')
        }

    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Erro ao salvar no RDS: {str(e)}")
        }
    finally:
        conn.close()
