import serial
import serial.tools.list_ports
import time
import numpy as np
import wave
import matplotlib.pyplot as plt

# configuration
BAUD = 921600
SAMPLE_RATE = 44100
BUF_SIZE = 128
DURATION = 10

# 4 byte synchronisation marker which is sent before each chunk 
SYNC = bytes([0xAA, 0xBB, 0xCC, 0xDD])

# find the stm port
def find_stm32_port():
    ports = serial.tools.list_ports.comports()
    
    for port in ports:
        desc = port.description.lower() if port.description else ""
        
        # Common identifiers for STM32
        if "stm" in desc:
            return port.device

    return None

PORT = find_stm32_port()

ser = serial.Serial(PORT, BAUD, timeout=5)

# shift buffer until you find the sync bytes
def find_sync(ser):
    buf = bytearray(4)
    while True:
        buf = buf[1:] + ser.read(1)
        if bytes(buf) == SYNC:
            return True

total_samples = SAMPLE_RATE * DURATION
received = []

print(f"Recording {DURATION} seconds...")

# read each chunk
while len(received) < total_samples:
    find_sync(ser)

    # read 2 times chunk size as they only come in one byte at a time (not in 16-bit blocks)
    chunk = ser.read(BUF_SIZE * 2)
    
    # if chunk is the right size, convert to uint16 array, mask bottom 12 bytes
    if len(chunk) == BUF_SIZE * 2:
        samples = np.frombuffer(chunk, dtype='<u2') & 0x0FFF

        # append samples to buffer
        received.extend(samples)
    print(f"{len(received)}/{total_samples} samples", end='\r')

print("\nDone — saving...")
ser.close()

# convert to numpy array
samples = np.array(received[:total_samples], dtype=np.uint16)


# estimate dc offset, convert into signed audio
dc_offset = int(samples.mean())
audio = np.clip((samples.astype(np.int32) - dc_offset) * 16, -32768, 32767).astype(np.int16)


# save audio: mono audio, 16 bits samples, 44.1ksps framerate, write audio data
with wave.open('recording.wav', 'w') as f:
    f.setnchannels(1)
    f.setsampwidth(2)
    f.setframerate(SAMPLE_RATE)
    f.writeframes(audio.tobytes())

print(f"Saved to recording.wav")
print(f"Min: {samples.min()}, Max: {samples.max()}, Mean: {samples.mean():.1f}")

# plot data
plt.figure(figsize=(20, 4))
plt.plot(samples[:2000])
plt.title('Raw ADC samples')
plt.ylabel('Value (0-4095)')
plt.xlabel('Sample')
plt.show()