package gnu.sercd;

import java.io.File;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.ArrayList;
import java.util.Enumeration;

import android.app.AlertDialog;
import android.os.Bundle;
import android.preference.CheckBoxPreference;
import android.preference.EditTextPreference;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.Preference.OnPreferenceChangeListener;
import android.serialport.SerialPortFinder;
import android.util.Log;

public class Sercd extends PreferenceActivity {

	protected static final String TAG = "Sercd";
	private CheckBoxPreference mEnabled;
	private ListPreference mSerialPort;
	private ListPreference mNetworkInterfaces;
	private EditTextPreference mNetworkPort;

	private OnPreferenceChangeListener mPreferenceChangeListener = new OnPreferenceChangeListener() {
		@Override
		public boolean onPreferenceChange(Preference preference, Object newValue) {
			preference.setSummary((CharSequence)newValue);
			return true;
		}
	};
	
	/** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        /* Load preferences from XML */
        addPreferencesFromResource(R.xml.preferences);

        /* Fetch corresponding Java objects */
    	mEnabled = (CheckBoxPreference)findPreference("enabled");
    	mSerialPort = (ListPreference)findPreference("serialport");
    	mNetworkInterfaces = (ListPreference)findPreference("netinterface");
    	mNetworkPort = (EditTextPreference)findPreference("portnumber");

    	/* Complete lists, etc */
    	mSerialPort.setOnPreferenceChangeListener(mPreferenceChangeListener);
    	mSerialPort.setSummary(mSerialPort.getValue());
    	mNetworkInterfaces.setOnPreferenceChangeListener(mPreferenceChangeListener);
    	mNetworkInterfaces.setSummary(mNetworkInterfaces.getValue());
    	mNetworkPort.setOnPreferenceChangeListener(mPreferenceChangeListener);
    	mNetworkPort.setSummary(mNetworkPort.getText());
    	mEnabled.setOnPreferenceChangeListener(new OnPreferenceChangeListener() {
			@Override
			public boolean onPreferenceChange(Preference preference, Object newValue) {
				if ((Boolean)newValue) {
					String serialport = mSerialPort.getValue();
					String networkinterface = mNetworkInterfaces.getValue();
					
					/* Check access permission */
					File device = new File(serialport);
					if (!device.canRead() || !device.canWrite()) {
						try {
							/* Missing read/write permission, trying to chmod the file */
							Process su;
							su = Runtime.getRuntime().exec("/system/bin/su");
							String cmd = "chmod 666 " + device.getAbsolutePath() + "\n"
									+ "exit\n";
							su.getOutputStream().write(cmd.getBytes());
							if ((su.waitFor() != 0) || !device.canRead()
									|| !device.canWrite()) {
								new AlertDialog.Builder(Sercd.this)
									.setMessage(R.string.su_failed)
									.show();
								return false;
							}
						} catch (Exception e) {
							e.printStackTrace();
							new AlertDialog.Builder(Sercd.this)
									.setMessage(R.string.su_failed)
									.show();
							return false;
						}
					}

					int port = Integer.parseInt(mNetworkPort.getText());
							Log.d(TAG, "Starting sercd with parameters "
									+ serialport + ", " + networkinterface
									+ ":" + port);
					SercdService.Start(
							Sercd.this,
							serialport,
							networkinterface,
							port
					);
				} else {
					SercdService.Stop(Sercd.this);
				}
				return true;
			}
		});

    	feedNetworkInterfacesList();
        feedSerialPortList();

        if (mEnabled.isChecked()) {
//        	SercdService.Start(this, "/dev/ttyMSM2", "127.0.0.1", 30001);
        }
    }

    private void feedSerialPortList() {
    	SerialPortFinder spf = new SerialPortFinder();
    	mSerialPort.setEntries(spf.getAllDevices());
    	mSerialPort.setEntryValues(spf.getAllDevicesPath());
	}

	private void feedNetworkInterfacesList() {
        Enumeration<NetworkInterface> nets;
		try {
			nets = NetworkInterface.getNetworkInterfaces();
	        ArrayList<String> displaynames = new ArrayList<String>();
	        ArrayList<String> names = new ArrayList<String>();
	        while(nets.hasMoreElements()) {
	        	NetworkInterface net = nets.nextElement();
	        	Enumeration<InetAddress> inets = net.getInetAddresses();
	        	while(inets.hasMoreElements()) {
	        		InetAddress inet = inets.nextElement();
	        		String address = inet.getHostAddress();
		        	displaynames.add(address + " (" + net.getDisplayName() + ")");
		        	names.add(address);
		        }
	        }
	        int size = displaynames.size();
	        int i;
	        CharSequence[] a = new CharSequence[size];
	        CharSequence[] b = new CharSequence[size];
	        for (i=0; i<size; i++) {
	        	a[i] = displaynames.get(i);
	        	b[i] = names.get(i);
	        }
	        mNetworkInterfaces.setEntries(a);
	        mNetworkInterfaces.setEntryValues(b);
		} catch (SocketException e) {
			e.printStackTrace();
		}
    }

	@Override
	protected void onDestroy() {
//		SercdService.Stop(this);
		super.onDestroy();
	}
}
