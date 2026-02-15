from pymodbus.client import ModbusTcpClient
import time


# ESP32 IP address and Modbus TCP port
MODBUS_IP = '192.168.15.22'
MODBUS_PORT = 502


# Connect to Modbus TCP server
client = ModbusTcpClient(MODBUS_IP, port=MODBUS_PORT)
if not client.connect():
    print("Error: failed to connect to Modbus server.")
    exit()


# --- Read holding registers 0-4 (sensor data)
print(">>> Reading registers 0-4 (sensor data):")

time.sleep(2)  # wait to ensure valid data
rr = client.read_holding_registers(address=0, count=6)  # Reading 6 registers (0-5)
if rr.isError():
    print("Read error!")
else:
    registers = rr.registers
    print(f"Temperature:     {registers[0] / 10:.1f} °C")
    print(f"Humidity:        {registers[1] / 10:.1f} %")
    pressure_int = (registers[3] << 16) | registers[2]  # high part << 16 + low part
    if pressure_int >= 0x80000000:
        pressure_int -= 0x100000000  # signed int32
    pressure = pressure_int / 100.0
    print(f"Pressure:        {pressure:.2f} Pa")
    print(f"Door closed:     {'Yes' if registers[4] == 1 else 'No'}")
    print(f"Alarm active:    {'Yes' if registers[5] == 1 else 'No'}")


# --- Define setpoints individually
print("\n>>> Defining new setpoints:")

# Temperature (multiplied by 10 to represent float as int)
temp_min = 210
temp_max = 350

# Humidity (multiplied by 10 to represent float as int)
humid_min = 200
humid_max = 650

# Pressure
press_min = 100
press_max = 720

# Combine into array for sending
new_setpoints = [temp_min, temp_max, humid_min, humid_max, press_min, press_max]

print("Setpoints prepared for sending:")
print(f"TEMP_MIN = {temp_min / 10:.1f} °C")
print(f"TEMP_MAX = {temp_max / 10:.1f} °C")
print(f"HUMID_MIN = {humid_min / 10:.1f} %")
print(f"HUMID_MAX = {humid_max / 10:.1f} %")
print(f"PRESS_MIN = {press_min} Pa")
print(f"PRESS_MAX = {press_max} Pa")

# --- Write new setpoints to registers 10-15
print("\n>>> Sending setpoints via Modbus...")
rq = client.write_registers(address=10, values=new_setpoints)
if rq.isError():
    print("Error writing setpoints!")
else:
    print("Setpoints sent successfully.")

# Close connection
client.close()
