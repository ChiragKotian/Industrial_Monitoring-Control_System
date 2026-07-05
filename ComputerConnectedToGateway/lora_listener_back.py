import socket
import json
import base64

# 🎧 Listen on all network interfaces on port 1700
UDP_IP = "0.0.0.0"
UDP_PORT = 1700

# Set up the UDP Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"🚀 HPCL Local Server online. Listening for SenseCAP M2 on port {UDP_PORT}...")

while True:
    data, addr = sock.recvfrom(2048) # Buffer size
    
    # 🔍 BULLETPROOF PARSER: Find the exact byte where the JSON starts '{'
    json_start = data.find(b'{')
    
    if json_start != -1:
        try:
            # Decode exactly from the '{' to the end of the packet
            payload_str = data[json_start:].decode('utf-8')
            packet = json.loads(payload_str)
            
            # 'rxpk' contains the array of received LoRa packets
            if "rxpk" in packet:
                for rx in packet["rxpk"]:
                    print("\n" + "="*50)
                    print(f"📡 Packet Caught from Gateway: {addr[0]}")
                    print(f"📶 Signal Strength: RSSI {rx.get('rssi')} | SNR {rx.get('lsnr')}")
                    
                    # LoRa data is Base64 encoded by the gateway, so we decode it here
                    raw_base64 = rx.get('data')
                    if raw_base64:
                        decoded_bytes = base64.b64decode(raw_base64)
                        
                        print(f"📦 Raw Hex: {decoded_bytes.hex()}")
                        
                        # 📝 This will perfectly print your "2024-01-01,14,1..." CSV string!
                        print(f"📝 Decoded Text: {decoded_bytes.decode('utf-8', errors='ignore')}")
                        
        except Exception as e:
            # Silently ignore harmless background network pings
            pass