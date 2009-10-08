package android.serialport.sample;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;


import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.DialogInterface.OnClickListener;
import android.os.Bundle;
import android.serialport.SerialPort;

public abstract class SerialPortActivity extends Activity {

	protected Application mApplication;
	protected SerialPort mSerialPort;
	protected OutputStream mOutputStream;
	private InputStream mInputStream;
	private ReadThread mReadThread;

	private class ReadThread extends Thread {

		@Override
		public void run() {
			super.run();
			while(!isInterrupted()) {
				int size;
				try {
					byte[] buffer = new byte[64];
					if (mInputStream == null) return;
					size = mInputStream.read(buffer);
					if (size > 0) {
						onDataReceived(buffer, size);
					}
				} catch (IOException e) {
					e.printStackTrace();
					return;
				}
			}
		}
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		mApplication = (Application) getApplication();
		mSerialPort = mApplication.getSerialPort();
		if (mSerialPort == null)
		{
			AlertDialog.Builder b = new AlertDialog.Builder(this);
			b.setTitle("Error");
			b.setMessage("Please configure your serial port.");
			b.setPositiveButton("OK", new OnClickListener() {
				public void onClick(DialogInterface dialog, int which) {
					startActivity(new Intent(SerialPortActivity.this, SerialPortPreferences.class));
					SerialPortActivity.this.finish();
				}
			});
			b.show();
		} else {
			mOutputStream = mSerialPort.getOutputStream();
			mInputStream = mSerialPort.getInputStream();
			
			/* Create a receiving thread */
			mReadThread = new ReadThread();
			mReadThread.start();
		}
	}

	protected abstract void onDataReceived(final byte[] buffer, final int size);

	@Override
	protected void onDestroy() {
		if (mReadThread != null)
			mReadThread.interrupt();
		mApplication.closeSerialPort();
		mSerialPort = null;
		super.onDestroy();
	}
}
