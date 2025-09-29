# sweep_once.py  
# logs continuously, posts to google sheet, forces shutter open, makes grating selectable
import time                         # allows for timestamps
import serial                       # serial comm (needed for MONO)
import numpy as np                  # math tool 
import math                         # simpler math tool
import requests                     # http client for posting
import msvcrt                       # allows for command prompt from windows
import win32com.client as win32     # connection through windows for Ophir

###----------------------------------------------------------------------------------------------###
### Ophir has a sample rate of 10Hz, so if u need faster sweeps, decrease SAMPLES, dont mess with 
### Dwell_s. Going below the current dwell time can return the same sample or NaN, and QUIETLY
### the google app script treats NaN as 0, invalidating ALL DATA
###----------------------------------------------------------------------------------------------###

MONO_COM = "COM3"                   # port monochromator is connected to 
DWELL_S = 0.15                      # dwell time between steps. 
SAMPLES = 5                         # power samples per step (averaged)
DO_BG_SUB = False                   # keep shutter open, no background subtraction
POST_URL = "https://script.google.com/macros/s/AKfycbzRB5kRTuDU3dlqUVokS2orTb6J8BoIjkudVlq_eNcWCWpixTXBzmH1sCBWQWsQavBv6w/exec"
# post url must match deployment from app script
STOP_WORD = "stop"                  # type this in console to stop
_input_buf = ""                     # buffer holding recent key strokes in command window


#-----\                       /------------ Wavelength vs power plot
#      \                     /
#       \                   /
#        \                 /
#         \               /
#          \             /
#           \           /
#            \         /
#             \       /
#              \     /
# We want to     ---
# find this point^^^ 
def dip_smooth_min(x, y):                       # find the dip in data, like diagram above
    n = len(y)                                  # get how many data points there are
    if n == 0:                                  # if no points,
        return float('nan'), float('nan')       # return nan values
    sm = [y[0]] + [(y[i-1] + y[i] + y[i+1]) / 3.0 for i in range(1, n-1)] + [y[-1]]# replace y[i] with the average of it, 
                                                                                   # and its 2 nearest neighbors
    idx = min(range(n), key=lambda i: sm[i])    # find index with smallest value
    return x[idx], y[idx]                       # return original x,y value with that index




def safe_num(x, default=0.0):                   # function returns a safe float, defaulting to 0
    try:                                        # try-except blocks run normally, until it fails
                                                # if it fails, it moves to the exception block
        x = float(x)                            # tries to set x as a float
        return x if np.isfinite(x) else default # check that x is finite
    except Exception:                           # if above functions failed,
        return default                          # return default (0)

def key_stop_pressed():                                         # function checks for "STOP"
    global _input_buf                                           # rolling rx buffer from command prompt
    while msvcrt.kbhit():                                       # if there are characters in windows console buffer,
        ch = msvcrt.getwch()                                    # read one unicode keypress
        if ch in ("\r","\n"): _input_buf = ""; continue         # enter clears the buffer
        if ch == "\x08": _input_buf = _input_buf[:-1]; continue # if backspace was pressed, delete prev. key
        _input_buf += ch; _input_buf = _input_buf[-8:]          # append new character. keep rolling window of last 8 chars
        if _input_buf.lower().endswith(STOP_WORD): return True  # checks if input matches "stop"
    return False                                                # defaults to return false, keep sweeping

def wait_idle(ser, timeout=20.0): # ser is the serial name of mono     # determines if the mono is idle
    t0 = time.time()                                                   # log the start time
    while time.time() - t0 < timeout:                                  # keep looping until timeout
        ser.reset_input_buffer()                                       # throw away stale bytes
        ser.write(b"IDLE?\r\n")                                        # are you idle?
        time.sleep(0.05)                                               # wait 50ms so the device can answer (hes slow)
        resp = ser.read_until(b"\r\n").decode(errors="ignore").strip() # read one line from device. returns 0 or 1
                                                                       # According to manual: 1 = no operations pending, 0 = pending
        if resp.endswith("1") or resp == "1":                          # if device claims to be idle,
            return True                                                # it must be idle
        time.sleep(0.05)                                               # if not idle, wait 50ms to ask again
    return False                                                       # while loop ran out of time and was not idle

