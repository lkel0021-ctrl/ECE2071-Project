import numpy as np
import wave
import serial
import serial.tools.list_ports
import time
import matplotlib.pyplot as plt

#task 1

devices = serial.tools.list_ports.comports()

for i in range(len(devices)):
    print(devices[i])

STM_serial_port = "/dev/cu.usbmodem21203"
ser = serial.Serial(STM_serial_port, 115200, timeout=100)
record_seconds = 5

#empty list to read data from
audio_data_list = []
sample_rate = 5000 
ADC_sample_length = 2

print(f"Recording audio for {record_seconds} seconds at {sample_rate} samples per second")

for i in range(record_seconds * sample_rate): 
    
    #Check ser.in_waiting before reading
    #This tells you how many bytes are sitting in the buffer before you try to read:
    while ser.in_waiting < ADC_sample_length:
        time.sleep(0.001)
        continue
    
    # read data:
    data = ser.read(ADC_sample_length)
  
    if len(data) < ADC_sample_length:
        print("No data received")
        continue
    
    sample = int.from_bytes(data, byteorder='little', signed=False)
    audio_data_list.append(sample)

    #try 'little' first, if values are >4095 swap to 'big' (fix to make audio cleaner)
    sample = int.from_bytes(data, byteorder='little', signed=False) & 0x0FFF  # mask top 4 bits
    audio_data_list.append(sample)

    if i % 700 == 0:
        print(f"Sample {i} = {sample}")

ser.close()

print(f"Total samples recorded: {len(audio_data_list)}")

# convert list to numpy array
audio_array = np.array(audio_data_list, dtype=np.float64)

# normalise to the full 16-bit unsigned range (0-65535)
audio_array = (audio_array - audio_array.min())
if audio_array.max() > 0:
    audio_array = audio_array / audio_array.max()
    audio_array = (audio_array * 65535 - 32767).astype(np.int16) #scale to signed 16-bit (fix to make audio cleaner)

output_filename = "wavefile.wav"
with wave.open(output_filename, 'wb') as wf:
    wf.setnchannels(1) # mono audio (single channel)
    wf.setsampwidth(2) # 16 bits (2 bytes) 
    wf.setframerate(sample_rate) # set the sample rate that the data was recorded at
    wf.writeframes(audio_array.tobytes()) # write the audio data to the file
print(f"Audio data saved to {output_filename}")

#output functions

#IGNORE CODE BELOW UNTIL OUTPUTS
def find_stm32_port():
    ports = serial.tools.list_ports.comports()
    
    for port in ports:

        desc = port.description.lower() if port.description else ""
        manu = port.manufacturer.lower() if port.manufacturer else ""

        # Common identifiers for STM32
        if ("stm" in desc):
            
            return port.device

    return None


def connect_to_stm32():
    port = find_stm32_port()

    if not port:
        print("STM32 not found")
        return None

    try:
        ser = serial.Serial(port, 921600, timeout=1)
        time.sleep(2)  # allow reset
        print(f"Connected to {port}")
        return ser
    except Exception as e:
        print(f"Failed to open port: {e}")
        return None

sample = []
ser = connect_to_stm32()

try:
    if ser:

        samplerate = 10000 #change to any sample rate

        for i in range(5 * samplerate):
            byte = ser.read(1)
            if len(byte) < 1:
                print(f"Timeout at sample {i}")
            sample.append(byte[0])

        data = np.array(sample)

        data = (data - data.min()) / data.max() 
        data = data * 255
        data = data.astype(np.uint8)

        #THIS IS WHERE THE DIFFERENT OUTPUT CODE IS
        with wave.open("test.wav", 'wb') as wf:
            wf.setnchannels(1) # mono audio (single channel)
            wf.setsampwidth(1) # 8 bits (1 byte ) per sample
            wf.setframerate(samplerate) # set the sample rate that the data was
            wf.writeframes(data.tobytes())
            print("Wave file made")
        

        with wave.open("test.wav", "rb") as rf:
            framesnum = rf.getnframes()
            frames = rf.readframes(framesnum)
            framrate = rf.getframerate()
            
         
        frames = np.array(frames)
        framesnum = np.array(range(framesnum))

        frames = np.frombuffer(frames, dtype=np.int16) #change np.int16 to whichever needed
        frameData = np.vstack((framesnum,frames))
        frameData = np.transpose(frameData)
        np.savetxt("output_wave_data.csv", frameData, delimiter=",")
        


        plt.plot(framesnum[0:1000],frames[0:1000])
        plt.title("Sound wave of inputted sound")
        plt.xlabel("Frame")
        plt.ylabel("Amplitude")
        plt.savefig('output_sound.png')

except Exception as e:
    print(f"error: {e}")


#task 2


STOP_DELAY = 1.5        # seconds to keep recording after object leaves range
DEFAULT_DISTANCE = 10   # default trigger distance in cm


def record_samples(ser, duration, samplerate):
    # reads audio bytes from the STM for the given duration
    samples = []
    total = int(duration * samplerate)
    print(f"Recording {duration}s ...")
    for i in range(total):
        byte = ser.read(1)
        if len(byte) < 1:
            print(f"Timeout at sample {i}")
            continue
        samples.append(byte[0])
    print(f"Done - {len(samples)} samples collected.")
    return samples


