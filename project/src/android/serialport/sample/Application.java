package android.serialport.sample;

import java.io.File;
import java.io.IOException;


import android.content.SharedPreferences;
import android.serialport.SerialPort;
import android.serialport.SerialPortFinder;

public class Application extends android.app.Application {

	public SerialPortFinder mSerialPortFinder = new SerialPortFinder();
	private SerialPort mSerialPort = null;

	public SerialPort getSerialPort() {
		if (mSerialPort == null) {

			/* Read serial port parameters */
			SharedPreferences sp = getSharedPreferences("cedric.serial_preferences", MODE_PRIVATE);
			String path = sp.getString("DEVICE", "");
			int baudrate = Integer.decode(sp.getString("BAUDRATE", "-1"));

			/* Check parameters */
			if ( (path.length() == 0) || (baudrate == -1)) {
				return null;
			}

			/* Open the serial port */
			try {
				mSerialPort = new SerialPort(new File(path), baudrate);
			} catch (IOException e) {
				e.printStackTrace();
			} catch (InterruptedException e) {
				e.printStackTrace();
			}
		}
		return mSerialPort;
	}

	public void closeSerialPort() {
		if (mSerialPort != null) {
			mSerialPort.close();
			mSerialPort = null;
		}
	}
}
