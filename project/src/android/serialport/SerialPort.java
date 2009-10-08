package android.serialport;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import android.util.Log;

public class SerialPort {

	private static final String TAG = "SerialPort";

	private FileDescriptor mFd;
	private FileInputStream mFileInputStream;
	private FileOutputStream mFileOutputStream;

	public SerialPort(File device, int baudrate) throws IOException, InterruptedException {

		/* Check access permission */
		if (!device.canRead() || !device.canWrite()) {
			/* Missing read/write permission, trying to chmod the file */
			Process su = Runtime.getRuntime().exec("/system/bin/su");
			String cmd =
				"chmod 666 " + device.getAbsolutePath() + "\n" +
				"exit\n";
			su.getOutputStream().write(cmd.getBytes());
			if ((su.waitFor() != 0) || !device.canRead() || !device.canWrite()) {
				throw new IOException("Modifying permissions failed");
			}
		}

		mFd = open(device.getAbsolutePath(), baudrate);
		if (mFd == null) {
			Log.e(TAG, "native open returns null");
			return;
		}
		mFileInputStream = new FileInputStream(mFd);
		mFileOutputStream = new FileOutputStream(mFd);
	}

	public native void close();

	public InputStream getInputStream() {
		return mFileInputStream;
	}

	public OutputStream getOutputStream() {
		return mFileOutputStream;
	}

	private native FileDescriptor open(String path, int baudrate);

	static {
		System.loadLibrary("serial_port");
	}
}
