function procEmg(path)
    data = readtable(path);
    time = data.Var1;
    voltage = data.Var2;
    
    rmsTab = [];
    
    %for i=1:length(voltage)
        % if(abs(voltage(i)) > threshold)
        %     temp = voltage(i)
        %     for ii=i:length(voltage)
        % 
        %     end
        %     break;
        % end
    
    %end
    dt = mean(diff(time));
    Fs = 1 / dt;
    fprintf('Sampling Rate: %.2f Hz\n', Fs);
    
    notch = 60;
    wo = notch / (Fs/2);
    bw = wo / 35; 
    [b, a] = iirnotch(wo, bw);
    clean_voltage = filtfilt(b, a, voltage);
    clean_voltage=abs(voltage);
    mov = movmean(clean_voltage, 700);
    
    t = ceil(2/(time(2)-time(1)));
    
    a = mean(clean_voltage(1:t));
    s = std(clean_voltage(1:t));
    rmss = [length(time)];
    lrms = 0;
    threshold = a+2*s
    logging = 0;
    startTime = 0;
    endTime = 0;
    for i=1:length(time)
        if(logging == 0)
            if(mov(i) > threshold)
                logging = 1;
                log = [];
                startTime = time(i);
            end
        end
        if(logging == 1)
            log = [log,clean_voltage(i)];
            if(mov(i) < threshold)
                logging = false;
                if(length(log) > 1000)
                    endTime = time(i);
                    lrms = rms(log);
                    rmsTab = [rmsTab; [startTime, endTime, lrms]];
                end
            end
           
        end
        rmss(i) = lrms;
    end
    
    longThreshold = threshold*ones(height(data),1);
    figure;
    hold on;
    plot(time, clean_voltage, 'b');
    plot(time, longThreshold, 'r');
    plot(time, mov, 'g');
    plot(time, rmss, 'c');
    for i = 1:height(rmsTab)
        sprintf("table value: %i", rmsTab(i,1))
        xline(rmsTab(i,1))
        xline(rmsTab(i,2))
    end
    xlabel('Time (s)');
    ylabel('Voltage (V)');
    title('Voltage vs Time');
    xlim([floor(time(1)*10)/10, ceil(time(length(time))*10)/10])
    ylim([0, ceil(max(voltage)*100)/100]);
    hold off;
end