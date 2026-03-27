%% 1 plot total:
% All 4 traces, each LOWPASS filtered at 5 Hz (solid)

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

%% ---- Lowpass filter (5 Hz) ----
fc = 5.0; % Hz cutoff

% resample to a uniform grid for stable digital filtering
fs_u = max(200, 20*fc);            % safe uniform resample rate
t_u  = (0:1/fs_u:t_s(end)).';

green_u = interp1(t_s, green,   t_u, 'linear', 'extrap');
red_u   = interp1(t_s, red,     t_u, 'linear', 'extrap');
ir_u    = interp1(t_s, ir,      t_u, 'linear', 'extrap');
off_u   = interp1(t_s, off_led, t_u, 'linear', 'extrap');

Wn = fc/(fs_u/2);
[b,a] = butter(5, Wn, 'low');

green_lp_u = filtfilt(b,a, green_u);
red_lp_u   = filtfilt(b,a, red_u);
ir_lp_u    = filtfilt(b,a, ir_u);
off_lp_u   = filtfilt(b,a, off_u);

% map filtered result back onto original non-uniform timestamps
green_lp = interp1(t_u, green_lp_u, t_s, 'linear', 'extrap');
red_lp   = interp1(t_u, red_lp_u,   t_s, 'linear', 'extrap');
ir_lp    = interp1(t_u, ir_lp_u,    t_s, 'linear', 'extrap');
off_lp   = interp1(t_u, off_lp_u,   t_s, 'linear', 'extrap');

%% ---- Colors ----
c_green  = [0.00 0.60 0.00];
c_red    = [1.00 0.00 0.00];
c_maroon = [0.50 0.00 0.00];
c_black  = [0.00 0.00 0.00];

%% ========= Plot: All 4 LOWPASS traces =========
figure; hold on;
plot(t_ms, green_lp, '-', 'Color', c_green,  'LineWidth', 1.8);
plot(t_ms, red_lp,   '-', 'Color', c_red,    'LineWidth', 1.8);
plot(t_ms, ir_lp,    '-', 'Color', c_maroon, 'LineWidth', 1.8);
plot(t_ms, off_lp,   '-', 'Color', c_black,  'LineWidth', 1.8);

xlabel('Time (ms)'); ylabel('Average Output (mV)');
title('All Signals Lowpass Filtered (5 Hz)');
legend('Green','Red','IR','No LED','Location','best');
grid on; hold off;

%% ---- Info ----
fprintf('Samples: %d\n', numel(t_ms));
fprintf('Total duration: %.3f ms\n', t_ms(end));
fprintf('Mean dt: %.2f us | Min dt: %.2f us | Max dt: %.2f us\n', ...
        mean(dt_us), min(dt_us), max(dt_us));
fprintf('Uniform resample fs for filtering: %.1f Hz\n', fs_u);