def set_grating(ser, n):                    # the device has 2 gratings, 1 for visible and 2 for NIR
    cmd = f"GRAT {int(n)}\r\n".encode()     # command the device expects. n can be 1 or 2, 2 for NIR
    ser.write(cmd)                          # indicate the grating based on user entry
    ok = wait_idle(ser, timeout=60.0)       # command to change grating, but wait up to 60s for an idle reading
    return ok                               # ok is what wait_idle returns, a boolean value

# ---------- INSTRUMENT I/O ----------
def connect_instruments(grating_choice=None):   # sets up serial comm of both instruments
    ser = serial.Serial(MONO_COM, 9600, timeout=1, dsrdtr=False, rtscts=False)# connect to monochromator
                       #com port, baud, timeout  , data terminal ready/data set ready, request to send/clear to send
    ser.write(b"UNITS nm\r\n")              # use nm as unit 
    ser.write(b"SHUTTER 1\r\n")             # force shutter open during operation
    time.sleep(0.1)                         # give device 100ms to process commands

    if grating_choice is not None:                      # if there is valid input for grating selection
        if not set_grating(ser, grating_choice):        # sends command
            print("Grating Switch Timed Out")           # UI message

    ophir = win32.Dispatch("OphirLMMeasurement.CoLMMeasurement") # Com object for power meter
    devs = ophir.ScanUSB()                                       # ask Ophir API for list of connected USB meters
    if not devs:                                                 # if no meters detected
        raise SystemExit("No power meter detected")              # stop program and display message
    h = ophir.OpenUSBDevice(devs[0])                             # opens first meter detected and gives it handle "h"
    ophir.StartStream(h,0); time.sleep(0.3); ophir.GetData(h,0)  # start streaming on channel 0, wait .3s for data, flush the buffer
    return ser, ophir, h                                         # return serial port of MONO, Com object of Power meter, device handle of that meter

def read_power(ophir, h):                       # reading data from power meter
    vals, ts, st = ophir.GetData(h,0)           # returns what power meter has buffered so far
                                                # vals: list of power readings(#s), 
                                                # ts:   timestamp of said readings
                                                # st:   status flags
    return (vals[-1] if vals else float("nan")) # dont use ts or st, so just return vals.

def sweep_once(ser, ophir, h, start_nm, end_nm, step_nm): # performs the sweep!!!
    ser.write(b"SHUTTER 1\r\n")                           # ensure OPEN shutter
    time.sleep(0.1)                                       # wait 100ms

    wl, pw = [], []                                       # wavelength, power at that wavelength
    nm = start_nm                                         # begin sweep at start wavelength
    N = int(round((end_nm - start_nm) / step_nm)) + 1     # figure number of steps
    for _ in range(N):                                    # loop once per wavelength point
        if key_stop_pressed(): return None                # if stop is typed, bail immediately
        ser.write(f"GOWAVE {nm:.3f}\r\n".encode())        # CS130B manual says GOWAVE command is best
        if not wait_idle(ser, timeout=5.0):               # wait UP TO 5s for idle to claim idle
            print("Warn: move not idle; continuing.")     # put up warning, this is likely a bad data point
        acc = []                                          # accumulate samples at this wavelength
        for _s in range(SAMPLES):                         # take N samples (defined at top)
            if key_stop_pressed(): return None            # dont return data if stop is typed
            time.sleep(DWELL_S / max(1, SAMPLES))         # small delay between reads. meter is slow
            v = read_power(ophir, h)                      # ask power meter for latest value
            if not np.isnan(v): acc.append(v)             # if real # came back, save it. skip the Nans
        p = (np.mean(acc) if acc else float("nan"))       # average all valid samples. if none valid, mark NaN
                                                          # CONSIDER CHANGING APP SCRIPT TO SHOW NaNS
        wl.append(nm); pw.append(p)                       # record wavelength and its average power
        nm += step_nm                                     # step to next wavelength in the group

    arr = np.array(pw, float)                             # make a NumPy array of the powers
    i_min = int(np.nanargmin(arr))                        # find the index of the smallest power value
    raw_nm, raw_W = wl[i_min], pw[i_min]                  # find wavelength of power dip (Bragg wavelength)
    fit_nm, fit_W = dip_smooth_min(wl, pw)                   # Find vertex of smallest dip (quadratic fitting)
    return wl, pw, raw_nm, raw_W, fit_nm, fit_W           # Return: Wavelengths, average power related to them, 
                                                          # raw minimum(nm,W), fitted minimum(nm,W)

