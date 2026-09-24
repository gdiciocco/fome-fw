package com.rusefi;

import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.config.generated.Fields;
import com.rusefi.tracing.Entry;
import com.rusefi.tracing.JsonOutput;
import com.rusefi.ui.RpmModel;

import javax.swing.*;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

import static com.rusefi.binaryprotocol.IoHelper.checkResponseCode;
import static com.rusefi.tools.ConsoleTools.startAndConnect;

public class PerformanceTraceHelper {
    public static void grabAdcGapTrace(JComponent parent, BinaryProtocol bp) {
        if (bp == null) {
            JOptionPane.showMessageDialog(parent, "Failed to locate serial ports");
            return;
        }

        byte[] armed = bp.executeCommand(Fields.TS_PERF_TRACE_BEGIN, new byte[]{1}, "arm ADC gap trace");
        if (!checkResponseCode(armed, (byte) Fields.TS_RESPONSE_OK)) {
            throw new IllegalStateException("Could not arm ADC gap trace (shared trace buffer in use)");
        }

        try {
            for (int i = 0; i < 300; i++) {
                Thread.sleep(100);
                byte[] packet = bp.executeCommand(Fields.TS_PERF_TRACE_GET_BUFFER, "get ADC gap trace");
                if (!checkResponseCode(packet, (byte) Fields.TS_RESPONSE_OK)) {
                    continue;
                }
                if ((packet.length - 1) % 8 != 0) {
                    throw new IllegalStateException("Unexpected ADC trace length=" + packet.length);
                }

                List<Entry> data = Entry.parseBuffer(packet);
                String fileName = FileLog.getDate() + "_adc_gap_trace.json";
                JsonOutput.writeToStream(data, Files.newOutputStream(Paths.get(fileName)));
                return;
            }

            // Reclaim the shared buffer if no gap occurred during the capture window.
            bp.executeCommand(Fields.TS_PERF_TRACE_BEGIN, "cancel ADC gap trace");
            bp.executeCommand(Fields.TS_PERF_TRACE_GET_BUFFER, "release ADC gap trace buffer");
            throw new IllegalStateException("No ADC update gap over 10 ms within 30 seconds");
        } catch (IOException | InterruptedException e) {
            throw new IllegalStateException(e);
        }
    }

    public static void grabPerformanceTrace(JComponent parent, BinaryProtocol bp) {
        if (bp == null) {
            JOptionPane.showMessageDialog(parent, "Failed to locate serial ports");
            return;
        }
        bp.executeCommand(Fields.TS_PERF_TRACE_BEGIN, "begin trace");

        try {
            Thread.sleep(500);

            byte[] packet = bp.executeCommand(Fields.TS_PERF_TRACE_GET_BUFFER, "get trace");
            if (!checkResponseCode(packet, (byte) Fields.TS_RESPONSE_OK) || ((packet.length - 1) % 8) != 0)
                throw new IllegalStateException("Unexpected packet, length=" + (packet != null ? 0 : packet.length));

            List<Entry> data = Entry.parseBuffer(packet);

            int rpm = RpmModel.getInstance().getValue();
            String fileName = FileLog.getDate() + "_rpm_" + rpm + "_rusEFI_trace" + ".json";


            JsonOutput.writeToStream(data, Files.newOutputStream(Paths.get(fileName)));
        } catch (IOException | InterruptedException e1) {
            throw new IllegalStateException(e1);
        }
    }

    public static void getPerformanceTune() {
        startAndConnect(linkManager -> {
            BinaryProtocol binaryProtocol = linkManager.getConnector().getBinaryProtocol();
            grabPerformanceTrace(null, binaryProtocol);
            System.exit(0);
            return null;
        });
    }
}
