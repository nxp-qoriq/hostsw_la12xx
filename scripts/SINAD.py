#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2023 NXP

#This script is to parse the IQ binary file and calculates the I and Q SIAND values for the given sampling rates and frequency

import numpy as np
import matplotlib.pyplot as plt
import math
import statistics
from scipy import signal
from numpy.fft import fft, ifft
from scipy.stats import norm
from scipy.signal import blackmanharris
import argparse

def calculateSNDR_bh(hwfft, f, dc_bins, signal_bins):
    signal_width = signal_bins
    s = np.linalg.norm(hwfft[f-signal_width:f+signal_width]) # /N for true rms value;
    if f < 2:
        noise_bins = np.arange(f+signal_width+1, len(hwfft))
    elif f + signal_width > len(hwfft):
        noise_bins = np.arange(dc_bins, f-(signal_width+1))
    else:
        noise_bins = np.concatenate((np.arange(dc_bins, f-(signal_width+1)), np.arange(f+(signal_width+1), len(hwfft)-1)))
    n = np.linalg.norm(hwfft[noise_bins]) # /N for true rms value;
    sndr = 20 * np.log10(s/n)
    return sndr

parser = argparse.ArgumentParser(description="Parse IQ binary data file and calculate I and Q SINAD values")
parser.add_argument("filename", help="IQ data bin file name (>64k samples)")
parser.add_argument("-s", "--sampling_rate", type=int, default=1966, choices = [122,245,983,1966], help="Sampling rate(MHz). default[1966]")
parser.add_argument("-f", "--frequency", type=int, default=400, choices = [10,50,100,200,300,400,500,600,700], help="Single tone Frequency (MHz). default[400]. Note: This single tone frequency should be less than half of the sampling rate")
args = parser.parse_args()

# Access the arguments
ifile = args.filename
f_in = args.frequency
Fs = args.sampling_rate

## convert MHz to Hz
f_in = f_in*1e6

if Fs == 1966:
    Fs = 1966080000

if Fs == 983:
    Fs = 983040000

if Fs == 245:
    Fs = 245760000

if Fs == 122:
    Fs = 122880000

print("")
print(" Input file       :",ifile)
print(" Frequency    (Hz):",format(f_in, '.0f'))
print(" Sampling rate(Hz):",Fs)

data = np.fromfile(str(ifile), dtype=np.int16)

I = data[0::2]/(2**15)
Q = data[1::2]/(2**15)
i_data = I[:64000]
q_data = Q[:64000]


f1 = 1
f_start = 10e3 # fft starting frequency in Hz (ignoring dc offset and 1/f noise)
f_end = Fs/2 
bw = f_end - f_start # input bandwidth in Hz --> for a Nyquist-rate ADC this value is usually set to Fs/2
R = Fs / (2 * bw) # R=1 for a Nyquist-rate ADC
dc_bins = 8 # if f_start is set to DC (f_start=1) then dc_bins should be set between 5 and 10 depending on the window used
signal_bins = 6
coh_gain_blackmanharris = 0.35875
full_scale = 2.0
full_scale_rms = full_scale / (2 * np.sqrt(2))
full_scale_rms_power = full_scale_rms ** 2

# I Data
y = i_data
y = y - sum(y) / len(y)
N = len(y)
fpb = Fs / N
f_start_bin = round(f_start / fpb) + 1
f_end_bin = math.floor(f_end / fpb)
in_band_bins = f_end_bin - f_start_bin + 1
fB = math.floor(N / (2 * R))
f_bin = round(f_in / fpb)
raw = np.fft.fft(y * blackmanharris(N))
amplitude = np.abs(raw) / N
ss_amplitude = 2 * amplitude[:N // 2]
ss_amplitude[0] = amplitude[0]
rms_ss_amplitude = ss_amplitude / np.sqrt(2)
rms_ss_amplitude[0] = amplitude[0]
rms_ss_amplitude = rms_ss_amplitude / coh_gain_blackmanharris
power = rms_ss_amplitude * rms_ss_amplitude
i_amplitude_dB = 20 * np.log10(rms_ss_amplitude / full_scale_rms)
i_power_dB = 10 * np.log10(power / full_scale_rms_power)
f_bin = (f_bin - f_start_bin) + 1
sndri = calculateSNDR_bh(raw[f_start_bin:f_end_bin], f_bin - f1 + 1, dc_bins, signal_bins)

# Q Data
y = q_data
y = y - sum(y) / len(y)
N = len(y)
fpb = Fs / N
f_start_bin = round(f_start / fpb) + 1
f_end_bin = math.floor(f_end / fpb)
in_band_bins = f_end_bin - f_start_bin + 1
fB = math.floor(N / (2 * R))

f_bin = round(f_in / fpb)

raw = np.fft.fft(y * blackmanharris(N))
amplitude = np.abs(raw) / N
ss_amplitude = 2 * amplitude[:N // 2]
ss_amplitude[0] = amplitude[0]
rms_ss_amplitude = ss_amplitude / np.sqrt(2)
rms_ss_amplitude[0] = amplitude[0]
rms_ss_amplitude = rms_ss_amplitude / coh_gain_blackmanharris
power = rms_ss_amplitude * rms_ss_amplitude
q_amplitude_dB = 20 * np.log10(rms_ss_amplitude / full_scale_rms)
q_power_dB = 10 * np.log10(power / full_scale_rms_power)
f_bin = (f_bin - f_start_bin) + 1
sndrq = calculateSNDR_bh(raw[f_start_bin:f_end_bin], f_bin - f1 + 1, dc_bins, signal_bins)
print("")
print(" SINAD_I (dB):",format(sndri, '.2f'))
print(" SINAD_Q (dB):",format(sndrq, '.2f'))
print("")
