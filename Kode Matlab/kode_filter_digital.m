clear;
clc;
close all;

%% 1. Parameter Dasar Sistem
Fs = 2500;              % Frekuensi Sampling (2500 Hz)
% Gunakan 25000 agar resolusi frekuensi tepat 0.05 Hz per titik. 
% Ini memastikan f=50.00 Hz, 100.00 Hz, dan 150.00 Hz tepat mengenai dasar jurang notch.
n_points = 25000;       

%% 2. Desain Filter Butterworth Orde 2 (HPF dan LPF)
% HPF 20 Hz (Menghilangkan motion artifact / gerakan kabel)
fc_hpf = 20;
[b_hpf, a_hpf] = butter(2, fc_hpf/(Fs/2), 'high');

% LPF 500 Hz (Menghilangkan frekuensi tinggi & Anti-aliasing)
fc_lpf = 500;
[b_lpf, a_lpf] = butter(2, fc_lpf/(Fs/2), 'low');

%% 3. Desain Band-Stop Filter (BSF) Notch di 50, 100, dan 150 Hz
% Q disesuaikan: 50 Hz lebih lebar untuk menangkap fluktuasi PLN, 
% Harmonik 100 & 150 Hz dibuat sangat sempit agar tidak merusak EMG.
Q_50  = 25; 
Q_100 = 50;
Q_150 = 75; 

[b_n50, a_n50]   = iirnotch(50/(Fs/2), 50/(Fs/2)/Q_50);
[b_n100, a_n100] = iirnotch(100/(Fs/2), 100/(Fs/2)/Q_100);
[b_n150, a_n150] = iirnotch(150/(Fs/2), 150/(Fs/2)/Q_150);

%% 4. Kaskade Double Notch 50 Hz (Ekstrem Redaman)
% Kita kalikan (konvolusi) filter 50 Hz dua kali agar redamannya berlipat ganda
b_n50_double = conv(b_n50, b_n50);
a_n50_double = conv(a_n50, a_n50);

% Konvolusi untuk BSF Gabungan (UNTUK KEPERLUAN PLOT TOTAL)
b_bsf_total = conv(conv(b_n50_double, b_n100), b_n150);
a_bsf_total = conv(conv(a_n50_double, a_n100), a_n150);

%% 5. Analisis Respons Frekuensi (freqz)
[h_hpf, f] = freqz(b_hpf, a_hpf, n_points, Fs);
[h_lpf, ~] = freqz(b_lpf, a_lpf, n_points, Fs);
[h_bsf, ~] = freqz(b_bsf_total, a_bsf_total, n_points, Fs);

% Respons Total Sistem (Cascade)
h_total = h_hpf .* h_lpf .* h_bsf;

%% 6. Visualisasi Plot Grafik
figure('Name', 'Respons Frekuensi Filter EMG', 'NumberTitle', 'off');

% Subplot 1: Plot Individu Filter
subplot(2,1,1);
plot(f, 20*log10(abs(h_hpf)), 'b', 'LineWidth', 1.5); hold on;
plot(f, 20*log10(abs(h_lpf)), 'r', 'LineWidth', 1.5);
plot(f, 20*log10(abs(h_bsf)), 'g', 'LineWidth', 1.5);
grid on;
xlim([0 500]); ylim([-80 5]);
title('Respons Individu (HPF 20Hz, LPF 500Hz, BSF 50/100/150Hz)');
xlabel('Frekuensi (Hz)'); ylabel('Magnitude (dB)');
legend('HPF 20 Hz', 'LPF 500 Hz', 'BSF Gabungan', 'Location', 'southwest');

% Subplot 2: Plot Total Sistem Gabungan
subplot(2,1,2);
plot(f, 20*log10(abs(h_total)), 'k', 'LineWidth', 2);
grid on;
xlim([0 500]); ylim([-80 5]);
title('Respons Total Sistem EMG');
xlabel('Frekuensi (Hz)'); ylabel('Magnitude (dB)');

%% 7. PERHITUNGAN NILAI REDAMAN NUMERIK TOTAL
f_test = [50, 100, 150];
fprintf('=== ANALISIS NUMERIK REDAMAN TOTAL ===\n\n');
fprintf('Frekuensi (Hz) | Total Redaman (dB)\n');
fprintf('-----------------------------------\n');
for i = 1:length(f_test)
    [~, idx] = min(abs(f - f_test(i)));
    mag_total = 20*log10(abs(h_total(idx)));
    fprintf('%14d | %17.2f\n', f_test(i), mag_total);
end

%% 8. Cetak Koefisien untuk C++ (ESP32)
fprintf('\n// === SALIN KODE DI BAWAH INI KE FILE ARDUINO (ESP32) ===\n\n');

print_arr('b_hpf', b_hpf); print_arr('a_hpf', a_hpf); fprintf('\n');
print_arr('b_lpf', b_lpf); print_arr('a_lpf', a_lpf); fprintf('\n');
print_arr('b_n50', b_n50); print_arr('a_n50', a_n50); fprintf('\n');
print_arr('b_n100', b_n100); print_arr('a_n100', a_n100); fprintf('\n');
print_arr('b_n150', b_n150); print_arr('a_n150', a_n150); fprintf('\n');

function print_arr(name, arr)
    fprintf('const float %s[3] = {', name);
    for i = 1:length(arr)
        fprintf('%.10ff', arr(i));
        if i < length(arr), fprintf(', '); end
    end
    fprintf('};\n');
end
