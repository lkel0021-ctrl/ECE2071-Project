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
STOP_DELAY = 3
trigger_mode = False
total_samples = SAMPLE_RATE * DURATION

# 4 byte synchronisation marker which is sent before each chunk 
SYNC = bytes([0xAA, 0xBB, 0xCC, 0xDD])
STOP = bytes([0xFF, 0xFE, 0xFD, 0xFC])

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

def get_output_options():
    options = ['png', 'csv']
    results = []
    
    for option in options:
        while True:
            response = input(f"Save as {option}? (y/n): ").strip().lower()
            if response in ('y', 'n'):
                results.append(response == 'y')
                break
            print("Please enter y or n")
    
    return results

def save_data(options, filename, received):
    wav_name = f"{filename}.wav"
    csv_name = f"{filename}.csv"
    png_name = f"{filename}.png"

    # convert to numpy array
    samples = np.array(received[:total_samples], dtype=np.uint16)

    # estimate dc offset, convert into signed audio
    dc_offset = int(samples.mean())
    audio = np.clip((samples.astype(np.int32) - dc_offset) * 16, -32768, 32767).astype(np.int16)

    with wave.open(wav_name, 'wb') as wf:
        wf.setnchannels(1) # mono audio (single channel)
        wf.setsampwidth(2) # 16 bits (2 bytes) per sample
        wf.setframerate(SAMPLE_RATE) # set the sample rate that the data was
        wf.writeframes(audio.tobytes())

    framesnum = np.arange(len(samples))

    frameData = np.vstack((framesnum, samples)).T

    if (options[1]):
        np.savetxt(csv_name, frameData, delimiter=",")

    if (options[0]):
        plt.plot(framesnum[0:1000], samples[0:1000])
        plt.title("Sound wave of inputted sound")
        plt.xlabel("Frame")
        plt.ylabel("Amplitude")
        plt.savefig(png_name)

    print("Saved!")

def manual_mode():
    
    ser.write(bytes([4]))

    print("\n--- MANUAL RECORDING MODE ---")
    print("You choose how long to record and which formats to save.\n")

    while True:
        try:
            duration = float(input("How many seconds do you want to record? "))
            if duration > 0:
                break
            print("Please enter a number greater than 0.")
        except ValueError:
            print("Please enter a valid number.")

    options = get_output_options()

    filename = input("Output filename (no extension): ").strip()
    if not filename:
        filename = "manual_recording"

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
    save_data(options, filename, received)
    

def distance_trigger_mode(options):

    ser.write(bytes([3]))

    received = []
    recording_number = 0
    print("Recording...")

    while True:
        buf = bytearray(4)

        while True:
            byte = ser.read(1)
            if len(byte) == 0:
                if len(received) > 0:
                    print(f"\nNothing received — saving recording {recording_number}...")
                    save_data(options, recording_number, received)
                    recording_number += 1
                    received = []
                else:
                    print("\nNo data received — waiting...")
                break
            
            buf = buf[1:] + byte
            
            if bytes(buf) == SYNC:
                break

        chunk = ser.read(BUF_SIZE * 2)
        if len(chunk) == BUF_SIZE * 2:
            samples = np.frombuffer(chunk, dtype='<u2') & 0x0FFF
            received.extend(samples)   




# main
if not ser:
    print("Could not connect to STM32. Exiting.")
else:

    print("\n==============================")
    print("       Audio Recording     ")
    print("==============================")

    while True:
        print("\nMain Menu:")
        print("  1) Manual Recording Mode  - record for a duration you choose")
        print("  2) Distance Trigger Mode  - sensor starts/stops recording automatically")
        print("  0) Exit")
        choice = input("Select an option: ").strip()

        if choice == '1':
            manual_mode()
        elif choice == '2':
            distance_trigger_mode(get_output_options())
        elif choice == '0':
            print("Goodbye.")
            break
        else:
            print("Please enter 0, 1, or 2.")

    ser.close()