def normalise_and_save(samples, samplerate, filename, fmt_choice):
    # save chosen formats
    data = np.array(samples)
    data = (data - data.min()) / data.max()
    data = data * 255
    data = data.astype(np.uint8)

    if '1' in fmt_choice or 'all' in fmt_choice:
        with wave.open(filename + ".wav", 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(1)
            wf.setframerate(samplerate)
            wf.writeframes(data.tobytes())
        print(f"Saved: {filename}.wav")

    if '2' in fmt_choice or 'all' in fmt_choice:
        framesnum = np.array(range(len(data)))
        plt.plot(framesnum[0:1000], data[0:1000])
        plt.title("Sound wave of inputted sound")
        plt.xlabel("Frame")
        plt.ylabel("Amplitude")
        plt.savefig(filename + ".png")
        plt.close()
        print(f"Saved: {filename}.png")

    if '3' in fmt_choice or 'all' in fmt_choice:
        framesnum = np.array(range(len(data)))
        frameData = np.vstack((framesnum, data))
        frameData = np.transpose(frameData)
        np.savetxt(filename + ".csv", frameData, delimiter=",")
        print(f"Saved: {filename}.csv")


def ask_formats():
    # ask user which output formats they want, returns their input as a string
    print("\nWhich output format(s) do you want?")
    print("  1) WAV")
    print("  2) PNG (waveform plot)")
    print("  3) CSV")
    print("  all) All of the above")
    return input("Enter choice(s) separated by spaces: ").strip().lower()


def read_distance(ser):
    # reads one 2-byte distance value (cm) from the STM
    data = ser.read(2)
    if len(data) < 2:
        return 9999  # return large number on timeout so we don't false-trigger
    return int.from_bytes(data, byteorder='little', signed=False)


def manual_mode(ser, samplerate):
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

    filename = input("Output filename (no extension): ").strip()
    if not filename:
        filename = "manual_recording"

    fmt_choice = ask_formats()

    samples = record_samples(ser, duration, samplerate)
    normalise_and_save(samples, samplerate, filename, fmt_choice)


def distance_trigger_mode(ser, samplerate):
    print("\n--- DISTANCE TRIGGER MODE ---")
    print(f"Recording starts automatically when an object enters range.")
    print(f"Recording stops {STOP_DELAY}s after the object leaves.")
    print("Keeps triggering until you press Ctrl-C.\n")

    dist_input = input(f"Trigger distance in cm [default {DEFAULT_DISTANCE}]: ").strip()
    try:
        trigger_dist = float(dist_input) if dist_input else DEFAULT_DISTANCE
    except ValueError:
        trigger_dist = DEFAULT_DISTANCE
        print(f"Invalid input, using {DEFAULT_DISTANCE} cm.")

    filename = input("Base filename for recordings (e.g. trigger_rec): ").strip()
    if not filename:
        filename = "trigger_recording"

    # ask for formats once, reused for every triggered recording
    fmt_choice = ask_formats()

    print(f"\nTrigger distance set to {trigger_dist} cm. Waiting for object ...")

    session = 0
    recording = False
    raw_samples = []
    stop_time = None

    try:
        while True:
            distance = read_distance(ser)

            if not recording:
                # waiting for something to enter range
                if distance <= trigger_dist:
                    recording = True
                    raw_samples = []
                    stop_time = None
                    session += 1
                    print(f"\nObject detected ({distance} cm)! Starting session {session} ...")

            else:
                # currently recording - collect one audio byte per loop
                byte = ser.read(1)
                if len(byte) == 1:
                    raw_samples.append(byte[0])

                if distance > trigger_dist:
                    # object left range - start the stop countdown
                    if stop_time is None:
                        stop_time = time.time() + STOP_DELAY
                        print(f"Object left range, stopping in {STOP_DELAY}s ...")
                else:
                    stop_time = None  # object came back, cancel countdown

                # stop once countdown finishes
                if stop_time is not None and time.time() >= stop_time:
                    recording = False
                    print(f"Recording stopped - {len(raw_samples)} samples collected.")
                    name = f"{filename}_{session}"
                    normalise_and_save(raw_samples, samplerate, name, fmt_choice)
                    print("\nWaiting for next trigger ...")

    except KeyboardInterrupt:
        print("\nExiting Distance Trigger Mode.")


# main

ser = connect_to_stm32()

if not ser:
    print("Could not connect to STM32. Exiting.")
else:
    samplerate = 10000  # change to match your STM sample rate

    print("\n==============================")
    print("     Audio Acquisition CLI    ")
    print("==============================")

    while True:
        print("\nMain Menu:")
        print("  1) Manual Recording Mode  - record for a duration you choose")
        print("  2) Distance Trigger Mode  - sensor starts/stops recording automatically")
        print("  0) Exit")
        choice = input("Select an option: ").strip()

        if choice == '1':
            manual_mode(ser, samplerate)
        elif choice == '2':
            distance_trigger_mode(ser, samplerate)
        elif choice == '0':
            print("Goodbye.")
            break
        else:
            print("Please enter 0, 1, or 2.")

    ser.close()