from pymodbus.client import ModbusTcpClient

# Endereço IP do ESP32 e porta Modbus TCP
MODBUS_IP = '192.168.15.16'  # Substitua pelo IP real do seu ESP32
MODBUS_PORT = 502

# Conectar ao servidor Modbus TCP
client = ModbusTcpClient(MODBUS_IP, port=MODBUS_PORT)
client.connect()

# --- Leitura dos registradores 0 a 4 (dados atuais do sistema)
print(">>> Lendo registradores 0 a 4 (dados de sensores):")
rr = client.read_holding_registers(address=0, count=5)
if rr.isError():
    print("Erro na leitura!")
else:
    registros = rr.registers
    print(f"Temperatura: {registros[0]/10:.1f} °C")
    print(f"Umidade:     {registros[1]/10:.1f} %")
    print(f"Pressão:     {registros[2]} Pa")
    print(f"Porta fechada: {'Sim' if registros[3] == 1 else 'Não'}")
    print(f"Alarme ativo:  {'Sim' if registros[4] == 1 else 'Não'}")

# --- Escrita de novos setpoints nos registradores 10 a 15
print("\n>>> Enviando novos setpoints:")

# Exemplo: temperatura min 20.0, max 29.0, umidade min 50.0, max 65.0, pressão min 350, max 720
novos_setpoints = [200, 290, 500, 650, 350, 720]  # valores multiplicados por 10 para float*10

rq = client.write_registers(address=10, values=novos_setpoints)
if rq.isError():
    print("Erro ao escrever setpoints!")
else:
    print("Setpoints enviados com sucesso.")

# Encerrar conexão
client.close()