def log_to_sheet(power_W, center_nm):                           # log over IOT, the power and wavelength
    payload = {                                                 # make JSON payload
        "mode": "update",                                       # Tells App Script to add to current row
        "OpticalPower": safe_num(power_W, 0.0),                 # cleaned version of optical power
        "CenterWavelength": safe_num(center_nm, 0.0)            # cleaned version of center wavelength
    }
    try:                                                        
        r = requests.post(POST_URL, json=payload, timeout=6)    # upload JSON request
        print("Patched last row:", r.status_code, payload)      # output to command window
    except Exception as e:                                      #
        print("POST failed:", e)                                # output to command window




def main():                                                                  
    try:
        start_nm = float(input("Start wavelength (nm): "))              # prompted user input
        end_nm   = float(input("End wavelength (nm): "))                # ^
        step_nm  = float(input("Step size (nm): "))                     # ^^
    except Exception:                                                   
        print("Invalid input."); return                                 # user error caused error
    g = input("Grating number [1/2] (Enter for 2): ").strip()           # Grating 2 is 1000 blaze, use for NIR
                                                                        # just press enter, grating 2 is default
    grating_choice = 2 if g == "" else int(g)                           # interpret user interface
        
    print(f"\nSweeping {start_nm} → {end_nm} nm in {step_nm} nm steps.")# console output
    print(f"Using grating {grating_choice}.")                           # console output
    print(f"Running continuously. Type '{STOP_WORD}' to stop.\n")       # remind user how to stop

    ser, ophir, h = connect_instruments(grating_choice)                 # open serial port, set units/shutter,
                                                                        # switch to selected grating, start ophir stream
    try:
        while True:                                                     # always runs
            if key_stop_pressed(): break                                # leave looop in stop condition
            res = sweep_once(ser, ophir, h, start_nm, end_nm, step_nm)  # DO THE SWEEP
            if res is None: break                                       # sweep interrupted by STOP command
            wl, pw, raw_nm, raw_W, fit_nm, fit_W = res                  # unpack lists of the two dip estimates
            print(f"DIP (raw): {raw_nm:.4f} nm, {raw_W:.3e} W")         # print raw dip
            print(f"DIP (fit): {fit_nm:.4f} nm, {fit_W:.3e} W")         # print fitted dip

            with open("sweep.csv","w") as f:                            # open or create csv file in current folder in write mode
                                                                        # name file "f" for easy writing in code
                f.write("wavelength_nm,power_W\n")                      # write header for CSV file
                for x,y in zip(wl,pw): f.write(f"{x:.5f},{y:.6e}\n")    # loop over data, with wavelength and power
                                                                        # wavelength is fixed point 5-digit, power is scientific 6-digit after decimal
                                                                        # zip stops at the shorter list if lengths differ
            log_to_sheet(fit_W, fit_nm)                                 # upload fitted power and center wavelength to Google Sheet

            for _ in range(10):                                         # do a 10 step pause between sweeps.
                                                                        # variable "_" is used on purpose to designate it as unused elsewhere
                if key_stop_pressed(): raise KeyboardInterrupt          # checks for keyboard activity
                time.sleep(0.1)                                         # sleep 100ms before next check
    except KeyboardInterrupt:                                           # catches stop signal. "stop" or Ctrl+c will do it
        pass                                                            # do nothing. skip error and let code enter cleanup section below
    finally:                                                            # shut things down safely
        try: ophir.StopStream(h,0)                                      # tell power meter to stop streaming data
        except: pass                                                    # ignore error from ophir, as it loves to do when told to stop
        try: ophir.Close(h)                                             # release comm handle of power meter
        except: pass                                                    # if already closed or cant close, ignore the error
        ser.close()                                                     # close serial port to monochromator
        print("Stopped.")                                               # console output
                            
if __name__ == "__main__":                                              # set this script as main so it runs properly
    main()                                                              # if name is main, run the script

