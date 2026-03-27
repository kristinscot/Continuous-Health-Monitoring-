%% 4 plots total:
% 1) All 4 RAW traces (solid)
% 2) Green bandpass (0.5–7 Hz) only (solid)
% 3) Red   bandpass (0.5–7 Hz) only (solid)
% 4) IR    bandpass (0.5–7 Hz) only (solid)

clear; clc; close all;

%% ---- File ----
filename = 'data';   % replace with exact filename if needed

%% ---- Read data ----
M = readmatrix(filename);
M = M(~all(isnan(M),2), :);

if size(M,2) < 5
    error('File must contain 5 columns: dt_us, green, red, IR, off.');
end

dt_us   = M(:,1);
green   = M(:,2);
red     = M(:,3);
ir      = M(:,4);
off_led = M(:,5);

valid = ~any(isnan([dt_us, green, red, ir, off_led]), 2);
dt_us   = dt_us(valid);
green   = green(valid);
red     = red(valid);
ir      = ir(valid);
off_led = off_led(valid);

%% ---- Build time axis ----
t_us = [0; cumsum(dt_us(2:end))];
t_s  = t_us * 1e-6;
t_ms = t_us * 1e-3;

%% ---- Bandpass filter (0.5–7 Hz) for Green/Red/IR ----
f_lo = 0.5;
f_hi = 5.0;

fs_u = max(200, 20*f_hi);          % safe uniform resample rate
t_u  = (0:1/fs_u:t_s(end)).';

green_u = interp1(t_s, green, t_u, 'linear', 'extrap');
red_u   = interp1(t_s, red,   t_u, 'linear', 'extrap');
ir_u    = interp1(t_s, ir,    t_u, 'linear', 'extrap');

Wn = [f_lo f_hi]/(fs_u/2);
[b,a] = butter(5, Wn, 'bandpass');

green_bp = interp1(t_u, filtfilt(b,a, green_u), t_s, 'linear', 'extrap');
red_bp   = interp1(t_u, filtfilt(b,a, red_u),   t_s, 'linear', 'extrap');
ir_bp    = interp1(t_u, filtfilt(b,a, ir_u),    t_s, 'linear', 'extrap');

%% ---- Colors ----
c_green  = [0.00 0.60 0.00];
c_red    = [1.00 0.00 0.00];
c_maroon = [0.50 0.00 0.00];
c_black  = [0.00 0.00 0.00];

%% ========= Plot 1: All RAW traces =========
figure; hold on;
plot(t_ms, green,   '-', 'Color', c_green,  'LineWidth', 1.6);
plot(t_ms, red,     '-', 'Color', c_red,    'LineWidth', 1.6);
plot(t_ms, ir,      '-', 'Color', c_maroon, 'LineWidth', 1.6);
plot(t_ms, off_led, '-', 'Color', c_black,  'LineWidth', 1.6);
xlabel('Time (ms)'); ylabel('Average Output (mV)');
title('All Raw LED Signals');
legend('Green','Red','IR','No LED','Location','best');
grid on; hold off;

%% ========= Plot 2: Green bandpass only =========
figure;
plot(t_ms, green_bp, '-', 'Color', c_green, 'LineWidth', 2.0);
xlabel('Time (ms)'); ylabel('Output (mV)');
title('Green Bandpass (0.5–7 Hz)');
grid on;

%% ========= Plot 3: Red bandpass only =========
figure;
plot(t_ms, red_bp, '-', 'Color', c_red, 'LineWidth', 2.0);
xlabel('Time (ms)'); ylabel('Output (mV)');
title('Red Bandpass (0.5–7 Hz)');
grid on;

%% ========= Plot 4: IR bandpass only =========
figure;
plot(t_ms, ir_bp, '-', 'Color', c_maroon, 'LineWidth', 2.0);
xlabel('Time (ms)'); ylabel('Output (mV)');
title('IR Bandpass (0.5–7 Hz)');
grid on;

%% ---- Info ----
fprintf('Samples: %d\n', numel(t_ms));
fprintf('Total duration: %.3f ms\n', t_ms(end));
fprintf('Mean dt: %.2f us | Min dt: %.2f us | Max dt: %.2f us\n', ...
        mean(dt_us), min(dt_us), max(dt_us));
fprintf('Uniform resample fs for filtering: %.1f Hz\n', fs_u);