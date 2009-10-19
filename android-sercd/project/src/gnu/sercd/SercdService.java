package gnu.sercd;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

public class SercdService extends Service {

	private static final String SERIALPORT = "serialport";
	private static final String INTERFACE = "interface";
	private static final String PORT = "port";

	public static void Start(Context ctxt, String serialport, String netinterface, int port) {
		Intent myself = new Intent(ctxt, SercdService.class);
		myself.putExtra(SERIALPORT, serialport);
		myself.putExtra(INTERFACE, netinterface);
		myself.putExtra(PORT, port);
		ctxt.startService(myself);
	}

	public static void Stop(Context ctxt) {
		Intent myself = new Intent(ctxt, SercdService.class);
		ctxt.stopService(myself);
	}

	@Override
	public IBinder onBind(Intent intent) {
		return null;
	}

	private enum ProxyState {
		STATE_READY, STATE_CONNECTED, STATE_PORT_OPENED, STATE_STOPPED, STATE_CRASHED
	}
	private ProxyState mState;
	private String mSerialport;
	private String mInterface;
	private int mPort;
	private NotificationManager mNotificationManager;
	private Context mContext;
	private PendingIntent mContentIntent;
	private boolean mExiting;

	private Thread mSercdThread = new Thread() {
		@Override
		public void run() {
			//ChangeState(ProxyState.STATE_READY);
			main(mSerialport, mInterface, mPort);
			if (mExiting) {
				ChangeState(ProxyState.STATE_STOPPED);
			} else {
				ChangeState(ProxyState.STATE_CRASHED);
			}
		}
	};

	@Override
	public void onCreate() {
		super.onCreate();
		System.loadLibrary("sercd");
		mNotificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
		mContentIntent = PendingIntent.getActivity(this, 0, new Intent(this, Sercd.class), 0);
		mContext = getApplicationContext();
		mState = ProxyState.STATE_STOPPED;
		mExiting = false;
	}

	@Override
	public void onStart(Intent intent, int startId) {
		super.onStart(intent, startId);
		mSerialport = intent.getStringExtra(SERIALPORT);
		mInterface = intent.getStringExtra(INTERFACE);
		mPort = intent.getIntExtra(PORT, 0);
		mSercdThread.start();
	}

	@Override
	public void onDestroy() {
		if (mSercdThread.isAlive()) {
			mExiting = true;
			exit();
			try {
				mSercdThread.join();
			} catch (InterruptedException e) {
				e.printStackTrace();
			}
		}
		ChangeState(ProxyState.STATE_STOPPED);
		super.onDestroy();
	}

	/**
	 * This method is called from libsercd.so
	 * @param newstate New sercd state.
	 */
	private synchronized void ChangeState(ProxyState newstate) {

		if (newstate == mState)
			return;

		/* Remove old notification if necessary */
		if (mState != ProxyState.STATE_STOPPED) {
			mNotificationManager.cancelAll();
		}

		/* Create new notification */
		long when = System.currentTimeMillis();
		CharSequence contentTitle = getText(R.string.app_name);
		int icon;
		CharSequence text;

		switch(newstate) {
		case STATE_READY:
			icon = R.drawable.notification_icon_ready;
			text = getText(R.string.notif_ready);
			break;
		case STATE_CONNECTED:
			icon = R.drawable.notification_icon_ready;
			text = getText(R.string.notif_connected);
			break;
		case STATE_PORT_OPENED:
			icon = R.drawable.notification_icon_connected;
			text = getText(R.string.notif_opened);
			break;
		case STATE_CRASHED:
			icon = R.drawable.notification_icon_ready;
			text = getText(R.string.notif_crash);
		default:
			return;
		}

		Notification notification = new Notification(icon, text, when);
		notification.setLatestEventInfo(mContext, contentTitle, text, mContentIntent);
		notification.flags |= Notification.FLAG_ONGOING_EVENT | Notification.FLAG_NO_CLEAR;
		mNotificationManager.notify(0, notification);

		mState = newstate;
	}

	private native int main(String serialport, String netinterface, int port);
	private native void exit();
}
