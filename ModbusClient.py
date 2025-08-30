from pymodbus.client import ModbusTcpClient

# Endereço IP do ESP32 e porta Modbus TCP
MODBUS_IP = '192.168.15.5'
MODBUS_PORT = 502

# Conectar ao servidor Modbus TCP
client = ModbusTcpClient(MODBUS_IP, port=MODBUS_PORT)
if not client.connect():
    print("Erro: não foi possível conectar ao servidor Modbus.")
    exit()

# --- Leitura dos registradores 0 a 4 (dados de sensores)
print(">>> Lendo registradores 0 a 4 (dados de sensores):")
rr = client.read_holding_registers(address=0, count=5)
if rr.isError():
    print("Erro na leitura!")
else:
    registros = rr.registers
    print(f"Temperatura:     {registros[0] / 10:.1f} °C")
    print(f"Umidade:         {registros[1] / 10:.1f} %")
    print(f"Pressão:         {registros[2]} Pa")
    print(f"Porta fechada:   {'Sim' if registros[3] == 1 else 'Não'}")
    print(f"Alarme ativo:    {'Sim' if registros[4] == 1 else 'Não'}")

# --- Definição de setpoints individualmente
print("\n>>> Definindo novos setpoints:")

# Temperatura (multiplicado por 10 para representar float como int)
temp_min = 210  # 21.0 °C
temp_max = 280  # 28.0 °C

# Umidade
umid_min = 500  # 50.0 %
umid_max = 650  # 65.0 %

# Pressão
press_min = 350  # 350 Pa
press_max = 720  # 720 Pa

# Combina em vetor para envio
novos_setpoints = [temp_min, temp_max, umid_min, umid_max, press_min, press_max]

print("Setpoints preparados para envio:")
print(f"TEMP_MIN = {temp_min / 10:.1f} °C")
print(f"TEMP_MAX = {temp_max / 10:.1f} °C")
print(f"UMID_MIN = {umid_min / 10:.1f} %")
print(f"UMID_MAX = {umid_max / 10:.1f} %")
print(f"PRESS_MIN = {press_min} Pa")
print(f"PRESS_MAX = {press_max} Pa")

# --- Escrita dos novos setpoints nos registradores 10 a 15
print("\n>>> Enviando setpoints via Modbus...")
rq = client.write_registers(address=10, values=novos_setpoints)
if rq.isError():
    print("Erro ao escrever setpoints!")
else:
    print("Setpoints enviados com sucesso.")

# Encerrar conexão
client.close()
