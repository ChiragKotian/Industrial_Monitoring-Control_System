import socket
import json
import base64
from Crypto.Cipher import AES
from Crypto.Util.Padding import unpad

# 🔑 EXACT SAME SECRETS AS YOUR ESP32
AES_KEY = b"HPCL_HACKATHON_K"
AES_IV  = bytes([0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
                 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F])

# 🎧 Listen on all network interfaces on port 1700
UDP_IP = "0.0.0.0"
UDP_PORT = 1700

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

def decrypt_payload(encrypted_bytes):
    """Attempt to decrypt the packet. Returns clean string if successful, None if it's noise."""
    try:
        # AES-128 CBC block sizes MUST be multiples of 16.
        if len(encrypted_bytes) % 16 != 0:
            return None # Ignore it. It's likely a random LoRa device in the neighborhood.
            
        cipher = AES.new(AES_KEY, AES.MODE_CBC, AES_IV)
        decrypted_padded = cipher.decrypt(encrypted_bytes)
        
        # Remove the PKCS#7 padding we added in the ESP32 C++ code
        decrypted_data = unpad(decrypted_padded, AES.block_size)
        
        return decrypted_data.decode('utf-8')
    except ValueError:
        # Silently ignore padding errors (this happens when the gateway picks up 
        # someone else's LoRa packets that don't match our encryption key)
        return None
    except Exception as e:
        print(f"🚨 Severe Decryption Error: {e}")
        return None

print(f"🚀 HPCL Local Server online. Listening for SenseCAP M2 on port {UDP_PORT}...")

while True:
    data, addr = sock.recvfrom(2048) # Buffer size
    
    # 🔍 BULLETPROOF PARSER: Find the exact byte where the JSON starts '{'
    json_start = data.find(b'{')
    
    if json_start != -1:
        try:
            payload_str = data[json_start:].decode('utf-8')
            packet = json.loads(payload_str)
            
            # 'rxpk' contains the array of received LoRa packets
            if "rxpk" in packet:
                for rx in packet["rxpk"]:
                    raw_base64 = rx.get('data')
                    
                    if raw_base64:
                        # 1. Decode Base64 to get the raw encrypted bytes
                        encrypted_bytes = base64.b64decode(raw_base64)
                        
                        # 2. Attempt AES-128 Decryption
                        clean_csv = decrypt_payload(encrypted_bytes)
                        
                        # 3. Only print if it successfully decrypted with OUR key
                        if clean_csv:
                            print("\n" + "="*50)
                            print(f"📡 Secure Packet Caught from Gateway: {addr[0]}")
                            print(f"📶 Signal Strength: RSSI {rx.get('rssi')} | SNR {rx.get('lsnr')}")
                            print(f"🔒 Raw Hex (Encrypted): {encrypted_bytes.hex()}")
                            print(f"✅ Decoded Text: {clean_csv}")
                            
                            # TODO: Pass 'clean_csv' to your InfluxDB / PostgreSQL / Streamlit UI here!
                            
        except Exception as e:
            # Silently ignore harmless background network pings from the SenseCAP
            pass