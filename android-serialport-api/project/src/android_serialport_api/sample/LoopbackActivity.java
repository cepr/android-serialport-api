/*
 * Copyright 2009 Cedric Priscal
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. 
 */

package android_serialport_api.sample;

import java.io.IOException;

import android.os.Bundle;
import android.widget.TextView;

public class LoopbackActivity extends SerialPortActivity {

	int mIncoming;
	int mOutgoing;
	SendingThread mSendingThread;
	TextView mTextViewOutgoing;
	TextView mTextViewIncoming;

	private class SendingThread extends Thread {
		@Override
		public void run() {
			byte[] buffer = new byte[1024];
			int i;
			for (i=0; i<buffer.length; i++) {
				buffer[i] = 0x55;
			}
			while(!isInterrupted()) {
				try {
					if (mOutputStream != null) {
						mOutputStream.write(buffer);
					} else {
						return;
					}
				} catch (IOException e) {
					e.printStackTrace();
					return;
				}
				mOutgoing += buffer.length;
				runOnUiThread(new Runnable() {
					public void run() {
						mTextViewOutgoing.setText(new Integer(mOutgoing).toString());
					}
				});
			}
		}
	}
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.loopback);
		mTextViewOutgoing = (TextView) findViewById(R.id.TextViewOutgoingValue);
		mTextViewIncoming = (TextView) findViewById(R.id.TextViewIncomingValue);
		if (mSerialPort != null) {
			mSendingThread = new SendingThread();
			mSendingThread.start();
		}
	}

	@Override
	protected void onDataReceived(byte[] buffer, int size) {
		mIncoming += size;
		runOnUiThread(new Runnable() {
			public void run() {
				mTextViewIncoming.setText(new Integer(mIncoming).toString());
			}
		});
	}

	@Override
	protected void onDestroy() {
		if (mSendingThread != null) mSendingThread.interrupt();
		super.onDestroy();
	}
}
